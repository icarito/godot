/*************************************************************************/
/*  slug_shape_builder.cpp                                              */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Extracted from slug_font.cpp so any curve producer can reuse the     */
/* curve/band/glyph packing. Layout contract: see slug_shader.h.        */
/*************************************************************************/

#include "slug_shape_builder.h"

#include "scene/resources/shader.h"
#include "slug_backend.h"
#include "slug_shader.h"

// Both curve_tex and band_tex are 4096 texels wide; see slug_shader.h for
// how the fragment shader walks them. Bands overlap slightly (BAND_EPS) so a
// curve that straddles a band boundary is tested by both sides.
static const int SLUG_TEX_WIDTH = 4096;
static const int SLUG_MAX_BANDS = 16;
static const float SLUG_BAND_EPS = 1.0f / 1024.0f;

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

SlugAtlasData SlugShapeBuilder::build(const Vector<SlugShape> &p_shapes) {
	SlugAtlasData out;
	int count = p_shapes.size();

	// Pack every contour's curves into curve_tex, one contour per row span;
	// a contour never crosses a row. Keep each curve's texel location so the
	// band pass below can reference it.
	Vector<Color> curve_px;
	int curve_cx = 0, curve_cy = 0;
	Vector<Vector<SlugCurveRef> > shape_curves;
	shape_curves.resize(count);

	for (int bi = 0; bi < count; bi++) {
		const Vector<SlugContour> &contours = p_shapes[bi].contours;
		Vector<SlugCurveRef> &refs = shape_curves.write[bi];
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
			// The next texel carries the shared endpoint p3 of the last curve.
			curve_px.write[curve_cy * SLUG_TEX_WIDTH + curve_cx + n] = Color(curves[n - 1].p3.x, curves[n - 1].p3.y, 0.0f, 0.0f);
			curve_cx += n + 1;
		}
	}
	int curve_rows = MAX(1, curve_px.size() / SLUG_TEX_WIDTH);
	curve_px.resize(curve_rows * SLUG_TEX_WIDTH);

	// One band block per shape in band_tex, and the two summary texels of
	// glyph_tex; see slug_shader.h for the exact layout both encode.
	Vector<Color> band_px;
	int band_cx = 0, band_cy = 0;
	Vector<Color> glyph_px;
	glyph_px.resize(count * 2);

	int max_curves_per_band = 0;

	for (int bi = 0; bi < count; bi++) {
		const Vector<SlugCurveRef> &all_refs = shape_curves[bi];

		if (all_refs.size() == 0) {
			glyph_px.write[bi * 2 + 0] = Color(0.0f, 0.0f, 0.0f, 0.0f);
			glyph_px.write[bi * 2 + 1] = Color(0.0f, 0.0f, 0.0f, 0.0f);
			continue;
		}

		float xmin = all_refs[0].xmin, xmax = all_refs[0].xmax;
		float ymin = all_refs[0].ymin, ymax = all_refs[0].ymax;
		for (int i = 1; i < all_refs.size(); i++) {
			xmin = MIN(xmin, all_refs[i].xmin);
			xmax = MAX(xmax, all_refs[i].xmax);
			ymin = MIN(ymin, all_refs[i].ymin);
			ymax = MAX(ymax, all_refs[i].ymax);
		}

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
		ERR_FAIL_COND_V_MSG(block_size > SLUG_TEX_WIDTH, out,
				"SlugShapeBuilder: band block (" + itos(block_size) + " texels) exceeds the texture width.");

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
	int glyph_rows = MAX(1, count);
	glyph_img->create(2, glyph_rows, false, Image::FORMAT_RGBAF);
	glyph_img->lock();
	for (int y = 0; y < count; y++) {
		glyph_img->set_pixel(0, y, glyph_px[y * 2 + 0]);
		glyph_img->set_pixel(1, y, glyph_px[y * 2 + 1]);
	}
	glyph_img->unlock();

	out.curve_rows = curve_rows;
	out.band_rows = band_rows;
	out.max_curves_per_band = max_curves_per_band;

	// El shader y sus texturas float exigen un backend GLES3 (texelFetch,
	// floatBitsToUint, RG32F); con GLES2 o el dummy el consumidor cae al
	// fallback y crear los recursos solo dejaria errores en el log. Se decide
	// por capacidad real del backend, no por OS::get_current_video_driver().
	if (slug_backend_supports_float_textures()) {
		Ref<Shader> shader;
		shader.instance();
		shader->set_code(SLUG_SHADER_CODE);

		out.curve_tex.instance();
		out.curve_tex->create_from_image(curve_img, 0);
		out.band_tex.instance();
		out.band_tex->create_from_image(band_img, 0);
		out.glyph_tex.instance();
		out.glyph_tex->create_from_image(glyph_img, 0);

		out.material.instance();
		out.material->set_shader(shader);
		out.material->set_shader_param("curve_tex", out.curve_tex);
		out.material->set_shader_param("band_tex", out.band_tex);
		out.material->set_shader_param("glyph_tex", out.glyph_tex);
	}

	return out;
}
