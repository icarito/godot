#include "input_sync.h"

#include "core/os/input.h"
#include "core/print_string.h"
#include "input_event_compat.h"
#include "zmq_runtime.h"

void InputSync::socket_connect(const String &p_address) {
	sock.connect(tg_resolve_ipc_address(p_address).utf8().get_data());
}

void InputSync::receive_input_events() {
	zmq::message_t msg;

	while (sock.recv(msg, zmq::recv_flags::dontwait)) {
		std::string msg_str(static_cast<const char *>(msg.data()), msg.size());
		Ref<InputEvent> event = tg_input_event_from_godot4_text(String::utf8(msg_str.c_str()));
		if (event.is_null()) {
			continue;
		}
		Ref<InputEventMouse> mouse = event;
		if (mouse.is_valid()) {
			mouse->set_position(mouse->get_position() * input_scale);
			mouse->set_global_position(mouse->get_global_position() * input_scale);

			Ref<InputEventMouseMotion> motion = event;
			if (motion.is_valid()) {
				motion->set_relative(motion->get_relative() * input_scale);
				motion->set_speed(motion->get_speed() * input_scale);
			}
		}

		Input::get_singleton()->parse_input_event(event);
	}
}

void InputSync::close() {
	sock.close();
}

InputSync::InputSync() :
		sock(tg_zmq_context(), zmq::socket_type::pair) {
	sock.set(zmq::sockopt::linger, 0);
}

InputSync::~InputSync() {
}
