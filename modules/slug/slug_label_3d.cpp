/*************************************************************************/
/*  slug_label_3d.cpp                                                   */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*************************************************************************/

#include "slug_label_3d.h"

#include "core/core_string_names.h"
#include "core/os/os.h"
#include "scene/resources/dynamic_font.h"
#include "servers/visual_server.h"

void SlugLabel3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_text", "text"), &SlugLabel3D::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &SlugLabel3D::get_text);

	ClassDB::bind_method(D_METHOD("set_font", "font"), &SlugLabel3D::set_font);
	ClassDB::bind_method(D_METHOD("get_font"), &SlugLabel3D::get_font);

	ClassDB::bind_method(D_METHOD("set_size", "size"), &SlugLabel3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &SlugLabel3D::get_size);

	ClassDB::bind_method(D_METHOD("set_modulate", "modulate"), &SlugLabel3D::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &SlugLabel3D::get_modulate);

	ClassDB::bind_method(D_METHOD("set_align", "align"), &SlugLabel3D::set_align);
	ClassDB::bind_method(D_METHOD("get_align"), &SlugLabel3D::get_align);

	ClassDB::bind_method(D_METHOD("set_line_spacing", "line_spacing"), &SlugLabel3D::set_line_spacing);
	ClassDB::bind_method(D_METHOD("get_line_spacing"), &SlugLabel3D::get_line_spacing);

	ClassDB::bind_method(D_METHOD("set_billboard", "billboard"), &SlugLabel3D::set_billboard);
	ClassDB::bind_method(D_METHOD("get_billboard"), &SlugLabel3D::get_billboard);

	ClassDB::bind_method(D_METHOD("set_fallback_font", "font"), &SlugLabel3D::set_fallback_font);
	ClassDB::bind_method(D_METHOD("get_fallback_font"), &SlugLabel3D::get_fallback_font);

	ClassDB::bind_method(D_METHOD("_update_mesh"), &SlugLabel3D::_update_mesh);
	ClassDB::bind_method(D_METHOD("_font_changed"), &SlugLabel3D::_font_changed);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT, ""), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "font", PROPERTY_HINT_RESOURCE_TYPE, "SlugFont"), "set_font", "get_font");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "size", PROPERTY_HINT_RANGE, "0.001,1000,0.001"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "align", PROPERTY_HINT_ENUM, "Left,Center,Right"), "set_align", "get_align");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "line_spacing"), "set_line_spacing", "get_line_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "billboard"), "set_billboard", "get_billboard");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "fallback_font", PROPERTY_HINT_RESOURCE_TYPE, "Font"), "set_fallback_font", "get_fallback_font");

	BIND_ENUM_CONSTANT(ALIGN_LEFT);
	BIND_ENUM_CONSTANT(ALIGN_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_RIGHT);
}

void SlugLabel3D::_queue_update() {
	if (pending_update) {
		return;
	}
	pending_update = true;
	call_deferred("_update_mesh");
}

void SlugLabel3D::_font_changed() {
	_queue_update();
}

void SlugLabel3D::set_text(const String &p_text) {
	if (text != p_text) {
		text = p_text;
		_queue_update();
	}
}

String SlugLabel3D::get_text() const {
	return text;
}

void SlugLabel3D::set_font(const Ref<SlugFont> &p_font) {
	if (font == p_font) {
		return;
	}
	if (font.is_valid()) {
		font->disconnect(CoreStringNames::get_singleton()->changed, this, "_font_changed");
	}
	font = p_font;
	if (font.is_valid()) {
		font->connect(CoreStringNames::get_singleton()->changed, this, "_font_changed");
	}
	_queue_update();
}

Ref<SlugFont> SlugLabel3D::get_font() const {
	return font;
}

void SlugLabel3D::set_size(float p_size) {
	ERR_FAIL_COND(p_size <= 0.0f);
	if (size != p_size) {
		size = p_size;
		_queue_update();
	}
}

float SlugLabel3D::get_size() const {
	return size;
}

void SlugLabel3D::set_modulate(const Color &p_color) {
	if (modulate != p_color) {
		modulate = p_color;
		_queue_update();
	}
}

Color SlugLabel3D::get_modulate() const {
	return modulate;
}

