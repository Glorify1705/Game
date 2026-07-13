#include "network.h"

#include "logging.h"

#ifdef GAME_WEB

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

#else

namespace G {

namespace {

// ENet allocator callback: allocates via the engine allocator passed as ctx.
void* ENET_CALLBACK EnetMalloc(size_t size, void* ctx) {
  return static_cast<Allocator*>(ctx)->Alloc(size, alignof(max_align_t));
}

// ENet allocator callback: frees via the engine allocator passed as ctx.
void ENET_CALLBACK EnetFree(void* memory, size_t size, void* ctx) {
  static_cast<Allocator*>(ctx)->Dealloc(memory, size);
}

// ENet callback when allocation fails. Crashes — cannot recover.
void ENET_CALLBACK EnetNoMemory() { CHECK(false, "ENet: out of memory"); }

}  // namespace

Network::Network(Allocator* allocator) : allocator_(allocator) {}

Network::~Network() { Shutdown(); }

ErrorOr<void> Network::Init() {
  if (initialized_) return {};
  ENetCallbacks callbacks = {EnetMalloc, EnetFree, EnetNoMemory,
                             allocator_, nullptr,  nullptr};
  if (enet_initialize_with_callbacks(ENET_VERSION, &callbacks) != 0) {
    return Error::Message("Failed to initialize ENet");
  }
  initialized_ = true;
  LOG("ENet initialized");
  return {};
}

void Network::Shutdown() {
  if (host_ != nullptr) {
    Disconnect();
    enet_host_destroy(host_);
    host_ = nullptr;
  }
  if (initialized_) {
    enet_deinitialize();
    initialized_ = false;
  }
}

ErrorOr<void> Network::CreateServer(uint16_t port, size_t max_clients,
                                    size_t channels) {
  TRY(Init());
  if (host_ != nullptr) {
    LOG("Network host already exists, skipping creation");
    return {};
  }
  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;
  host_ = enet_host_create(&address, max_clients, channels, 0, 0);
  if (host_ == nullptr) {
    return Error::Message("Failed to create ENet server");
  }
  LOG("Network server created on port ", port, " (max ", max_clients,
      " clients)");
  return {};
}

ErrorOr<void> Network::CreateClient(size_t channels) {
  TRY(Init());
  if (host_ != nullptr) {
    LOG("Network host already exists, skipping creation");
    return {};
  }
  host_ = enet_host_create(nullptr, /*peerCount=*/1, channels, 0, 0);
  if (host_ == nullptr) {
    return Error::Message("Failed to create ENet client");
  }
  LOG("Network client created");
  return {};
}

ErrorOr<void> Network::Connect(const char* hostname, uint16_t port) {
  if (host_ == nullptr) {
    return Error::Message("No network host — call create_client first");
  }
  ENetAddress address;
  enet_address_set_host(&address, hostname);
  address.port = port;
  ENetPeer* peer = enet_host_connect(host_, &address, host_->channelLimit, 0);
  if (peer == nullptr) {
    return Error::Message("Failed to initiate connection");
  }
  LOG("Connecting to ", hostname, ":", port);
  return {};
}

void Network::Disconnect() {
  if (host_ == nullptr) return;
  for (size_t i = 0; i < host_->peerCount; ++i) {
    ENetPeer* peer = &host_->peers[i];
    if (peer->state == ENET_PEER_STATE_CONNECTED) {
      enet_peer_disconnect(peer, 0);
    }
  }
  enet_host_flush(host_);
}

void Network::FreeReceivedPackets() {
  for (size_t i = 0; i < received_packet_count_; ++i) {
    enet_packet_destroy(received_packets_[i]);
  }
  received_packet_count_ = 0;
}

void Network::Poll(int timeout_ms) {
  event_count_ = 0;
  received_packet_count_ = 0;
  if (host_ == nullptr) return;
  ENetEvent event;
  while (enet_host_service(host_, &event, timeout_ms) > 0) {
    timeout_ms = 0;  // Only block on the first call.
    if (event_count_ >= kMaxEventsPerFrame) break;
    Event& e = events_[event_count_];
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        e.type = Event::kConnect;
        e.peer_id = static_cast<uint32_t>(event.peer - host_->peers);
        e.channel = 0;
        e.data = ByteSlice();
        ++event_count_;
        LOG("Peer ", e.peer_id, " connected");
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        e.type = Event::kDisconnect;
        e.peer_id = static_cast<uint32_t>(event.peer - host_->peers);
        e.channel = 0;
        e.data = ByteSlice();
        ++event_count_;
        LOG("Peer ", e.peer_id, " disconnected");
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        e.type = Event::kReceive;
        e.peer_id = static_cast<uint32_t>(event.peer - host_->peers);
        e.channel = event.channelID;
        e.data = ByteSlice(event.packet->data, event.packet->dataLength);
        received_packets_[received_packet_count_++] = event.packet;
        ++event_count_;
        break;
      default:
        break;
    }
  }
}

void Network::Send(uint32_t peer_id, ByteSlice data, uint8_t channel,
                   Reliability reliability) {
  if (host_ == nullptr || peer_id >= host_->peerCount) return;
  enet_uint32 flags =
      reliability == Reliability::kReliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
  enet_peer_send(&host_->peers[peer_id], channel, packet);
}

void Network::Broadcast(ByteSlice data, uint8_t channel,
                        Reliability reliability) {
  if (host_ == nullptr) return;
  enet_uint32 flags =
      reliability == Reliability::kReliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
  enet_host_broadcast(host_, channel, packet);
}

size_t Network::PeerCount() const {
  if (host_ == nullptr) return 0;
  size_t count = 0;
  for (size_t i = 0; i < host_->peerCount; ++i) {
    if (host_->peers[i].state == ENET_PEER_STATE_CONNECTED) ++count;
  }
  return count;
}

}  // namespace G

#endif  // GAME_WEB
