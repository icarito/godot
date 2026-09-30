/*************************************************************************/
/*  slug_vector_2d.h                                                    */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Draws a SlugVector (an SVG) as flat 2D UI with the Slug canvas       */
/* shader. Analogous to SlugVector3D, one quad per shape.               */
/*************************************************************************/

#ifndef SLUG_VECTOR_2D_H
#define SLUG_VECTOR_2D_H

#include "scene/2d/node_2d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "slug_vector.h"

class SlugVector2D : public Node2D {
	GDCLASS(SlugVector2D, Node2D);

private:
	Ref<SlugVector> vector;
	float size = 64.0f;
	Color tint = Color(1, 1, 1, 1);
	bool centered = true;

	Ref<ShaderMaterial> material;
	Ref<Mesh> mesh;
	bool pending_update = false;

	void _queue_update();
	void _update_material();
	void _update_mesh();
	void _vector_changed();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_vector(const Ref<SlugVector> &p_vector);
	Ref<SlugVector> get_vector() const;

	void set_size(float p_size);
	float get_size() const;

	void set_tint(const Color &p_color);
	Color get_tint() const;

	void set_centered(bool p_centered);
	bool get_centered() const;

	SlugVector2D();
	~SlugVector2D();
};

#endif // SLUG_VECTOR_2D_H
