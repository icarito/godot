#include "renderer_net_client.h"

#include "fd_passing.h"

#include "core/os/mutex.h"

#include <unistd.h>

namespace {

int s_control_fd = -1;
Mutex s_mutex;

void close_fd(int p_fd) {
	if (p_fd >= 0) {
		::close(p_fd);
	}
}

// The broker is single-peer and answers in order, so one mutex serializes every round trip.
Error round_trip(const BrokerProtocol::Request &p_req, BrokerProtocol::Response &r_resp, int *r_received_fd) {
	*r_received_fd = -1;

	uint8_t req_buf[BrokerProtocol::Request::MAX_SIZE];
	const int req_len = p_req.serialize(req_buf);

	MutexLock lock(s_mutex);

	const Error sent = TGFDPassing::send_msg(s_control_fd, -1, nullptr, req_buf, req_len);
	if (sent != OK) {
		return sent;
	}

	uint8_t resp_buf[BrokerProtocol::Response::MAX_SIZE];
	int resp_len = 0;
	const Error received = TGFDPassing::recv_msg(s_control_fd, r_received_fd, resp_buf, (int)sizeof(resp_buf), &resp_len);
	if (received != OK) {
		return received;
	}
	if (!r_resp.parse(resp_buf, resp_len)) {
		return ERR_INVALID_DATA;
	}
	return OK;
}

} // namespace

Error RendererNetClient::install(int p_fd) {
	ERR_FAIL_COND_V(p_fd < 0, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(s_control_fd >= 0 && s_control_fd != p_fd, ERR_ALREADY_EXISTS);
	s_control_fd = p_fd;
	return OK;
}

void RendererNetClient::shutdown() {
	close_fd(s_control_fd);
	s_control_fd = -1;
}

bool RendererNetClient::is_installed() {
	return s_control_fd >= 0;
}

Error RendererNetClient::open_socket(BrokerProtocol::Opcode p_opcode, const IP_Address &p_ip, uint16_t p_port,
		const String &p_hostname, int *r_fd) {
	ERR_FAIL_NULL_V(r_fd, ERR_INVALID_PARAMETER);
	*r_fd = -1;
	if (s_control_fd < 0) {
		return ERR_UNCONFIGURED;
	}

	BrokerProtocol::Request req;
	req.opcode = (uint8_t)p_opcode;
	req.set_ip(p_ip);
	req.port = p_port;
	req.hostname = p_hostname;

	BrokerProtocol::Response resp;
	int received_fd = -1;
	const Error rc = round_trip(req, resp, &received_fd);
	if (rc != OK || resp.status != BrokerProtocol::ST_OK || received_fd < 0) {
		close_fd(received_fd);
		if (rc != OK) {
			return rc;
		}
		const bool denied = resp.status == BrokerProtocol::ST_DENIED_POLICY || resp.status == BrokerProtocol::ST_DENIED_FORCE_FAIL;
		return denied ? ERR_UNAUTHORIZED : FAILED;
	}
	*r_fd = received_fd;
	return OK;
}

bool RendererNetClient::resolve_hostname(const String &p_hostname, List<IP_Address> &r_addresses) {
	if (s_control_fd < 0) {
		return false;
	}

	BrokerProtocol::Request req;
	req.opcode = (uint8_t)BrokerProtocol::OP_RESOLVE_HOSTNAME;
	req.hostname = p_hostname;

	BrokerProtocol::Response resp;
	int unused_fd = -1;
	const Error rc = round_trip(req, resp, &unused_fd);
	close_fd(unused_fd);
	if (rc != OK || resp.status != BrokerProtocol::ST_OK) {
		return false;
	}
	resp.get_resolved_ips(r_addresses);
	return !r_addresses.empty();
}
