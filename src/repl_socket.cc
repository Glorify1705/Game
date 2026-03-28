#include "repl_socket.h"

#include "logging.h"

#ifdef _WIN32

namespace G {

bool InitSockets() {
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

void ShutdownSockets() { WSACleanup(); }

SocketHandle CreateListenSocket(uint16_t port) {
  SocketHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == kInvalidSocket) return kInvalidSocket;

  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

  // Non-blocking mode.
  u_long mode = 1;
  ioctlsocket(sock, FIONBIO, &mode);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    closesocket(sock);
    return kInvalidSocket;
  }
  if (listen(sock, /*backlog=*/4) != 0) {
    closesocket(sock);
    return kInvalidSocket;
  }
  return sock;
}

SocketHandle AcceptConnection(SocketHandle listen_socket) {
  SocketHandle client = accept(listen_socket, nullptr, nullptr);
  if (client == kInvalidSocket) return kInvalidSocket;
  u_long mode = 1;
  ioctlsocket(client, FIONBIO, &mode);
  return client;
}

int PollSockets(const SocketHandle* sockets, bool* out_ready, int count,
                int timeout_ms) {
  WSAPOLLFD fds[8];
  int n = count < 8 ? count : 8;
  for (int i = 0; i < n; ++i) {
    fds[i].fd = sockets[i];
    fds[i].events = POLLIN;
    fds[i].revents = 0;
    out_ready[i] = false;
  }
  int result = WSAPoll(fds, n, timeout_ms);
  if (result > 0) {
    for (int i = 0; i < n; ++i) {
      out_ready[i] = (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    }
  }
  return result;
}

int SocketRecv(SocketHandle socket, char* buffer, int buffer_size) {
  int r = recv(socket, buffer, buffer_size, 0);
  if (r == SOCKET_ERROR) {
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return -1;
    return 0;
  }
  return r;
}

int SocketSend(SocketHandle socket, const char* data, int data_size) {
  return send(socket, data, data_size, 0);
}

void CloseSocket(SocketHandle socket) {
  if (socket != kInvalidSocket) closesocket(socket);
}

}  // namespace G

#else  // POSIX

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace G {

namespace {

void SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

bool InitSockets() { return true; }

void ShutdownSockets() {}

SocketHandle CreateListenSocket(uint16_t port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return kInvalidSocket;

  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  SetNonBlocking(sock);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) !=
      0) {
    ELOG("REPL: bind failed on port ", port, ": errno=", errno);
    close(sock);
    return kInvalidSocket;
  }
  if (listen(sock, /*backlog=*/4) != 0) {
    ELOG("REPL: listen failed: errno=", errno);
    close(sock);
    return kInvalidSocket;
  }
  return sock;
}

SocketHandle AcceptConnection(SocketHandle listen_socket) {
  int client = accept(listen_socket, nullptr, nullptr);
  if (client < 0) return kInvalidSocket;
  SetNonBlocking(client);
  // Disable Nagle for low-latency responses.
  int yes = 1;
  setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  return client;
}

int PollSockets(const SocketHandle* sockets, bool* out_ready, int count,
                int timeout_ms) {
  struct pollfd fds[8];
  int n = count < 8 ? count : 8;
  for (int i = 0; i < n; ++i) {
    fds[i].fd = sockets[i];
    fds[i].events = POLLIN;
    fds[i].revents = 0;
    out_ready[i] = false;
  }
  int result = poll(fds, n, timeout_ms);
  if (result > 0) {
    for (int i = 0; i < n; ++i) {
      out_ready[i] = (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    }
  }
  return result;
}

int SocketRecv(SocketHandle socket, char* buffer, int buffer_size) {
  ssize_t r = recv(socket, buffer, buffer_size, 0);
  if (r < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    return 0;
  }
  return static_cast<int>(r);
}

int SocketSend(SocketHandle socket, const char* data, int data_size) {
  ssize_t r = send(socket, data, data_size, MSG_NOSIGNAL);
  if (r < 0) return -1;
  return static_cast<int>(r);
}

void CloseSocket(SocketHandle socket) {
  if (socket != kInvalidSocket) close(socket);
}

}  // namespace G

#endif
