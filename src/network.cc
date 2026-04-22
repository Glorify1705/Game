#include "network.h"

#include "logging.h"
#include "units.h"

namespace G {

namespace {

// Custom ENet allocator callbacks routing through the engine's allocator.
Allocator* s_enet_allocator = nullptr;

void* ENET_CALLBACK EnetMalloc(size_t size) {
  return s_enet_allocator->Alloc(size, alignof(std::max_align_t));
}

void ENET_CALLBACK EnetFree(void* memory) {
  // ENet's default free callback does not pass size. Since we use a
  // MimallocAllocator (which tracks sizes internally), passing 0 is safe.
  s_enet_allocator->Dealloc(memory, 0);
}

void ENET_CALLBACK EnetNoMemory() {
  LOG("ENet: out of memory");
}

}  // namespace

Network::Network(Allocator* allocator) : allocator_(allocator) {}

Network::~Network() { Shutdown(); }

bool Network::Init() {
  if (initialized_) return true;
  s_enet_allocator = allocator_;
  ENetCallbacks callbacks = {EnetMalloc, EnetFree, EnetNoMemory,
                             nullptr, nullptr};
  if (enet_initialize_with_callbacks(ENET_VERSION, &callbacks) != 0) {
    LOG("Failed to initialize ENet");
    return false;
  }
  initialized_ = true;
  LOG("ENet initialized");
  return true;
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

bool Network::CreateServer(uint16_t port, size_t max_clients, size_t channels) {
  if (!initialized_) Init();
  if (host_ != nullptr) {
    LOG("Network host already exists, skipping creation");
    return true;
  }
  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;
  host_ = enet_host_create(&address, max_clients, channels, 0, 0);
  if (host_ == nullptr) {
    LOG("Failed to create ENet server on port ", port);
    return false;
  }
  LOG("Network server created on port ", port, " (max ", max_clients,
      " clients)");
  return true;
}

bool Network::CreateClient(size_t channels) {
  if (!initialized_) Init();
  if (host_ != nullptr) {
    LOG("Network host already exists, skipping creation");
    return true;
  }
  host_ = enet_host_create(nullptr, /*peerCount=*/1, channels, 0, 0);
  if (host_ == nullptr) {
    LOG("Failed to create ENet client");
    return false;
  }
  LOG("Network client created");
  return true;
}

bool Network::Connect(const char* hostname, uint16_t port) {
  if (host_ == nullptr) {
    LOG("No network host — call create_client first");
    return false;
  }
  ENetAddress address;
  enet_address_set_host(&address, hostname);
  address.port = port;
  ENetPeer* peer = enet_host_connect(host_, &address, host_->channelLimit, 0);
  if (peer == nullptr) {
    LOG("Failed to initiate connection to ", hostname, ":", port);
    return false;
  }
  LOG("Connecting to ", hostname, ":", port);
  return true;
}

void Network::Disconnect() {
  if (host_ == nullptr) return;
  for (size_t i = 0; i < host_->peerCount; ++i) {
    ENetPeer* peer = &host_->peers[i];
    if (peer->state == ENET_PEER_STATE_CONNECTED) {
      enet_peer_disconnect(peer, 0);
    }
  }
  // Flush disconnect packets.
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
        e.data = nullptr;
        e.data_length = 0;
        ++event_count_;
        LOG("Peer ", e.peer_id, " connected");
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        e.type = Event::kDisconnect;
        e.peer_id = static_cast<uint32_t>(event.peer - host_->peers);
        e.channel = 0;
        e.data = nullptr;
        e.data_length = 0;
        ++event_count_;
        LOG("Peer ", e.peer_id, " disconnected");
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        e.type = Event::kReceive;
        e.peer_id = static_cast<uint32_t>(event.peer - host_->peers);
        e.channel = event.channelID;
        e.data = event.packet->data;
        e.data_length = event.packet->dataLength;
        received_packets_[received_packet_count_++] = event.packet;
        ++event_count_;
        break;
      default:
        break;
    }
  }
}

void Network::Send(uint32_t peer_id, const void* data, size_t length,
                   uint8_t channel, bool reliable) {
  if (host_ == nullptr || peer_id >= host_->peerCount) return;
  ENetPacket* packet = enet_packet_create(
      data, length, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  enet_peer_send(&host_->peers[peer_id], channel, packet);
}

void Network::Broadcast(const void* data, size_t length, uint8_t channel,
                        bool reliable) {
  if (host_ == nullptr) return;
  ENetPacket* packet = enet_packet_create(
      data, length, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
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
