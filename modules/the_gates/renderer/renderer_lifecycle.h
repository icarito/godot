#ifndef TG_RENDERER_LIFECYCLE_H
#define TG_RENDERER_LIFECYCLE_H

#include "core/array.h"
#include "core/math/vector2.h"
#include "core/object.h"

class CommandSync;
class InputSync;
class TGGLExternalTexture;

// Godot 4's fork drives this from #ifdef TG_RENDERER blocks in main.cpp. Godot
// 3 needs no engine patch: register_module_types runs after the window and GL
// context exist, and VisualServer::frame_post_draw is a per-frame hook that
// fires on the thread owning the context.
class TGRendererLifecycle : public Object {
	GDCLASS(TGRendererLifecycle, Object);

	CommandSync *command_sync = nullptr;
	InputSync *input_sync = nullptr;
	TGGLExternalTexture *ext_texture = nullptr;

	bool first_frame_sent = false;
	int frames_seen = 0;
	uint64_t last_heartbeat_ms = 0;
	int last_mouse_mode = -1;
	Size2i shared_size;

	Size2i shared_texture_size() const;
	void import_shared_texture();
	void forward_mouse_mode();

protected:
	static void _bind_methods();

public:
	static TGRendererLifecycle *get_singleton();

	bool engage();
	void teardown();
	void send_command(const String &p_name, const Array &p_args);
	void _on_frame_post_draw();

	TGRendererLifecycle();
	~TGRendererLifecycle();
};

bool tg_renderer_is_enabled();
void tg_renderer_engage();
void tg_renderer_teardown();

#endif // TG_RENDERER_LIFECYCLE_H
