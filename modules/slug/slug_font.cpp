/*************************************************************************/
/*  slug_font.cpp                                                       */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*************************************************************************/

#include "slug_font.h"

#include "core/os/file_access.h"
#include "core/os/os.h"
#include "core/print_string.h"
#include "slug_shader.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// Both curve_tex and band_tex are 4096 texels wide; see slug_shader.h for
// how the fragment shader walks them. Bands overlap slightly (BAND_EPS) so a
// curve that straddles a band boundary is tested by both sides.
static const int SLUG_TEX_WIDTH = 4096;
static const int SLUG_MAX_BANDS = 16;
static const float SLUG_BAND_EPS = 1.0f / 1024.0f;

// One quadratic Bezier in em space; straight lines are stored as {p1, p2, p2}.
struct SlugQCurve {
	Vector2 p1, p2, p3;
};

struct SlugContour {
	Vector<SlugQCurve> curves;
};

struct SlugGlyphBuild {
	uint32_t codepoint = 0;
	FT_UInt gi = 0;
	float advance = 0.0f;
	Vector<SlugContour> contours;
	Rect2 bounds;
	bool has_outline = false;
};

// A curve already placed in curve_tex, with the bbox needed to bin it into
// bands and to sort each band's list.
struct SlugCurveRef {
	Point2i loc;
	float xmin, xmax, ymin, ymax;
};

struct SlugSortDescX {
	bool operator()(const SlugCurveRef &p_a, const SlugCurveRef &p_b) const { return p_a.xmax > p_b.xmax; }
};

struct SlugSortDescY {
	bool operator()(const SlugCurveRef &p_a, const SlugCurveRef &p_b) const { return p_a.ymax > p_b.ymax; }
};

struct SlugDecomposeCtx {
	Vector<SlugContour> *contours = nullptr;
	Vector2 cur;
	float inv_upem = 1.0f;
	bool has_cubic = false;
};

static int _slug_move_to(const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	ctx->contours->push_back(SlugContour());
	ctx->cur = Vector2(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem);
	return 0;
}

static int _slug_line_to(const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	Vector2 p(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem);
	if (ctx->cur != p) { // Skip a degenerate {p1, p2, p2} where p1 == p2.
		SlugQCurve c;
		c.p1 = ctx->cur;
		c.p2 = p;
		c.p3 = p;
		ctx->contours->write[ctx->contours->size() - 1].curves.push_back(c);
	}
	ctx->cur = p;
	return 0;
}

static int _slug_conic_to(const FT_Vector *p_control, const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	Vector2 ctrl(p_control->x * ctx->inv_upem, p_control->y * ctx->inv_upem);
	Vector2 p(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem);
	if (!(ctx->cur == ctrl && ctrl == p)) { // Skip a fully degenerate curve.
		SlugQCurve c;
		c.p1 = ctx->cur;
		c.p2 = ctrl;
		c.p3 = p;
		ctx->contours->write[ctx->contours->size() - 1].curves.push_back(c);
	}
	ctx->cur = p;
	return 0;
}

static int _slug_cubic_to(const FT_Vector *p_c1, const FT_Vector *p_c2, const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	ctx->has_cubic = true;
	return 1; // Abort the decomposition; the font is rejected as a whole.
}

// Max curves in any band if the [p_lo, p_hi] extent is split into p_bands
// equal bands (using .ymin/.ymax when p_use_y, .xmin/.xmax otherwise).
static int _slug_max_band_count(const Vector<SlugCurveRef> &p_list, float p_lo, float p_hi, int p_bands, bool p_use_y) {
	float extent = p_hi - p_lo;
	int max_count = 0;
	for (int b = 0; b < p_bands; b++) {
		float lo = p_lo + extent * b / p_bands;
		float hi = p_lo + extent * (b + 1) / p_bands;
		int count = 0;
		for (int i = 0; i < p_list.size(); i++) {
			float cmin = p_use_y ? p_list[i].ymin : p_list[i].xmin;
			float cmax = p_use_y ? p_list[i].ymax : p_list[i].xmax;
			if (cmax >= lo - SLUG_BAND_EPS && cmin <= hi + SLUG_BAND_EPS) {
				count++;
			}
		}
		max_count = MAX(max_count, count);
	}
	return max_count;
}

