#include "gate_commands.h"

#include "renderer_lifecycle.h"

void TGGateCommands::send_command(const String &p_name, const Array &p_args) {
	TGRendererLifecycle *lifecycle = TGRendererLifecycle::get_singleton();
	ERR_FAIL_NULL_MSG(lifecycle, "TheGates.send_command: the renderer is not connected to a launcher");
	lifecycle->send_command(p_name, p_args);
}

void TGGateCommands::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send_command", "name", "args"), &TGGateCommands::send_command, DEFVAL(Array()));
}