void SlugLabel3D::set_align(Align p_align) {
	ERR_FAIL_INDEX(p_align, 3);
	if (align != p_align) {
		align = p_align;
		_queue_update();
	}
}

SlugLabel3D::Align SlugLabel3D::get_align() const {
	return align;
}

void SlugLabel3D::set_line_spacing(float p_spacing) {
	if (line_spacing != p_spacing) {
		line_spacing = p_spacing;
		_queue_update();
	}
}

float SlugLabel3D::get_line_spacing() const {
	return line_spacing;
}

void SlugLabel3D::set_billboard(bool p_billboard) {
	if (billboard != p_billboard) {
		billboard = p_billboard;
		_queue_update();
	}
}

bool SlugLabel3D::get_billboard() const {
	return billboard;
}

void SlugLabel3D::set_fallback_font(const Ref<Font> &p_font) {
	if (fallback_font == p_font) {
		return;
	}
	fallback_font = p_font;
	_queue_update();
}

Ref<Font> SlugLabel3D::get_fallback_font() const {
	return fallback_font;
}

AABB SlugLabel3D::get_aabb() const {
	return aabb;
}

PoolVector<Face3> SlugLabel3D::get_faces(uint32_t p_usage_flags) const {
	return PoolVector<Face3>();
}

void SlugLabel3D::_update_fallback_label() {
	if (!fallback_label) {
		fallback_label = memnew(Label3D);
		fallback_label->set_name("_SlugFallback");
		add_child(fallback_label);
	}

	fallback_label->set_text(text);
	fallback_label->set_modulate(modulate);
	fallback_label->set_horizontal_alignment((Label3D::Align)(int)align);
	fallback_label->set_line_spacing(line_spacing);
	fallback_label->set_billboard_mode(billboard ? Material3D::BILLBOARD_ENABLED : Material3D::BILLBOARD_DISABLED);
	fallback_label->set_draw_flag(Label3D::FLAG_DOUBLE_SIDED, true);
	fallback_label->set_font(fallback_font);

	// One em of the Label3D must equal `size` world units. A DynamicFont's
	// size is already the em in pixels; a generic Font only exposes its
	// pixel height, which is close enough for the non-dynamic fallback case.
	DynamicFont *dynamic_font = Object::cast_to<DynamicFont>(fallback_font.ptr());
	float em_px = dynamic_font ? (float)dynamic_font->get_size() : fallback_font->get_height();
	fallback_label->set_pixel_size(em_px > 0.0f ? size / em_px : size);
}

void SlugLabel3D::_free_fallback_label() {
	if (!fallback_label) {
		return;
	}
	remove_child(fallback_label);
	memdelete(fallback_label);
	fallback_label = nullptr;
}