void SlugFont::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_font_data", "font_data"), &SlugFont::set_font_data);
	ClassDB::bind_method(D_METHOD("get_font_data"), &SlugFont::get_font_data);

	ClassDB::bind_method(D_METHOD("is_valid"), &SlugFont::is_valid);
	ClassDB::bind_method(D_METHOD("get_glyph_count"), &SlugFont::get_glyph_count);
	ClassDB::bind_method(D_METHOD("get_build_time_usec"), &SlugFont::get_build_time_usec);
	ClassDB::bind_method(D_METHOD("get_max_curves_per_band"), &SlugFont::get_max_curves_per_band);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "font_data", PROPERTY_HINT_RESOURCE_TYPE, "DynamicFontData"), "set_font_data", "get_font_data");
}

void SlugFont::set_font_data(const Ref<DynamicFontData> &p_font_data) {
	if (font_data == p_font_data) {
		return;
	}
	font_data = p_font_data;
	_clear_built();
	emit_changed();
}

Ref<DynamicFontData> SlugFont::get_font_data() const {
	return font_data;
}

void SlugFont::_clear_built() {
	built = false;
	valid = false;
	build_time_usec = 0;
	max_curves_per_band = 0;
	ascent = 0.0f;
	descent = 0.0f;
	line_height = 0.0f;
	glyphs.clear();
	kerning.clear();
	curve_tex.unref();
	band_tex.unref();
	glyph_tex.unref();
	material.unref();
	shader.unref();
}

