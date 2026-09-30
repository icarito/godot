/*************************************************************************/
/*  slug_vector_3d.cpp                                                  */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*************************************************************************/

#include "slug_vector_3d.h"

#include "core/class_db.h"
#include "core/core_string_names.h"
#include "servers/visual_server.h"
#include "slug_backend.h"

void SlugVector3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_vector", "vector"), &SlugVector3D::set_vector);
	ClassDB::bind_method(D_METHOD("get_vector"), &SlugVector3D::get_vector);

	ClassDB::bind_method(D_METHOD("set_size", "size"), &SlugVector3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &SlugVector3D::get_size);

	ClassDB::bind_method(D_METHOD("set_modulate", "modulate"), &SlugVector3D::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &SlugVector3D::get_modulate);

	ClassDB::bind_method(D_METHOD("set_billboard", "billboard"), &SlugVector3D::set_billboard);
	ClassDB::bind_method(D_METHOD("get_billboard"), &SlugVector3D::get_billboard);

	ClassDB::bind_method(D_METHOD("_update_mesh"), &SlugVector3D::_update_mesh);
	ClassDB::bind_method(D_METHOD("_vector_changed"), &SlugVector3D::_vector_changed);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "vector", PROPERTY_HINT_RESOURCE_TYPE, "SlugVector"), "set_vector", "get_vector");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "size", PROPERTY_HINT_RANGE, "0.001,1000,0.001"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "billboard"), "set_billboard", "get_billboard");
}

void SlugVector3D::_queue_update() {
	if (pending_update) {
		return;
	}
	pending_update = true;
	call_deferred("_update_mesh");
}

void SlugVector3D::_vector_changed() {
	_queue_update();
}

void SlugVector3D::set_vector(const Ref<SlugVector> &p_vector) {
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

Ref<SlugVector> SlugVector3D::get_vector() const {
	return vector;
}

void SlugVector3D::set_size(float p_size) {
	ERR_FAIL_COND(p_size <= 0.0f);
	if (size != p_size) {
		size = p_size;
		_queue_update();
	}
}

float SlugVector3D::get_size() const {
	return size;
}

void SlugVector3D::set_modulate(const Color &p_color) {
	if (modulate != p_color) {
		modulate = p_color;
		_queue_update();
	}
}

Color SlugVector3D::get_modulate() const {
	return modulate;
}

void SlugVector3D::set_billboard(bool p_billboard) {
	if (billboard != p_billboard) {
		billboard = p_billboard;
		_queue_update();
	}
}

bool SlugVector3D::get_billboard() const {
	return billboard;
}

AABB SlugVector3D::get_aabb() const {
	return aabb;
}

PoolVector<Face3> SlugVector3D::get_faces(uint32_t p_usage_flags) const {
	return PoolVector<Face3>();
}

void SlugVector3D::_update_mesh() {
	pending_update = false;

	VS::get_singleton()->mesh_clear(mesh);
	aabb = AABB();

	if (!slug_backend_supports_float_textures() || !vector.is_valid() || !vector->is_valid()) {
		VS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);
		update_gizmo();
		return;
	}

	const Vector<SlugShape> &shapes = vector->get_shapes();
	Rect2 bounds = vector->get_bounds();
	float height = bounds.size.y;
	float scale = height > 0.0f ? size / height : size;
	Vector2 center = bounds.position + bounds.size * 0.5f;

	PoolVector3Array vertices;
	PoolVector3Array normals;
	PoolVector2Array uvs;
	PoolVector2Array uv2s;
	PoolColorArray colors;
	PoolRealArray tangents;
	PoolIntArray indices;

	Color linear_modulate = modulate.to_linear();
	float billboard_flag = billboard ? 1.0f : 0.0f;
	bool has_point = false;

	Vector2 normal_dirs[4] = {
		Vector2(-1, -1),
		Vector2(1, -1),
		Vector2(1, 1),
		Vector2(-1, 1),
	};

	for (int si = 0; si < shapes.size(); si++) {
		Rect2 sb = shapes[si].compute_bounds();
		if (sb.size.x <= 0.0f || sb.size.y <= 0.0f) {
			continue;
		}
		Vector2 corners[4] = {
			Vector2(sb.position.x, sb.position.y),
			Vector2(sb.position.x + sb.size.x, sb.position.y),
			Vector2(sb.position.x + sb.size.x, sb.position.y + sb.size.y),
			Vector2(sb.position.x, sb.position.y + sb.size.y),
		};
		Color shape_color = vector->get_shape_color(si) * linear_modulate;

		int base = vertices.size();
		for (int ci = 0; ci < 4; ci++) {
			Vector2 local = (corners[ci] - center) * scale;
			Vector3 v(local.x, local.y, 0.0f);
			vertices.push_back(v);
			normals.push_back(Vector3(normal_dirs[ci].x, normal_dirs[ci].y, billboard_flag));
			uvs.push_back(corners[ci]);
			uv2s.push_back(Vector2((float)si, 1.0f / scale));
			colors.push_back(shape_color);
			tangents.push_back(0.0f);
			tangents.push_back(0.0f);
			tangents.push_back(0.0f);
			tangents.push_back(1.0f);

			if (!has_point) {
				aabb.position = v;
				has_point = true;
			} else {
				aabb.expand_to(v);
			}
		}
		indices.push_back(base + 0);
		indices.push_back(base + 1);
		indices.push_back(base + 2);
		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 3);
	}

	if (vertices.size() == 0) {
		VS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);
		update_gizmo();
		return;
	}

	aabb = aabb.grow(size * 0.05f);
	if (billboard) {
		float r = 0.0f;
		PoolVector3Array::Read vr = vertices.read();
		for (int i = 0; i < vertices.size(); i++) {
			r = MAX(r, vr[i].length());
		}
		aabb = AABB(Vector3(-r, -r, -r), Vector3(2.0f * r, 2.0f * r, 2.0f * r));
	}

	Array arrays;
	arrays.resize(VS::ARRAY_MAX);
	arrays[VS::ARRAY_VERTEX] = vertices;
	arrays[VS::ARRAY_NORMAL] = normals;
	arrays[VS::ARRAY_COLOR] = colors;
	arrays[VS::ARRAY_TEX_UV] = uvs;
	arrays[VS::ARRAY_TEX_UV2] = uv2s;
	arrays[VS::ARRAY_TANGENT] = tangents;
	arrays[VS::ARRAY_INDEX] = indices;

	VS::get_singleton()->mesh_add_surface_from_arrays(mesh, VS::PRIMITIVE_TRIANGLES, arrays, Array(), 0);
	VS::get_singleton()->mesh_surface_set_material(mesh, 0, vector->get_material_rid());
	VS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);

	update_gizmo();
}

SlugVector3D::SlugVector3D() {
	mesh = VisualServer::get_singleton()->mesh_create();
	set_base(mesh);
}

SlugVector3D::~SlugVector3D() {
	if (vector.is_valid()) {
		vector->disconnect(CoreStringNames::get_singleton()->changed, this, "_vector_changed");
	}
	VS::get_singleton()->free(mesh);
}
