/*************************************************************************/
/*  slug_vector.h                                                       */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* A set of filled regions parsed from an SVG, packed into the Slug     */
/* atlas textures by SlugShapeBuilder. Consumed by SlugVector3D.        */
/*************************************************************************/

#ifndef SLUG_VECTOR_H
#define SLUG_VECTOR_H

#include "core/color.h"
#include "core/math/rect2.h"
#include "core/resource.h"
#include "slug_shape_builder.h"

class SlugVector : public Resource {
	GDCLASS(SlugVector, Resource);

	String svg_path;

	bool built = false;
	bool valid = false;
	int build_time_usec = 0;
	Rect2 bounds;

	Vector<SlugShape> shapes;
	Vector<Color> colors;
	SlugAtlasData atlas;

	void _ensure_built();
	void _clear_built();

protected:
	static void _bind_methods();

public:
	void set_svg_path(const String &p_path);
	String get_svg_path() const;

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