void SlugFont::_ensure_built() {
	if (built) {
		return;
	}
	built = true;

	if (font_data.is_null()) {
		return;
	}
	String path = font_data->get_font_path();
	ERR_FAIL_COND(path.empty());

	Vector<uint8_t> file_data = FileAccess::get_file_as_array(path);
	ERR_FAIL_COND_MSG(file_data.empty(), "SlugFont: could not read font file '" + path + "'.");

	uint64_t start_usec = OS::get_singleton()->get_ticks_usec();

	FT_Library library;
	ERR_FAIL_COND_MSG(FT_Init_FreeType(&library) != 0, "SlugFont: error initializing FreeType.");

	FT_Face face;
	if (FT_New_Memory_Face(library, file_data.ptr(), file_data.size(), 0, &face) != 0) {
		FT_Done_FreeType(library);
		ERR_FAIL_MSG("SlugFont: could not load font face '" + path + "'.");
	}
	if (!FT_IS_SCALABLE(face)) {
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		ERR_FAIL_MSG("SlugFont: font is not scalable: '" + path + "'.");
	}

	float inv_upem = 1.0f / (float)face->units_per_EM;
	ascent = face->ascender * inv_upem;
	descent = -face->descender * inv_upem;
	line_height = face->height * inv_upem;

	// Charset: ASCII, Latin-1 supplement, and the replacement character.
	Vector<uint32_t> codepoints;
	for (uint32_t c = 0x0020; c <= 0x007E; c++) {
		codepoints.push_back(c);
	}
	for (uint32_t c = 0x00A0; c <= 0x00FF; c++) {
		codepoints.push_back(c);
	}
	codepoints.push_back(0xFFFD);

	Vector<SlugGlyphBuild> builds;
	bool any_cubic = false;

	FT_Outline_Funcs funcs;
	funcs.move_to = _slug_move_to;
	funcs.line_to = _slug_line_to;
	funcs.conic_to = _slug_conic_to;
	funcs.cubic_to = _slug_cubic_to;
	funcs.shift = 0;
	funcs.delta = 0;

	for (int i = 0; i < codepoints.size(); i++) {
		FT_UInt gi = FT_Get_Char_Index(face, codepoints[i]);
		if (gi == 0) {
			continue;
		}
		if (FT_Load_Glyph(face, gi, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
			continue;
		}

		SlugGlyphBuild gb;
		gb.codepoint = codepoints[i];
		gb.gi = gi;
		gb.advance = face->glyph->advance.x * inv_upem;

		if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_contours > 0) {
			SlugDecomposeCtx ctx;
			ctx.contours = &gb.contours;
			ctx.inv_upem = inv_upem;
			FT_Outline_Decompose(&face->glyph->outline, &funcs, &ctx);

			if (ctx.has_cubic) {
				any_cubic = true;
			}

			bool first = true;
			for (int ci = 0; ci < gb.contours.size(); ci++) {
				const Vector<SlugQCurve> &curves = gb.contours[ci].curves;
				for (int qi = 0; qi < curves.size(); qi++) {
					Vector2 pts[3] = { curves[qi].p1, curves[qi].p2, curves[qi].p3 };
					for (int pi = 0; pi < 3; pi++) {
						if (first) {
							gb.bounds = Rect2(pts[pi], Size2());
							first = false;
						} else {
							gb.bounds.expand_to(pts[pi]);
						}
					}
				}
			}
			gb.has_outline = !first;
		}

		builds.push_back(gb);
	}

	if (any_cubic) {
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		ERR_FAIL_MSG("SlugFont: cubic (CFF/OTF) outlines are not supported: " + path);
	}

	if (FT_HAS_KERNING(face)) {
		for (int i = 0; i < builds.size(); i++) {
			for (int j = 0; j < builds.size(); j++) {
				FT_Vector k;
				if (FT_Get_Kerning(face, builds[i].gi, builds[j].gi, FT_KERNING_UNSCALED, &k) == 0 && k.x != 0) {
					uint64_t key = ((uint64_t)builds[i].codepoint << 32) | builds[j].codepoint;
					kerning[key] = k.x * inv_upem;
				}
			}
		}
	}

	FT_Done_Face(face);
	FT_Done_FreeType(library);

	// Pack every contour's curves into curve_tex, one contour per row span;
	// a contour never crosses a row. Keep each curve's texel location so the
	// band pass below can reference it.
	Vector<Color> curve_px;
	int curve_cx = 0, curve_cy = 0;
	Vector<Vector<SlugCurveRef> > glyph_curves;
	glyph_curves.resize(builds.size());

	for (int bi = 0; bi < builds.size(); bi++) {
		if (!builds[bi].has_outline) {
			continue;
		}
		Vector<SlugCurveRef> &refs = glyph_curves.write[bi];
		const Vector<SlugContour> &contours = builds[bi].contours;
		for (int ci = 0; ci < contours.size(); ci++) {
			const Vector<SlugQCurve> &curves = contours[ci].curves;
			int n = curves.size();
			if (n == 0) {
				continue;
			}
			if (curve_cx + n + 1 > SLUG_TEX_WIDTH) {
				curve_cx = 0;
				curve_cy++;
			}
			int need = (curve_cy + 1) * SLUG_TEX_WIDTH;
			if (curve_px.size() < need) {
				curve_px.resize(need);
			}
			for (int qi = 0; qi < n; qi++) {
				const SlugQCurve &c = curves[qi];
				SlugCurveRef ref;
				ref.loc = Point2i(curve_cx + qi, curve_cy);
				ref.xmin = MIN(c.p1.x, MIN(c.p2.x, c.p3.x));
				ref.xmax = MAX(c.p1.x, MAX(c.p2.x, c.p3.x));
				ref.ymin = MIN(c.p1.y, MIN(c.p2.y, c.p3.y));
				ref.ymax = MAX(c.p1.y, MAX(c.p2.y, c.p3.y));
				refs.push_back(ref);
				curve_px.write[curve_cy * SLUG_TEX_WIDTH + curve_cx + qi] = Color(c.p1.x, c.p1.y, c.p2.x, c.p2.y);
			}
			curve_px.write[curve_cy * SLUG_TEX_WIDTH + curve_cx + n] = Color(curves[n - 1].p3.x, curves[n - 1].p3.y, 0.0f, 0.0f);
			curve_cx += n + 1;
		}
	}
	int curve_rows = MAX(1, curve_px.size() / SLUG_TEX_WIDTH);
	curve_px.resize(curve_rows * SLUG_TEX_WIDTH);

	// One band block per glyph in band_tex, and the two summary texels of
	// glyph_tex; see slug_shader.h for the exact layout both encode.
	Vector<Color> band_px;
	int band_cx = 0, band_cy = 0;
	Vector<Color> glyph_px;
	glyph_px.resize(builds.size() * 2);

	for (int bi = 0; bi < builds.size(); bi++) {
		const SlugGlyphBuild &gb = builds[bi];

		Glyph glyph;
		glyph.index = bi;
		glyph.advance = gb.advance;
		glyph.bounds = gb.bounds;
		glyph.has_outline = gb.has_outline;
		glyphs[gb.codepoint] = glyph;

		if (!gb.has_outline) {
			glyph_px.write[bi * 2 + 0] = Color();
			glyph_px.write[bi * 2 + 1] = Color();
			continue;
		}

		const Vector<SlugCurveRef> &all_refs = glyph_curves[bi];
		float xmin = gb.bounds.position.x, xmax = gb.bounds.position.x + gb.bounds.size.x;
		float ymin = gb.bounds.position.y, ymax = gb.bounds.position.y + gb.bounds.size.y;

		Vector<SlugCurveRef> h_all, v_all;
		for (int i = 0; i < all_refs.size(); i++) {
			const SlugCurveRef &c = all_refs[i];
			if (!(c.ymin == c.ymax)) { // Not a horizontal line: eligible for horizontal bands.
				h_all.push_back(c);
			}
			if (!(c.xmin == c.xmax)) { // Not a vertical line: eligible for vertical bands.
				v_all.push_back(c);
			}
		}

		int best_nh = 1;
		int best_nh_max = _slug_max_band_count(h_all, ymin, ymax, 1, true);
		for (int nh = 2; nh <= SLUG_MAX_BANDS; nh++) {
			int m = _slug_max_band_count(h_all, ymin, ymax, nh, true);
			if (m < best_nh_max) {
				best_nh_max = m;
				best_nh = nh;
			}
		}
		int best_nv = 1;
		int best_nv_max = _slug_max_band_count(v_all, xmin, xmax, 1, false);
		for (int nv = 2; nv <= SLUG_MAX_BANDS; nv++) {
			int m = _slug_max_band_count(v_all, xmin, xmax, nv, false);
			if (m < best_nv_max) {
				best_nv_max = m;
				best_nv = nv;
			}
		}

		Vector<Vector<SlugCurveRef> > h_bands;
		h_bands.resize(best_nh);
		for (int b = 0; b < best_nh; b++) {
			float lo = ymin + (ymax - ymin) * b / best_nh;
			float hi = ymin + (ymax - ymin) * (b + 1) / best_nh;
			for (int i = 0; i < h_all.size(); i++) {
				if (h_all[i].ymax >= lo - SLUG_BAND_EPS && h_all[i].ymin <= hi + SLUG_BAND_EPS) {
					h_bands.write[b].push_back(h_all[i]);
				}
			}
			h_bands.write[b].sort_custom<SlugSortDescX>();
			max_curves_per_band = MAX(max_curves_per_band, h_bands[b].size());
		}

		Vector<Vector<SlugCurveRef> > v_bands;
		v_bands.resize(best_nv);
		for (int b = 0; b < best_nv; b++) {
			float lo = xmin + (xmax - xmin) * b / best_nv;
			float hi = xmin + (xmax - xmin) * (b + 1) / best_nv;
			for (int i = 0; i < v_all.size(); i++) {
				if (v_all[i].xmax >= lo - SLUG_BAND_EPS && v_all[i].xmin <= hi + SLUG_BAND_EPS) {
					v_bands.write[b].push_back(v_all[i]);
				}
			}
			v_bands.write[b].sort_custom<SlugSortDescY>();
			max_curves_per_band = MAX(max_curves_per_band, v_bands[b].size());
		}

		int block_size = best_nh + best_nv;
		for (int b = 0; b < best_nh; b++) {
			block_size += h_bands[b].size();
		}
		for (int b = 0; b < best_nv; b++) {
			block_size += v_bands[b].size();
		}
		if (block_size > SLUG_TEX_WIDTH) {
			ERR_FAIL_MSG("SlugFont: glyph band block (" + itos(block_size) + " texels) exceeds the texture width in '" + path + "'.");
		}

		if (band_cx + block_size > SLUG_TEX_WIDTH) {
			band_cx = 0;
			band_cy++;
		}
		int need = (band_cy + 1) * SLUG_TEX_WIDTH;
		if (band_px.size() < need) {
			band_px.resize(need);
		}

		int block_x = band_cx, block_y = band_cy;
		int cursor = best_nh + best_nv;
		Vector<int> h_offsets, v_offsets;
		h_offsets.resize(best_nh);
		for (int b = 0; b < best_nh; b++) {
			h_offsets.write[b] = cursor;
			cursor += h_bands[b].size();
		}
		v_offsets.resize(best_nv);
		for (int b = 0; b < best_nv; b++) {
			v_offsets.write[b] = cursor;
			cursor += v_bands[b].size();
		}

		int row_base = block_y * SLUG_TEX_WIDTH + block_x;
		for (int b = 0; b < best_nh; b++) {
			band_px.write[row_base + b] = Color((float)h_bands[b].size(), (float)h_offsets[b], 0.0f, 0.0f);
		}
		for (int b = 0; b < best_nv; b++) {
			band_px.write[row_base + best_nh + b] = Color((float)v_bands[b].size(), (float)v_offsets[b], 0.0f, 0.0f);
		}
		for (int b = 0; b < best_nh; b++) {
			for (int k = 0; k < h_bands[b].size(); k++) {
				const Point2i &loc = h_bands[b][k].loc;
				band_px.write[row_base + h_offsets[b] + k] = Color((float)loc.x, (float)loc.y, 0.0f, 0.0f);
			}
		}
		for (int b = 0; b < best_nv; b++) {
			for (int k = 0; k < v_bands[b].size(); k++) {
				const Point2i &loc = v_bands[b][k].loc;
				band_px.write[row_base + v_offsets[b] + k] = Color((float)loc.x, (float)loc.y, 0.0f, 0.0f);
			}
		}
		band_cx = block_x + block_size;

		float sx = (xmax > xmin) ? (float)best_nv / (xmax - xmin) : 0.0f;
		float sy = (ymax > ymin) ? (float)best_nh / (ymax - ymin) : 0.0f;
		glyph_px.write[bi * 2 + 0] = Color(sx, sy, -xmin * sx, -ymin * sy);
		glyph_px.write[bi * 2 + 1] = Color((float)block_x, (float)block_y, (float)(best_nv - 1), (float)(best_nh - 1));
	}
	int band_rows = MAX(1, band_px.size() / SLUG_TEX_WIDTH);
	band_px.resize(band_rows * SLUG_TEX_WIDTH);

	Ref<Image> curve_img;
	curve_img.instance();
	curve_img->create(SLUG_TEX_WIDTH, curve_rows, false, Image::FORMAT_RGBAF);
	curve_img->lock();
	for (int y = 0; y < curve_rows; y++) {
		for (int x = 0; x < SLUG_TEX_WIDTH; x++) {
			curve_img->set_pixel(x, y, curve_px[y * SLUG_TEX_WIDTH + x]);
		}
	}
	curve_img->unlock();

	Ref<Image> band_img;
	band_img.instance();
	band_img->create(SLUG_TEX_WIDTH, band_rows, false, Image::FORMAT_RGF);
	band_img->lock();
	for (int y = 0; y < band_rows; y++) {
		for (int x = 0; x < SLUG_TEX_WIDTH; x++) {
			band_img->set_pixel(x, y, band_px[y * SLUG_TEX_WIDTH + x]);
		}
	}
	band_img->unlock();

	Ref<Image> glyph_img;
	glyph_img.instance();
	int glyph_rows = MAX(1, builds.size());
	glyph_img->create(2, glyph_rows, false, Image::FORMAT_RGBAF);
	glyph_img->lock();
	for (int y = 0; y < builds.size(); y++) {
		glyph_img->set_pixel(0, y, glyph_px[y * 2 + 0]);
		glyph_img->set_pixel(1, y, glyph_px[y * 2 + 1]);
	}
	glyph_img->unlock();

	curve_tex.instance();
	curve_tex->create_from_image(curve_img, 0);
	band_tex.instance();
	band_tex->create_from_image(band_img, 0);
	glyph_tex.instance();
	glyph_tex->create_from_image(glyph_img, 0);

	shader.instance();
	shader->set_code(SLUG_SHADER_CODE);

	material.instance();
	material->set_shader(shader);
	material->set_shader_param("curve_tex", curve_tex);
	material->set_shader_param("band_tex", band_tex);
	material->set_shader_param("glyph_tex", glyph_tex);

	valid = true;
	build_time_usec = (int)(OS::get_singleton()->get_ticks_usec() - start_usec);

	print_verbose("SlugFont: " + itos(glyphs.size()) + " glyphs, " + itos(curve_rows) + " curve rows, " +
			itos(band_rows) + " band rows, max " + itos(max_curves_per_band) + " curves/band, " +
			itos(build_time_usec) + " usec (" + path + ")");
}

