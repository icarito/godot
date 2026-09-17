#ifndef TG_MINGW_SYS_SOCKET_H
#define TG_MINGW_SYS_SOCKET_H

// libzmq includes this for AF_UNIX outside MSVC; MinGW provides it through Winsock.
#include <winsock2.h>
#include <ws2tcpip.h>

#endif // TG_MINGW_SYS_SOCKET_H
