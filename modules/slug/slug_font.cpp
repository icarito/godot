/*************************************************************************/
/*  slug_font.cpp                                                       */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* FreeType outlines -> SlugShapes -> SlugShapeBuilder. Cubic (CFF/OTF) */
/* outlines are split into quadratics by SlugCurveDecomposer.           */
/*************************************************************************/

#include "slug_font.h"

#include "core/os/file_access.h"
#include "core/os/os.h"
#include "core/print_string.h"
#include "slug_decomposer.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

struct SlugDecomposeCtx {
	SlugCurveDecomposer *decomposer = nullptr;
	float inv_upem = 1.0f;
};

static int _slug_move_to(const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	ctx->decomposer->move_to(Vector2(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem));
	return 0;
}

static int _slug_line_to(const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	ctx->decomposer->line_to(Vector2(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem));
	return 0;
}

static int _slug_conic_to(const FT_Vector *p_control, const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	Vector2 ctrl(p_control->x * ctx->inv_upem, p_control->y * ctx->inv_upem);
	Vector2 p(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem);
	ctx->decomposer->quad_to(ctrl, p);
	return 0;
}

static int _slug_cubic_to(const FT_Vector *p_c1, const FT_Vector *p_c2, const FT_Vector *p_to, void *p_user) {
	SlugDecomposeCtx *ctx = (SlugDecomposeCtx *)p_user;
	Vector2 c1(p_c1->x * ctx->inv_upem, p_c1->y * ctx->inv_upem);
	Vector2 c2(p_c2->x * ctx->inv_upem, p_c2->y * ctx->inv_upem);
	Vector2 p(p_to->x * ctx->inv_upem, p_to->y * ctx->inv_upem);
	ctx->decomposer->cubic_to(c1, c2, p);
	return 0;
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
	ascent = 0.0f;
	descent = 0.0f;
	line_height = 0.0f;
	glyphs.clear();
	kerning.clear();
	atlas = SlugAtlasData();
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

	FT_Outline_Funcs funcs;
	funcs.move_to = _slug_move_to;
	funcs.line_to = _slug_line_to;
	funcs.conic_to = _slug_conic_to;
	funcs.cubic_to = _slug_cubic_to;
	funcs.shift = 0;
	funcs.delta = 0;

	Vector<SlugShape> shapes; // Row index in glyph_tex == index here.
	Vector<uint32_t> shape_codepoints;
	Vector<FT_UInt> shape_glyph_indices;
	Vector<float> shape_advances;

	for (int i = 0; i < codepoints.size(); i++) {
		FT_UInt gi = FT_Get_Char_Index(face, codepoints[i]);
		if (gi == 0) {
			continue;
		}
		if (FT_Load_Glyph(face, gi, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
			continue;
		}

		SlugCurveDecomposer decomposer;
		if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_contours > 0) {
			SlugDecomposeCtx ctx;
			ctx.decomposer = &decomposer;
			ctx.inv_upem = inv_upem;
			FT_Outline_Decompose(&face->glyph->outline, &funcs, &ctx);
		}

		shapes.push_back(decomposer.get_shape());
		shape_codepoints.push_back(codepoints[i]);
		shape_glyph_indices.push_back(gi);
		shape_advances.push_back(face->glyph->advance.x * inv_upem);
	}

	if (FT_HAS_KERNING(face)) {
		for (int i = 0; i < shape_glyph_indices.size(); i++) {
			for (int j = 0; j < shape_glyph_indices.size(); j++) {
				FT_Vector k;
				if (FT_Get_Kerning(face, shape_glyph_indices[i], shape_glyph_indices[j], FT_KERNING_UNSCALED, &k) == 0 && k.x != 0) {
					uint64_t key = ((uint64_t)shape_codepoints[i] << 32) | shape_codepoints[j];
					kerning[key] = k.x * inv_upem;
				}
			}
		}
	}

	FT_Done_Face(face);
	FT_Done_FreeType(library);

	atlas = SlugShapeBuilder::build(shapes);

	for (int i = 0; i < shapes.size(); i++) {
		Glyph glyph;
		glyph.index = i;
		glyph.advance = shape_advances[i];
		glyph.bounds = shapes[i].compute_bounds();
		glyph.has_outline = shapes[i].has_curves();
		glyphs[shape_codepoints[i]] = glyph;
	}

	valid = true;
	build_time_usec = (int)(OS::get_singleton()->get_ticks_usec() - start_usec);

	print_verbose("SlugFont: " + itos(glyphs.size()) + " glyphs, " + itos(atlas.curve_rows) + " curve rows, " +
			itos(atlas.band_rows) + " band rows, max " + itos(atlas.max_curves_per_band) + " curves/band, " +
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
	return atlas.max_curves_per_band;
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
	return atlas.material.is_valid() ? atlas.material->get_rid() : RID();
}

SlugFont::SlugFont() {
}

SlugFont::~SlugFont() {
}
