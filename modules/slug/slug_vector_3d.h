/*************************************************************************/
/*  slug_vector_3d.h                                                    */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Draws a SlugVector (an SVG) as resolution-independent geometry with  */
/* the Slug fragment shader. Analogous to SlugLabel3D, one quad per     */
/* shape.                                                               */
/*************************************************************************/

#ifndef SLUG_VECTOR_3D_H
#define SLUG_VECTOR_3D_H

#include "scene/3d/visual_instance.h"
#include "slug_vector.h"

class SlugVector3D : public GeometryInstance {
	GDCLASS(SlugVector3D, GeometryInstance);

private:
	Ref<SlugVector> vector;
	float size = 1.0f;
	Color modulate = Color(1, 1, 1, 1);
	bool billboard = false;

	RID mesh;
	AABB aabb;
	bool pending_update = false;

	void _queue_update();
	void _update_mesh();
	void _vector_changed();

protected:
	static void _bind_methods();

public:
	void set_vector(const Ref<SlugVector> &p_vector);
	Ref<SlugVector> get_vector() const;

	void set_size(float p_size);
	float get_size() const;

	void set_modulate(const Color &p_color);
	Color get_modulate() const;

	void set_billboard(bool p_billboard);
	bool get_billboard() const;

	virtual AABB get_aabb() const;
	virtual PoolVector<Face3> get_faces(uint32_t p_usage_flags) const;

	SlugVector3D();
	~SlugVector3D();
};

#endif // SLUG_VECTOR_3D_H
