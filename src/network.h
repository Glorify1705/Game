#pragma once
#ifndef GAME_NETWORK_H
#define GAME_NETWORK_H

#include "allocators.h"
#include "array.h"
#include "error.h"
#include "libraries/enet.h"

namespace G {

// Whether a packet must be delivered reliably (retransmitted on loss) or
// can be dropped (fire-and-forget, lower latency).
enum class Reliability : uint8_t { kReliable, kUnreliable };

// Wraps ENet for game networking. Owns the ENet host and manages peers.
class Network {
 public:
  explicit Network(Allocator* allocator);
  ~Network();

  // Initialize ENet. Must be called before any other method.
  ErrorOr<void> Init();

  // Shut down ENet and disconnect all peers.
  void Shutdown();

  // Create a server listening on the given port.
  ErrorOr<void> CreateServer(uint16_t port, size_t max_clients,
                             size_t channels = 3);

  // Create a client host for connecting to a server.
  ErrorOr<void> CreateClient(size_t channels = 3);

  // Connect to a remote server (client mode).
  ErrorOr<void> Connect(const char* host, uint16_t port);

  // Disconnect from the server or disconnect all peers.
  void Disconnect();

  // Returns true if a host has been created.
  bool IsActive() const { return host_ != nullptr; }

  // Poll ENet for events. Called once per frame from the main loop.
  void Poll(int timeout_ms = 0);

  // Send data to a specific peer.
  void Send(uint32_t peer_id, ByteSlice data, uint8_t channel,
            Reliability reliability);

  // Broadcast data to all connected peers.
  void Broadcast(ByteSlice data, uint8_t channel, Reliability reliability);

  // Returns the number of connected peers.
  size_t PeerCount() const;

  // Network event, queued during Poll() for Lua dispatch.
  struct Event {
    // Event type.
    enum Type { kConnect, kDisconnect, kReceive };
    Type type;
    // Peer index within the ENet host's peer array.
    uint32_t peer_id;
    // Channel the packet arrived on (only for kReceive).
    uint8_t channel;
    // Packet payload (only valid for kReceive, points into ENet packet).
    ByteSlice data;
  };

  // Access queued events after Poll(). Reset each frame.
  const Event* events() const { return events_; }
  size_t event_count() const { return event_count_; }

  // Frees ENet packets from the last Poll(). Call after dispatching events.
  void FreeReceivedPackets();

  // Access the underlying ENet host for debug UI stats. May be null.
  ENetHost* host() const { return host_; }

 private:
  ENetHost* host_ = nullptr;
  Allocator* allocator_;
  bool initialized_ = false;

  // Per-frame event buffer.
  static constexpr size_t kMaxEventsPerFrame = 256;
  Event events_[kMaxEventsPerFrame];
  size_t event_count_ = 0;

  // Packets received during Poll() that need freeing after dispatch.
  ENetPacket* received_packets_[kMaxEventsPerFrame] = {};
  size_t received_packet_count_ = 0;
};

}  // namespace G

#endif
