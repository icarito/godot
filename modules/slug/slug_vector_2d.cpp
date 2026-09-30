/*************************************************************************/
/*  slug_vector_2d.cpp                                                  */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*                                                                      */
/* Canvas shaders have no UV2, so the shape index is packed into the    */
/* mesh's vertex color red channel (index / 255) and the fragment       */
/* fetches the per-shape texels from paint_tex; the quad is grown by    */
/* one pixel of shape units on the CPU, which replaces the perspective  */
/* dilation of the 3D shader.                                           */
/*************************************************************************/

#include "slug_vector_2d.h"

#include "core/class_db.h"
#include "core/core_string_names.h"
#include "servers/visual_server.h"
#include "slug_backend.h"
#include "slug_shader.h"

void SlugVector2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_vector", "vector"), &SlugVector2D::set_vector);
	ClassDB::bind_method(D_METHOD("get_vector"), &SlugVector2D::get_vector);

	ClassDB::bind_method(D_METHOD("set_size", "size"), &SlugVector2D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &SlugVector2D::get_size);

	ClassDB::bind_method(D_METHOD("set_tint", "color"), &SlugVector2D::set_tint);
	ClassDB::bind_method(D_METHOD("get_tint"), &SlugVector2D::get_tint);

	ClassDB::bind_method(D_METHOD("set_centered", "centered"), &SlugVector2D::set_centered);
	ClassDB::bind_method(D_METHOD("get_centered"), &SlugVector2D::get_centered);

	ClassDB::bind_method(D_METHOD("_update_mesh"), &SlugVector2D::_update_mesh);
	ClassDB::bind_method(D_METHOD("_vector_changed"), &SlugVector2D::_vector_changed);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "vector", PROPERTY_HINT_RESOURCE_TYPE, "SlugVector"), "set_vector", "get_vector");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "size", PROPERTY_HINT_RANGE, "0.001,4096,0.001"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "centered"), "set_centered", "get_centered");
}

void SlugVector2D::_queue_update() {
	if (pending_update) {
		return;
	}
	pending_update = true;
	call_deferred("_update_mesh");
}

void SlugVector2D::_vector_changed() {
	_queue_update();
}

void SlugVector2D::set_vector(const Ref<SlugVector> &p_vector) {
	if (vector == p_vector) {
		return;
	}
	if (vector.is_valid()) {
		vector->disconnect(CoreStringNames::get_singleton()->changed, this, "_vector_changed");
	}
	vector = p_vector;
	if (vector.is_valid()) {
		vector->connect(CoreStringNames::get_singleton()->changed, this, "_vector_changed");
	}
	_queue_update();
}

Ref<SlugVector> SlugVector2D::get_vector() const {
	return vector;
}

void SlugVector2D::set_size(float p_size) {
	ERR_FAIL_COND(p_size <= 0.0f);
	if (size != p_size) {
		size = p_size;
		_queue_update();
	}
}

float SlugVector2D::get_size() const {
	return size;
}

void SlugVector2D::set_tint(const Color &p_color) {
	if (tint != p_color) {
		tint = p_color;
		if (material.is_valid()) {
			material->set_shader_param("shape_modulate", tint * get_modulate());
		}
		update();
	}
}

Color SlugVector2D::get_tint() const {
	return tint;
}

void SlugVector2D::set_centered(bool p_centered) {
	if (centered != p_centered) {
		centered = p_centered;
		_queue_update();
	}
}

bool SlugVector2D::get_centered() const {
	return centered;
}

void SlugVector2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		if (material.is_valid()) {
			// CanvasItem.modulate is inherited and does not force a redraw, so
			// the explicit tint carries most of the use; refresh both on draw.
			material->set_shader_param("shape_modulate", tint * get_modulate());
		}
		if (mesh.is_valid() && slug_backend_supports_float_textures()) {
			draw_mesh(mesh, Ref<Texture>(), Ref<Texture>());
		}
	}
}

void SlugVector2D::_update_material() {
	if (!material.is_valid()) {
		Ref<Shader> shader;
		shader.instance();
		shader->set_code(slug_canvas_shader_code());
		material.instance();
		material->set_shader(shader);
		set_material(material);
	}
	material->set_shader_param("shape_modulate", get_modulate());

	if (!vector.is_valid() || !vector->is_valid()) {
		return;
	}
	material->set_shader_param("curve_tex", vector->get_curve_texture());
	material->set_shader_param("band_tex", vector->get_band_texture());
	material->set_shader_param("glyph_tex", vector->get_glyph_texture());
	material->set_shader_param("paint_tex", vector->get_paint_texture());
	material->set_shader_param("gradient_tex", vector->get_gradient_texture());
	material->set_shader_param("gradient_rows_inv", vector->get_gradient_rows_inv());
}

void SlugVector2D::_update_mesh() {
	pending_update = false;

	_update_material();

	mesh.unref();

	if (!slug_backend_supports_float_textures() || !vector.is_valid() || !vector->is_valid()) {
		update();
		return;
	}

	const Vector<SlugShape> &shapes = vector->get_shapes();
	Rect2 bounds = vector->get_bounds();
	float height = bounds.size.y;
	float scale = height > 0.0f ? size / height : size;
	if (scale <= 0.0f) {
		update();
		return;
	}
	Vector2 origin = centered ? bounds.position + bounds.size * 0.5f : bounds.position;
	float margin = 1.0f / scale; // One pixel in shape units, replaces dilation.

	PoolVector3Array vertices;
	PoolVector2Array uvs;
	PoolColorArray colors;
	PoolIntArray indices;

	for (int si = 0; si < shapes.size(); si++) {
		Rect2 sb = shapes[si].compute_bounds().grow(margin);
		if (sb.size.x <= 0.0f || sb.size.y <= 0.0f) {
			continue;
		}
		Vector2 corners[4] = {
			Vector2(sb.position.x, sb.position.y),
			Vector2(sb.position.x + sb.size.x, sb.position.y),
			Vector2(sb.position.x + sb.size.x, sb.position.y + sb.size.y),
			Vector2(sb.position.x, sb.position.y + sb.size.y),
		};
		// index / 255 fits the vertex color; up to 255 shapes.
		Color index_color = Color((float)si / 255.0f, 0.0f, 0.0f, 0.0f);

		int base = vertices.size();
		for (int ci = 0; ci < 4; ci++) {
			Vector2 local = (corners[ci] - origin) * scale;
			vertices.push_back(Vector3(local.x, -local.y, 0.0f)); // y-down
			uvs.push_back(corners[ci]);
			colors.push_back(index_color);
		}
		indices.push_back(base + 0);
		indices.push_back(base + 1);
		indices.push_back(base + 2);
		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 3);
	}

	if (vertices.size() == 0) {
		update();
		return;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> am;
	am.instance();
	am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), 0);
	mesh = am;

	update();
}

SlugVector2D::SlugVector2D() {
	set_material(material);
}

SlugVector2D::~SlugVector2D() {
	if (vector.is_valid()) {
		vector->disconnect(CoreStringNames::get_singleton()->changed, this, "_vector_changed");
	}
}
