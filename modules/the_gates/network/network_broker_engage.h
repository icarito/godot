#ifndef TG_NETWORK_BROKER_ENGAGE_H
#define TG_NETWORK_BROKER_ENGAGE_H

// Routes the renderer's sockets and DNS through the launcher's NetworkBroker
// when the launcher spawned it sandboxed (--tg-broker-fd=<n>). Returns false
// when no broker handle was passed, leaving networking untouched.
bool tg_engage_network_broker();
void tg_disengage_network_broker();

#endif // TG_NETWORK_BROKER_ENGAGE_H
