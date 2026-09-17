#ifndef TG_GATE_COMMANDS_H
#define TG_GATE_COMMANDS_H

#include "core/array.h"
#include "core/object.h"

// Exposed to gate scripts as the TheGates engine singleton, the Godot 3
// counterpart of the 4.x fork's SceneTree::send_command: it forwards open_gate,
// open_link and highlight_button to the launcher. Registered only inside
// TheGates, so a gate checks Engine.has_singleton("TheGates") first.
class TGGateCommands : public Object {
	GDCLASS(TGGateCommands, Object);

protected:
	static void _bind_methods();

public:
	void send_command(const String &p_name, const Array &p_args);
};

#endif // TG_GATE_COMMANDS_H
