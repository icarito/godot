#include "renderer_lifecycle.h"

#include "../ipc/command_sync.h"
#include "../ipc/input_sync.h"
#include "../ipc/zmq_runtime.h"
#include "gl_external_texture.h"

#ifdef X11_ENABLED
#include "../network/network_broker_engage.h"
#include "../sandbox/lockdown_engage.h"
#endif

#include "core/os/input.h"
#include "core/os/os.h"
#include "core/print_string.h"
#include "core/project_settings.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "servers/visual_server.h"

namespace {

const uint64_t HEARTBEAT_INTERVAL_MSEC = 1000;

// RenderingDevice::DATA_FORMAT_* in the Godot 4 launcher. Godot 3 has no
// RenderingDevice, so the values are sent as the plain ints the launcher's
// RenderResult.set_texture_format matches on.
const int DATA_FORMAT_R8G8B8A8_UNORM = 36;
const int DATA_FORMAT_B8G8R8A8_UNORM = 43;

TGRendererLifecycle *singleton = nullptr;

// user:// becomes --tg-user-data-dir, the per-gate folder the launcher's sandbox lets the renderer write.
void redirect_user_data_dir() {
	const String gate_dir = tg_cmdline_value("--tg-user-data-dir");
	if (gate_dir.empty()) {
		return;
	}
	const String data_path = OS::get_singleton()->get_data_path();
	ERR_FAIL_COND_MSG(!gate_dir.begins_with(data_path + "/"),
			"--tg-user-data-dir is not under the data path " + data_path + "; user:// keeps its default location");
	ProjectSettings::get_singleton()->set("application/config/use_custom_user_dir", true);
	ProjectSettings::get_singleton()->set("application/config/custom_user_dir_name", gate_dir.substr(data_path.length() + 1));
}

} // namespace

TGRendererLifecycle *TGRendererLifecycle::get_singleton() {
	return singleton;
}

bool TGRendererLifecycle::engage() {
	print_line("[RENDERER-START]");
	redirect_user_data_dir();

#ifdef X11_ENABLED
	const bool sandboxed = tg_engage_network_broker();
#endif

	// The launcher allocated the shared image at --resolution; matching the
	// window keeps the per-frame blit 1:1 instead of rescaling.
	shared_size = shared_texture_size();
	OS::get_singleton()->set_window_size(Size2(shared_size.width, shared_size.height));

	command_sync = memnew(CommandSync);
	command_sync->socket_connect();

	const String filehandle_path = tg_resolve_ipc_address(FILEHANDLE_PATH);
	Array filehandle_arg;
#ifdef WINDOWS_ENABLED
	// The launcher needs this process's id to DuplicateHandle the shared image into it.
	filehandle_arg.append(filehandle_path + "|" + itos(OS::get_singleton()->get_process_id()));
#else
	filehandle_arg.append(filehandle_path);
#endif
	command_sync->send_command("send_filehandle", filehandle_arg);

	ext_texture = memnew(TGGLExternalTexture);
	print_line("TGGLExternalTexture: waiting for filehandle");
	if (!ext_texture->recv_filehandle(filehandle_path)) {
		return false;
	}
	// Opening the allocation (and measuring it through Vulkan) needs files the lockdown forbids.
	import_shared_texture();

	Array format_arg;
	format_arg.append(ext_texture->is_bgra() ? DATA_FORMAT_B8G8R8A8_UNORM : DATA_FORMAT_R8G8B8A8_UNORM);
	command_sync->send_command("ext_texture_format", format_arg);

	input_sync = memnew(InputSync);
	input_sync->socket_connect();

#ifdef X11_ENABLED
	if (sandboxed) {
		tg_lock_down_renderer();
	}
#endif

	VisualServer::get_singleton()->connect("frame_post_draw", this, "_on_frame_post_draw");
	return true;
}

// The launcher allocated the shared image at the size it passed as
// --resolution, which a gate's own window settings can diverge from. Importing
// at any other size would alias the allocation.
Size2i TGRendererLifecycle::shared_texture_size() const {
	const String resolution = tg_cmdline_value("--resolution");
	const Vector<String> parts = resolution.split("x");
	if (parts.size() == 2 && parts[0].is_valid_integer() && parts[1].is_valid_integer()) {
		return Size2i(parts[0].to_int(), parts[1].to_int());
	}

	const Size2 window = OS::get_singleton()->get_window_size();
	return Size2i((int)window.width, (int)window.height);
}

