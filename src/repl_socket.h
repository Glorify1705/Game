#pragma once
#ifndef _GAME_REPL_SOCKET_H
#define _GAME_REPL_SOCKET_H

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace G {

// Initializes platform socket subsystem (Winsock on Windows, no-op on POSIX).
bool InitSockets();

// Shuts down platform socket subsystem.
void ShutdownSockets();

// Creates a non-blocking TCP listen socket bound to 127.0.0.1 on the given
// port. Returns kInvalidSocket on failure.
SocketHandle CreateListenSocket(uint16_t port);

// Accepts a pending connection on a listen socket. Returns kInvalidSocket if
// no connection is ready. The accepted socket is set to non-blocking mode.
SocketHandle AcceptConnection(SocketHandle listen_socket);

// Polls an array of sockets for readability. Returns the number of sockets
// that are ready, or -1 on error. timeout_ms of 0 means non-blocking poll.
// The ready flags are written into the out_ready array (true = readable).
int PollSockets(const SocketHandle* sockets, bool* out_ready, int count,
                int timeout_ms);

// Receives data from a socket. Returns bytes read, 0 on disconnect, or -1 if
// no data is available (EAGAIN/EWOULDBLOCK).
int SocketRecv(SocketHandle socket, char* buffer, int buffer_size);

// Sends data on a socket. Returns bytes sent, or -1 on error.
int SocketSend(SocketHandle socket, const char* data, int data_size);

// Closes a socket handle.
void CloseSocket(SocketHandle socket);

}  // namespace G

#endif  // _GAME_REPL_SOCKET_H
