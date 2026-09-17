#ifndef TG_RENDERER_NET_CLIENT_H
#define TG_RENDERER_NET_CLIENT_H

#include "broker_protocol.h"

#include "core/error_list.h"
#include "core/io/ip.h"
#include "core/io/ip_address.h"
#include "core/ustring.h"

// Renderer end of the launcher's NetworkBroker, ported from the 4.x fork's
// modules/the_gates/network/renderer_net_client. The broker opens every socket
// and resolves every hostname on the renderer's behalf, over the AF_UNIX
// control channel the launcher passes as --tg-broker-fd.
class RendererNetClient {
public:
	static Error install(int p_fd);
	static void shutdown();
	static bool is_installed();

	// On success the returned fd is connected to the destination and owned by the caller.
	static Error open_socket(BrokerProtocol::Opcode p_opcode, const IP_Address &p_ip, uint16_t p_port,
			const String &p_hostname, int *r_fd);

	// Returns false on any broker error, so callers fail closed.
	static bool resolve_hostname(const String &p_hostname, List<IP_Address> &r_addresses);
};

#endif // TG_RENDERER_NET_CLIENT_H