void TGRendererLifecycle::import_shared_texture() {
	if (!ext_texture->import(shared_size.width, shared_size.height)) {
		// Matches the Godot 4 renderer: a failed handshake kills the process so
		// the launcher reports an error instead of showing an empty gate.
		CRASH_NOW_MSG("Shared texture import failed. Exiting child.");
	}
}

void TGRendererLifecycle::send_command(const String &p_name, const Array &p_args) {
	ERR_FAIL_NULL(command_sync);
	command_sync->send_command(p_name, p_args);
}

void TGRendererLifecycle::forward_mouse_mode() {
	const int mode = (int)Input::get_singleton()->get_mouse_mode();
	if (mode == last_mouse_mode) {
		return;
	}
	last_mouse_mode = mode;

	Array args;
	args.append(mode);
	command_sync->send_command("set_mouse_mode", args);
}

void TGRendererLifecycle::_on_frame_post_draw() {
	if (command_sync == nullptr || ext_texture == nullptr || input_sync == nullptr) {
		return;
	}

	if (!ext_texture->is_imported()) {
		import_shared_texture();
	}

	SceneTree *tree = SceneTree::get_singleton();
	if (tree != nullptr && tree->get_root() != nullptr) {
		const Size2 source = tree->get_root()->get_size();
		const RID viewport_texture = VisualServer::get_singleton()->viewport_get_texture(
				tree->get_root()->get_viewport_rid());
		ext_texture->copy_from_texture(
				VisualServer::get_singleton()->texture_get_texid(viewport_texture),
				(int)source.width, (int)source.height);
	}

	// Viewport applies the stretch transform itself, so events must arrive in window space.
	if (shared_size.width > 0 && shared_size.height > 0) {
		const Size2 window = OS::get_singleton()->get_window_size();
		input_sync->set_input_scale(Vector2(window.width / (float)shared_size.width,
				window.height / (float)shared_size.height));
	}

	frames_seen++;
	if (!first_frame_sent && frames_seen > 2) {
		command_sync->send_command("first_frame");
		first_frame_sent = true;
	}

	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (first_frame_sent && now - last_heartbeat_ms > HEARTBEAT_INTERVAL_MSEC) {
		command_sync->send_command("heartbeat");
		last_heartbeat_ms = now;
	}

	input_sync->receive_input_events();
	forward_mouse_mode();

	command_sync->poll_monitor();
	if (!command_sync->is_peer_connected()) {
		CRASH_NOW_MSG("CommandSync peer disconnected. Exiting child.");
	}
}

void TGRendererLifecycle::teardown() {
	if (VisualServer::get_singleton() != nullptr &&
			VisualServer::get_singleton()->is_connected("frame_post_draw", this, "_on_frame_post_draw")) {
		VisualServer::get_singleton()->disconnect("frame_post_draw", this, "_on_frame_post_draw");
	}

	if (ext_texture != nullptr) {
		memdelete(ext_texture);
		ext_texture = nullptr;
	}
	if (input_sync != nullptr) {
		input_sync->close();
		memdelete(input_sync);
		input_sync = nullptr;
	}
	if (command_sync != nullptr) {
		command_sync->send_command("exit_gate");
		command_sync->close();
		memdelete(command_sync);
		command_sync = nullptr;
	}
}

void TGRendererLifecycle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_frame_post_draw"), &TGRendererLifecycle::_on_frame_post_draw);
}

TGRendererLifecycle::TGRendererLifecycle() {
	singleton = this;
}

TGRendererLifecycle::~TGRendererLifecycle() {
	teardown();
	singleton = nullptr;
}

bool tg_renderer_is_enabled() {
	return !tg_cmdline_value("--tg-ipc-dir").empty();
}

void tg_renderer_engage() {
	if (TGRendererLifecycle::get_singleton() != nullptr) {
		return;
	}

	TGRendererLifecycle *lifecycle = memnew(TGRendererLifecycle);
	if (!lifecycle->engage()) {
		CRASH_NOW_MSG("Renderer handshake with the launcher failed.");
	}
	print_line("[RENDERER-READY]");
}

void tg_renderer_teardown() {
	TGRendererLifecycle *lifecycle = TGRendererLifecycle::get_singleton();
	if (lifecycle != nullptr) {
		memdelete(lifecycle);
	}
#ifdef X11_ENABLED
	tg_disengage_network_broker();
#endif
	tg_zmq_shutdown();
}
