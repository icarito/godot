#ifndef TG_COMMAND_SYNC_H
#define TG_COMMAND_SYNC_H

#include "core/array.h"
#include "core/ustring.h"

#include "zmq.hpp"

#ifdef WINDOWS_ENABLED
static const String COMMAND_SYNC_ADDRESS("ipc://user://command_sync");
#else
static const String COMMAND_SYNC_ADDRESS("ipc:///tmp/command_sync");
#endif
static const String COMMAND_SYNC_MONITOR_ENDPOINT("inproc://command_sync_monitor");

// Renderer half of the command channel: the launcher binds, we connect and
// send. Nothing in a Godot 3 gate receives commands, so there is no execute
// callback here — see CommandSync in the Godot 4 fork for the full duplex.
class CommandSync {
	zmq::socket_t sock;
	zmq::socket_t monitor_sock;
	bool peer_disconnected = false;

public:
	void socket_connect(const String &p_address = COMMAND_SYNC_ADDRESS,
			const String &p_monitor_endpoint = COMMAND_SYNC_MONITOR_ENDPOINT);

	void send_command(const String &p_name);
	void send_command(const String &p_name, const Array &p_args);

	void poll_monitor();
	bool is_peer_connected() const { return !peer_disconnected; }

	void close();

	CommandSync();
	~CommandSync();
};

#endif // TG_COMMAND_SYNC_H
