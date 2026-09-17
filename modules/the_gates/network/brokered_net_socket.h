#ifndef TG_BROKERED_NET_SOCKET_H
#define TG_BROKERED_NET_SOCKET_H

#include "core/io/net_socket.h"

class BrokeredNetSocket : public NetSocket {
private:
	int _sock = -1;
	Type _requested_type = TYPE_NONE;
	IP::Type _ip_type = IP::TYPE_NONE;

	// Destination the kernel socket is connected to. Used by sendto to
	// detect "different destination than the first one we locked."
	IP_Address _connected_ip;
	uint16_t _connected_port = 0;
	bool _connected = false;

	// Server-mode sockets are inert, the way WebSocketServer is on HTML5:
	// bind()/listen() succeed so a gate that tries to host a service carries
	// on instead of erroring every frame, but no socket is ever created and
	// accept() never yields a peer. A sandboxed renderer cannot host network
	// services, and the gate must not be able to tell the difference.
	bool _listening = false;

	// Socket options requested between open() and the first FD acquisition.
	// Replayed onto the kernel FD as soon as the broker hands it back.
	struct PendingOptions {
		bool has_broadcast = false;
		bool broadcast = false;
		bool has_blocking = false;
		bool blocking = false;
		bool has_ipv6_only = false;
		bool ipv6_only = false;
		bool has_tcp_no_delay = false;
		bool tcp_no_delay = false;
		bool has_reuse_address = false;
		bool reuse_address = false;
	} _pending;

	Error _ensure_connected(const IP_Address &p_ip, uint16_t p_port);
	void _apply_pending_options();

	// Probes whether a non-blocking connect has finished on `_sock`.
	// Returns OK on success, ERR_BUSY while the handshake is in flight,
	// FAILED on connect error or select/getsockopt error.
	Error _check_connect_complete() const;

protected:
	static NetSocket *_create_func();

public:
	// Replaces NetSocket::_create with one that returns BrokeredNetSocket.
	// Idempotent; safe to call multiple times.
	static void make_default();

	// NetSocket interface.
	Error open(Type p_sock_type, IP::Type &ip_type) override;
	void close() override;
	Error bind(IP_Address p_addr, uint16_t p_port) override;
	Error listen(int p_max_pending) override;
	Error connect_to_host(IP_Address p_host, uint16_t p_port) override;
	Error poll(PollType p_type, int timeout) const override;
	Error recv(uint8_t *p_buffer, int p_len, int &r_read) override;
	Error recvfrom(uint8_t *p_buffer, int p_len, int &r_read, IP_Address &r_ip, uint16_t &r_port, bool p_peek = false) override;
	Error send(const uint8_t *p_buffer, int p_len, int &r_sent) override;
	Error sendto(const uint8_t *p_buffer, int p_len, int &r_sent, IP_Address p_ip, uint16_t p_port) override;
	Ref<NetSocket> accept(IP_Address &r_ip, uint16_t &r_port) override;

	bool is_open() const override;
	int get_available_bytes() const override;

	Error set_broadcasting_enabled(bool p_enabled) override;
	void set_blocking_enabled(bool p_enabled) override;
	void set_ipv6_only_enabled(bool p_enabled) override;
	void set_tcp_no_delay_enabled(bool p_enabled) override;
	void set_reuse_address_enabled(bool p_enabled) override;
	Error join_multicast_group(const IP_Address &p_multi_address, String p_if_name) override;
	Error leave_multicast_group(const IP_Address &p_multi_address, String p_if_name) override;

	BrokeredNetSocket() {}
	~BrokeredNetSocket() override;
};

#endif // TG_BROKERED_NET_SOCKET_H