bool SlugFont::is_valid() {
	_ensure_built();
	return valid;
}

int SlugFont::get_glyph_count() {
	_ensure_built();
	return glyphs.size();
}

int SlugFont::get_build_time_usec() {
	_ensure_built();
	return build_time_usec;
}

int SlugFont::get_max_curves_per_band() {
	_ensure_built();
	return max_curves_per_band;
}

const SlugFont::Glyph *SlugFont::get_glyph(CharType p_char) {
	_ensure_built();
	if (!valid) {
		return nullptr;
	}
	return glyphs.getptr((uint32_t)p_char);
}

float SlugFont::get_kerning(CharType p_a, CharType p_b) {
	_ensure_built();
	if (!valid) {
		return 0.0f;
	}
	uint64_t key = ((uint64_t)(uint32_t)p_a << 32) | (uint32_t)p_b;
	const float *k = kerning.getptr(key);
	return k ? *k : 0.0f;
}

float SlugFont::get_ascent() {
	_ensure_built();
	return ascent;
}

float SlugFont::get_descent() {
	_ensure_built();
	return descent;
}

float SlugFont::get_line_height() {
	_ensure_built();
	return line_height;
}

RID SlugFont::get_material_rid() {
	_ensure_built();
	return material.is_valid() ? material->get_rid() : RID();
}

SlugFont::SlugFont() {
}

SlugFont::~SlugFont() {
}
