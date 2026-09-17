#include "network_broker_engage.h"

#include "brokered_net_socket.h"
#include "renderer_net_client.h"

#include "../ipc/zmq_runtime.h"
#include "../tg_vformat.h"
#include "core/io/ip.h"
#include "core/os/os.h"
#include "core/print_string.h"

namespace {

// Resolves through the broker. Godot 3's IP constructor makes each instance the
// singleton, so creating this one after the engine's diverts IP::resolve_hostname;
// the engine's instance is restored before this one is deleted.
class TGBrokeredIP : public IP {
	IP *previous = nullptr;

protected:
	void _resolve_hostname(List<IP_Address> &r_addresses, const String &p_hostname, Type p_type = TYPE_ANY) const override {
		RendererNetClient::resolve_hostname(p_hostname, r_addresses);
	}

public:
	void get_local_interfaces(Map<String, Interface_Info> *r_interfaces) const override {
		previous->get_local_interfaces(r_interfaces);
	}

	static TGBrokeredIP *install() {
		IP *engine_ip = IP::get_singleton();
		TGBrokeredIP *brokered = memnew(TGBrokeredIP);
		brokered->previous = engine_ip;
		return brokered;
	}

	static void uninstall(TGBrokeredIP *p_brokered) {
		singleton = p_brokered->previous;
		memdelete(p_brokered);
	}
};

TGBrokeredIP *brokered_ip = nullptr;

int broker_fd_from_argv() {
	const String prefix = "--tg-broker-fd=";
	const Vector<String> argv = tg_process_argv();
	for (int i = 0; i < argv.size(); i++) {
		if (argv[i].begins_with(prefix)) {
			const String value = argv[i].substr(prefix.length());
			return value.is_valid_integer() ? value.to_int() : -1;
		}
	}
	return -1;
}

} // namespace

bool tg_engage_network_broker() {
	const int broker_fd = broker_fd_from_argv();
	if (broker_fd < 0) {
		return false;
	}
	if (RendererNetClient::install(broker_fd) != OK) {
		CRASH_NOW_MSG("RendererNetClient::install failed; refusing to run a sandboxed gate without the network broker.");
	}
	BrokeredNetSocket::make_default();
	brokered_ip = TGBrokeredIP::install();
	print_line(tg_vformat("[NETWORK-BROKER] inherited fd=%d; NetSocket and IP routed through the launcher", broker_fd));
	return true;
}

void tg_disengage_network_broker() {
	if (brokered_ip != nullptr) {
		TGBrokeredIP::uninstall(brokered_ip);
		brokered_ip = nullptr;
	}
	RendererNetClient::shutdown();
}