void SlugLabel3D::_update_mesh() {
	pending_update = false;

	bool gles2 = OS::get_singleton()->get_current_video_driver() == OS::VIDEO_DRIVER_GLES2;
	bool font_ok = font.is_valid() && font->is_valid();

	if (gles2 || !font_ok) {
		VS::get_singleton()->mesh_clear(mesh);
		aabb = AABB();
		VS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);

		if (fallback_font.is_valid()) {
			WARN_PRINT_ONCE(gles2 ? "SlugLabel3D: GLES2 can't draw Slug text; using fallback_font (Label3D) instead."
								  : "SlugLabel3D: font is null or invalid; using fallback_font (Label3D) instead.");
			_update_fallback_label();
			update_gizmo();
			return;
		}

		_free_fallback_label();
		WARN_PRINT_ONCE(gles2 ? "SlugLabel3D requires GLES3; nothing will be drawn."
							  : "SlugLabel3D: font is null or invalid; nothing will be drawn.");
		update_gizmo();
		return;
	}

	_free_fallback_label();

	VS::get_singleton()->mesh_clear(mesh);
	aabb = AABB();

	if (text.empty()) {
		VS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);
		update_gizmo();
		return;
	}

	Vector<String> lines = text.split("\n");

	// First pass: resolve glyphs and lay out each line's pen advance, so the
	// alignment offset for the second pass is already known.
	Vector<Vector<const SlugFont::Glyph *> > line_glyphs;
	Vector<float> line_widths;
	line_glyphs.resize(lines.size());
	line_widths.resize(lines.size());

	for (int li = 0; li < lines.size(); li++) {
		const String &line = lines[li];
		Vector<const SlugFont::Glyph *> &glyphs = line_glyphs.write[li];
		float x = 0.0f;
		CharType prev = 0;
		for (int i = 0; i < line.length(); i++) {
			CharType c = line[i];
			const SlugFont::Glyph *g = font->get_glyph(c);
			if (!g) {
				g = font->get_glyph(0xFFFD);
				if (!g) {
					g = font->get_glyph('?');
				}
				WARN_PRINT_ONCE("SlugLabel3D: text has a codepoint missing from the font; using a fallback glyph.");
			}
			if (i > 0 && g) {
				x += font->get_kerning(prev, c);
			}
			if (g) {
				x += g->advance;
			}
			glyphs.push_back(g);
			prev = c;
		}
		line_widths.write[li] = x;
	}

	float ascent = font->get_ascent();
	float line_height = font->get_line_height();
	float block_height = lines.size() * line_height + MAX(0, lines.size() - 1) * line_spacing;
	float y = block_height / 2.0f - ascent;

	PoolVector3Array vertices;
	PoolVector3Array normals;
	PoolVector2Array uvs;
	PoolVector2Array uv2s;
	PoolColorArray colors;
	PoolIntArray indices;

	Color linear_modulate = modulate.to_linear();
	float billboard_flag = billboard ? 1.0f : 0.0f;
	bool has_point = false;

	for (int li = 0; li < lines.size(); li++) {
		float x = 0.0f;
		switch (align) {
			case ALIGN_LEFT:
				break;
			case ALIGN_CENTER:
				x = -line_widths[li] / 2.0f;
				break;
			case ALIGN_RIGHT:
				x = -line_widths[li];
				break;
		}

		const String &line = lines[li];
		const Vector<const SlugFont::Glyph *> &glyphs = line_glyphs[li];
		CharType prev = 0;
		for (int i = 0; i < line.length(); i++) {
			CharType c = line[i];
			const SlugFont::Glyph *g = glyphs[i];
			if (g && i > 0) {
				x += font->get_kerning(prev, c);
			}
			prev = c;
			if (!g) {
				continue;
			}
			if (g->has_outline) {
				Vector2 corners[4] = {
					Vector2(g->bounds.position.x, g->bounds.position.y),
					Vector2(g->bounds.position.x + g->bounds.size.x, g->bounds.position.y),
					Vector2(g->bounds.position.x + g->bounds.size.x, g->bounds.position.y + g->bounds.size.y),
					Vector2(g->bounds.position.x, g->bounds.position.y + g->bounds.size.y),
				};
				Vector2 normal_dirs[4] = {
					Vector2(-1, -1),
					Vector2(1, -1),
					Vector2(1, 1),
					Vector2(-1, 1),
				};

				int base = vertices.size();
				for (int ci = 0; ci < 4; ci++) {
					Vector3 v((x + corners[ci].x) * size, (y + corners[ci].y) * size, 0.0f);
					vertices.push_back(v);
					normals.push_back(Vector3(normal_dirs[ci].x, normal_dirs[ci].y, billboard_flag));
					uvs.push_back(corners[ci]);
					uv2s.push_back(Vector2((float)g->index, 1.0f / size));
					colors.push_back(linear_modulate);

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
			x += g->advance;
		}

		y -= line_height + line_spacing;
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
	arrays[VS::ARRAY_INDEX] = indices;

	VS::get_singleton()->mesh_add_surface_from_arrays(mesh, VS::PRIMITIVE_TRIANGLES, arrays, Array(), 0);
	VS::get_singleton()->mesh_surface_set_material(mesh, 0, font->get_material_rid());
	VS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);

	update_gizmo();
}

SlugLabel3D::SlugLabel3D() {
	mesh = VisualServer::get_singleton()->mesh_create();
	set_base(mesh);
}

SlugLabel3D::~SlugLabel3D() {
	if (font.is_valid()) {
		font->disconnect(CoreStringNames::get_singleton()->changed, this, "_font_changed");
	}
	VS::get_singleton()->free(mesh);
}
