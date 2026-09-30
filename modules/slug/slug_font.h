/*************************************************************************/
/*  slug_font.h                                                         */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Turns a DynamicFontData's outlines into SlugShapes and hands them to */
/* SlugShapeBuilder. The GPU resources and packing live in the builder  */
/* so other producers (SVG paths) can share them.                       */
/*************************************************************************/

#ifndef SLUG_FONT_H
#define SLUG_FONT_H

#include "core/hash_map.h"
#include "scene/resources/dynamic_font.h"
#include "slug_shape_builder.h"

class SlugFont : public Resource {
	GDCLASS(SlugFont, Resource);

public:
	// Everything is in em (font units / upem).
	struct Glyph {
		int index = 0;
		float advance = 0.0f;
		Rect2 bounds;
		bool has_outline = false;
	};

private:
	Ref<DynamicFontData> font_data;

	bool built = false;
	bool valid = false;
	int build_time_usec = 0;

	float ascent = 0.0f;
	float descent = 0.0f;
	float line_height = 0.0f;

	HashMap<uint32_t, Glyph> glyphs; // Codepoint -> glyph.
	HashMap<uint64_t, float> kerning; // (a << 32 | b) -> kerning in em.

	SlugAtlasData atlas;

	void _ensure_built();
	void _clear_built();

protected:
	static void _bind_methods();

public:
	void set_font_data(const Ref<DynamicFontData> &p_font_data);
	Ref<DynamicFontData> get_font_data() const;

	bool is_valid();
	int get_glyph_count();
	int get_build_time_usec();
	int get_max_curves_per_band();

	// Internal API used by SlugLabel3D; not bound to script.
	const Glyph *get_glyph(CharType p_char);
	float get_kerning(CharType p_a, CharType p_b);
	float get_ascent();
	float get_descent();
	float get_line_height();
	RID get_material_rid();

	SlugFont();
	~SlugFont();
};

#endif // SLUG_FONT_H
