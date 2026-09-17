#include "command_sync.h"

#include "core/print_string.h"
#include "../tg_vformat.h"
#include "variant_tools.h"
#include "zmq_runtime.h"

#include <zmq.h>

void CommandSync::socket_connect(const String &p_address, const String &p_monitor_endpoint) {
	sock.connect(tg_resolve_ipc_address(p_address).utf8().get_data());

	std::string monitor_endpoint = p_monitor_endpoint.utf8().get_data();
	zmq_socket_monitor(sock.handle(), monitor_endpoint.c_str(), ZMQ_EVENT_ALL);
	monitor_sock.connect(monitor_endpoint);
}

void CommandSync::send_command(const String &p_name) {
	send_command(p_name, Array());
}

void CommandSync::send_command(const String &p_name, const Array &p_args) {
	// A registered Command class would shadow same-named identifiers in gate scripts.
	const String text = "Object(Command,\"name\":" + var_to_str(p_name) + ",\"args\":" + var_to_str(p_args) + ",\"script\":null)\n";
	std::string msg_str = text.utf8().get_data();
	zmq::message_t msg(msg_str);
	if (!sock.send(msg, zmq::send_flags::none)) {
		print_line("Failed to send command");
	}
}

void CommandSync::poll_monitor() {
	zmq::message_t msg;

	while (monitor_sock.recv(msg, zmq::recv_flags::dontwait)) {
		if (msg.size() >= 6) {
			const uint8_t *msg_data = static_cast<const uint8_t *>(msg.data());
			uint16_t event_id = 0;
			uint32_t value = 0;
			memcpy(&event_id, msg_data, sizeof(uint16_t));
			memcpy(&value, msg_data + sizeof(uint16_t), sizeof(uint32_t));

			print_line(tg_vformat("ZMQ Monitor Event: %d, Value: %d", event_id, value));

			if (event_id == ZMQ_EVENT_DISCONNECTED || event_id == ZMQ_EVENT_CLOSED || event_id == ZMQ_EVENT_CLOSE_FAILED) {
				peer_disconnected = true;
				print_line("ZMQ Peer disconnected detected");
			}

			if (event_id == ZMQ_EVENT_CONNECTED || event_id == ZMQ_EVENT_ACCEPTED || event_id == ZMQ_EVENT_LISTENING) {
				peer_disconnected = false;
				print_line("ZMQ Peer connected detected");
			}
		}

		// Each monitor event is multipart: the event frame, then the endpoint address.
		while (msg.more()) {
			if (!monitor_sock.recv(msg)) {
				break;
			}
		}
	}
}

void CommandSync::close() {
	// A final command such as exit_gate is still queued; give it time to reach the launcher.
	sock.set(zmq::sockopt::linger, 500);
	sock.close();
	monitor_sock.close();
}

CommandSync::CommandSync() :
		sock(tg_zmq_context(), zmq::socket_type::pair),
		monitor_sock(tg_zmq_context(), zmq::socket_type::pair) {
	sock.set(zmq::sockopt::linger, 0);
	monitor_sock.set(zmq::sockopt::linger, 0);
}

CommandSync::~CommandSync() {
}
