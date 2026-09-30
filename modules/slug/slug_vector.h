/*************************************************************************/
/*  slug_vector.h                                                       */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* A set of filled regions parsed from an SVG, packed into the Slug     */
/* atlas textures by SlugShapeBuilder. Consumed by SlugVector3D.        */
/* Sugar-style fill/stroke color roles can be recolored at runtime.     */
/*************************************************************************/

#ifndef SLUG_VECTOR_H
#define SLUG_VECTOR_H

#include "core/color.h"
#include "core/math/rect2.h"
#include "core/resource.h"
#include "slug_shape_builder.h"
#include "slug_svg.h"

class SlugVector : public Resource {
	GDCLASS(SlugVector, Resource);

	String svg_path;

	bool built = false;
	bool valid = false;
	int build_time_usec = 0;

	SlugSvgData svg;
	Color fill_color = Color(1, 1, 1, 1);
	Color stroke_color = Color(0, 0, 0, 1);
	bool fill_custom = false;
	bool stroke_custom = false;
	SlugAtlasData atlas;
	Ref<ImageTexture> paint_tex;
	Ref<ImageTexture> gradient_tex;

	void _ensure_built();
	void _clear_built();
	void _build_paint_texture();
	void _update_gradient_texture();
	Color _gradient_sample(int p_gradient, float p_t) const;

protected:
	static void _bind_methods();

public:
	void set_svg_path(const String &p_path);
	String get_svg_path() const;

	void set_fill_color(const Color &p_color);
	Color get_fill_color() const;
	void set_stroke_color(const Color &p_color);
	Color get_stroke_color() const;

	bool is_valid();
	int get_shape_count();
	int get_build_time_usec();
	int get_max_curves_per_band();
	Rect2 get_bounds();
	RID get_material_rid();

	// Internal API used by SlugVector3D; not bound to script.
	const Vector<SlugShape> &get_shapes();
	Color get_shape_color(int p_index) const;

	SlugVector();
	~SlugVector();
};

#endif // SLUG_VECTOR_H
