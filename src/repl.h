#pragma once
#ifndef _GAME_REPL_H
#define _GAME_REPL_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "allocators.h"
#include "circular_buffer.h"
#include "repl_socket.h"
#include "stringlib.h"

struct lua_State;

namespace G {

class Stats;

// TCP-based REPL server for evaluating Lua code in the running engine.
// The server runs a listener thread that accepts connections and reads NDJSON
// messages. Eval requests are queued for the main thread, which processes them
// between update and render. Results are queued back for the listener thread
// to send to clients.
class ReplServer {
 public:
  // Maximum number of simultaneous client connections.
  static constexpr int kMaxClients = 4;

  // Maximum size of a single NDJSON message (request or response).
  static constexpr size_t kMaxMessageSize = 8192;

  // Maximum number of pending eval requests.
  static constexpr size_t kMaxQueueSize = 64;

  ReplServer();
  ~ReplServer();

  // Initializes the server. Must be called before Start().
  void Init(uint16_t port, Allocator* allocator);

  // Starts the listener thread and begins accepting connections.
  void Start();

  // Stops the listener thread and closes all connections.
  void Stop();

  // Processes pending eval requests on the main thread. Call this once per
  // frame between update and render. The stats parameter is used for the
  // state operation to report FPS.
  void ProcessQueue(lua_State* state, const Stats* stats);

  // Returns true if the server is running.
  bool running() const { return running_; }

  // Enqueues a response to be sent back to a client. Public because the
  // print() capture function (a free function) needs to call it.
  void EnqueueResponse(int client_index, int id, const char* data,
                       size_t data_len);

 private:
  // An eval request from a client, queued for main-thread processing.
  struct EvalRequest {
    // Client index that sent the request.
    int client_index = 0;
    // Request ID from the NDJSON message.
    int id = 0;
    // Operation type.
    enum Op { kEval, kState, kInspect, kComplete };
    Op op = kEval;
    // Code or expression to evaluate (for eval/inspect/complete).
    char code[4096] = {};
    size_t code_len = 0;
  };

  // A response to send back to a client.
  struct Response {
    // Client index to send the response to.
    int client_index = 0;
    // The full NDJSON line to send (including trailing newline).
    char data[kMaxMessageSize] = {};
    size_t data_len = 0;
  };

  // Per-client connection state.
  struct ClientState {
    SocketHandle socket = kInvalidSocket;
    // Receive buffer for partial line accumulation.
    char recv_buf[kMaxMessageSize] = {};
    size_t recv_len = 0;
  };

  // The listener thread entry point.
  void ListenerLoop();

  // Parses a complete NDJSON line and enqueues an eval request.
  void ParseMessage(int client_index, const char* line, size_t len);

  // Processes an eval request on the main thread.
  void ProcessEval(lua_State* state, const EvalRequest& req);

  // Processes a state request on the main thread.
  void ProcessState(lua_State* state, const EvalRequest& req,
                    const Stats* stats);

  // Processes an inspect request on the main thread.
  void ProcessInspect(lua_State* state, const EvalRequest& req);

  // Processes a complete request on the main thread.
  void ProcessComplete(lua_State* state, const EvalRequest& req);

  uint16_t port_ = 9741;
  Allocator* allocator_ = nullptr;
  std::atomic<bool> running_{false};

  SocketHandle listen_socket_ = kInvalidSocket;
  std::thread listener_thread_;

  // Client connections (managed by listener thread, read by main for sends).
  ClientState clients_[kMaxClients] = {};

  // Eval queue: listener thread produces, main thread consumes.
  std::mutex eval_mu_;
  CircularBuffer<EvalRequest>* eval_queue_ = nullptr;

  // Response queue: main thread produces, listener thread consumes.
  std::mutex response_mu_;
  CircularBuffer<Response>* response_queue_ = nullptr;
};

}  // namespace G

#endif  // _GAME_REPL_H
