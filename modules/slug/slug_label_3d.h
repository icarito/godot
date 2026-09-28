/*************************************************************************/
/*  slug_label_3d.h                                                     */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*************************************************************************/

#ifndef SLUG_LABEL_3D_H
#define SLUG_LABEL_3D_H

#include "scene/3d/label_3d.h"
#include "scene/3d/visual_instance.h"
#include "slug_font.h"

class SlugLabel3D : public GeometryInstance {
	GDCLASS(SlugLabel3D, GeometryInstance);

public:
	enum Align {
		ALIGN_LEFT,
		ALIGN_CENTER,
		ALIGN_RIGHT,
	};

private:
	String text;
	Ref<SlugFont> font;
	float size = 1.0f;
	Color modulate = Color(1, 1, 1, 1);
	float outline_size = 0.0f;
	Color outline_modulate = Color(0, 0, 0, 1);
	Align align = ALIGN_CENTER;
	float line_spacing = 0.0f;
	bool billboard = false;
	Ref<Font> fallback_font;

	RID mesh;
	AABB aabb;
	bool pending_update = false;
	Label3D *fallback_label = nullptr;

	void _queue_update();
	void _update_mesh();
	void _font_changed();
	void _update_fallback_label();
	void _free_fallback_label();

protected:
	static void _bind_methods();

public:
	void set_text(const String &p_text);
	String get_text() const;

	void set_font(const Ref<SlugFont> &p_font);
	Ref<SlugFont> get_font() const;

	void set_size(float p_size);
	float get_size() const;

	void set_modulate(const Color &p_color);
	Color get_modulate() const;

	void set_outline_size(float p_size);
	float get_outline_size() const;

	void set_outline_modulate(const Color &p_color);
	Color get_outline_modulate() const;

	void set_align(Align p_align);
	Align get_align() const;

	void set_line_spacing(float p_spacing);
	float get_line_spacing() const;

	void set_billboard(bool p_billboard);
	bool get_billboard() const;

	void set_fallback_font(const Ref<Font> &p_font);
	Ref<Font> get_fallback_font() const;

	virtual AABB get_aabb() const;
	virtual PoolVector<Face3> get_faces(uint32_t p_usage_flags) const;

	SlugLabel3D();
	~SlugLabel3D();
};

VARIANT_ENUM_CAST(SlugLabel3D::Align);

#endif // SLUG_LABEL_3D_H
