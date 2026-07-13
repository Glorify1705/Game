#include "logging.h"
#include "network.h"

namespace G {

// Browsers cannot open the raw UDP sockets ENet needs; every entry point
// that would start networking reports failure, which lua_network.cc turns
// into a Lua error.
Network::Network(Allocator* allocator) : allocator_(allocator) {}

Network::~Network() {}

ErrorOr<void> Network::Init() {
  return Error::Message("networking is not supported on web");
}

void Network::Shutdown() {}

ErrorOr<void> Network::CreateServer(uint16_t /*port*/, size_t /*max_clients*/,
                                    size_t /*channels*/) {
  return Error::Message("networking is not supported on web");
}

ErrorOr<void> Network::CreateClient(size_t /*channels*/) {
  return Error::Message("networking is not supported on web");
}

ErrorOr<void> Network::Connect(const char* /*host*/, uint16_t /*port*/) {
  return Error::Message("networking is not supported on web");
}

void Network::Disconnect() {}

void Network::Poll(int /*timeout_ms*/) {}

void Network::Send(uint32_t /*peer_id*/, ByteSlice /*data*/,
                   uint8_t /*channel*/, Reliability /*reliability*/) {}

void Network::Broadcast(ByteSlice /*data*/, uint8_t /*channel*/,
                        Reliability /*reliability*/) {}

size_t Network::PeerCount() const { return 0; }

void Network::FreeReceivedPackets() {}

}  // namespace G
