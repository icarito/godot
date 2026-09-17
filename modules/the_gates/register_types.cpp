#include "register_types.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "renderer/gate_commands.h"
#include "renderer/renderer_lifecycle.h"

namespace {

TGGateCommands *gate_commands = nullptr;

} // namespace

void register_the_gates_types() {
	ClassDB::register_class<TGRendererLifecycle>();
	ClassDB::register_class<TGGateCommands>();

	// Without the launcher's IPC directory this binary is a plain Godot 3
	// engine, so the handshake — which blocks on the shared texture — is
	// skipped rather than hanging.
	if (tg_renderer_is_enabled()) {
		tg_renderer_engage();
		gate_commands = memnew(TGGateCommands);
		Engine::get_singleton()->add_singleton(Engine::Singleton("TheGates", gate_commands));
	}
}

void unregister_the_gates_types() {
	tg_renderer_teardown();
	if (gate_commands != nullptr) {
		memdelete(gate_commands);
		gate_commands = nullptr;
	}
}
