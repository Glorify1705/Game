#pragma once
#ifndef GAME_NETWORK_H
#define GAME_NETWORK_H

#include "allocators.h"
#include "libraries/enet.h"

namespace G {

// Wraps ENet for game networking. Owns the ENet host and manages peers.
class Network {
 public:
  explicit Network(size_t audio_channels, Allocator* allocator);
  ~Network();

  // Initialize ENet. Must be called before any other method.
  bool Init();

  // Shut down ENet and disconnect all peers.
  void Shutdown();

  // Create a server listening on the given port.
  bool CreateServer(uint16_t port, size_t max_clients, size_t channels = 3);

  // Create a client and connect to a remote host.
  bool CreateClient(size_t channels = 3);

  // Connect to a server (client mode).
  bool Connect(const char* host, uint16_t port);

  // Disconnect from the server or disconnect all peers.
  void Disconnect();

  // Returns true if a host has been created.
  bool IsActive() const { return host_ != nullptr; }

  // Poll ENet for events. Called once per frame from the main loop.
  // Dispatches events to Lua callbacks via the provided lua_State.
  void Poll(int timeout_ms = 0);

  // Send data to a specific peer.
  void Send(uint32_t peer_id, const void* data, size_t length,
            uint8_t channel, bool reliable);

  // Broadcast data to all connected peers.
  void Broadcast(const void* data, size_t length, uint8_t channel,
                 bool reliable);

  // Returns the number of connected peers.
  size_t PeerCount() const;

  // Network event, queued during Poll() for Lua dispatch.
  struct Event {
    enum Type { kConnect, kDisconnect, kReceive };
    Type type;
    uint32_t peer_id;
    uint8_t channel;
    const uint8_t* data;  // Only valid for kReceive, points into ENet packet.
    size_t data_length;
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
