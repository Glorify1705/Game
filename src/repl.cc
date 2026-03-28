#include "repl.h"

#include <cstring>

#include "clock.h"
#include "json.h"
#include "logging.h"
#include "lua.h"
#include "stats.h"
#include "thread.h"

namespace G {
namespace {

// Escapes a string for inclusion in a JSON string value. Writes to dst and
// returns the number of bytes written (not including null terminator).
size_t JsonEscape(const char* src, size_t src_len, char* dst,
                  size_t dst_capacity) {
  size_t out = 0;
  for (size_t i = 0; i < src_len && out + 6 < dst_capacity; ++i) {
    char c = src[i];
    switch (c) {
      case '"':
        dst[out++] = '\\';
        dst[out++] = '"';
        break;
      case '\\':
        dst[out++] = '\\';
        dst[out++] = '\\';
        break;
      case '\n':
        dst[out++] = '\\';
        dst[out++] = 'n';
        break;
      case '\r':
        dst[out++] = '\\';
        dst[out++] = 'r';
        break;
      case '\t':
        dst[out++] = '\\';
        dst[out++] = 't';
        break;
      case '\b':
        dst[out++] = '\\';
        dst[out++] = 'b';
        break;
      case '\f':
        dst[out++] = '\\';
        dst[out++] = 'f';
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // Control character: emit \u00XX.
          int written = snprintf(dst + out, dst_capacity - out, "\\u%04x",
                                 static_cast<unsigned char>(c));
          if (written > 0) out += written;
        } else {
          dst[out++] = c;
        }
        break;
    }
  }
  if (out < dst_capacity) dst[out] = '\0';
  return out;
}

// Builds a JSON result response line into buf. Returns bytes written.
size_t BuildResultResponse(int id, bool ok, const char* value, size_t value_len,
                           char* buf, size_t buf_size) {
  // Escape the value for JSON.
  char escaped[4096];
  size_t escaped_len = JsonEscape(value, value_len, escaped, sizeof(escaped));

  int written;
  if (ok) {
    written = snprintf(buf, buf_size,
                       "{\"id\":%d,\"type\":\"result\",\"ok\":true,"
                       "\"value\":\"%.*s\"}\n",
                       id, (int)escaped_len, escaped);
  } else {
    written = snprintf(buf, buf_size,
                       "{\"id\":%d,\"type\":\"result\",\"ok\":false,"
                       "\"error\":\"%.*s\"}\n",
                       id, (int)escaped_len, escaped);
  }
  return written > 0 ? static_cast<size_t>(written) : 0;
}

// Builds a JSON output message line (for captured print output).
size_t BuildOutputResponse(int id, const char* text, size_t text_len, char* buf,
                           size_t buf_size) {
  char escaped[4096];
  size_t escaped_len = JsonEscape(text, text_len, escaped, sizeof(escaped));
  int written = snprintf(buf, buf_size,
                         "{\"id\":%d,\"type\":\"output\",\"text\":\"%.*s\"}\n",
                         id, (int)escaped_len, escaped);
  return written > 0 ? static_cast<size_t>(written) : 0;
}

// Context for print() capture during eval.
struct PrintCapture {
  int request_id = 0;
  int client_index = 0;
  ReplServer* server = nullptr;
};

// Thread-local pointer to the active print capture context. Set only during
// eval on the main thread.
thread_local PrintCapture* g_print_capture = nullptr;

// Replacement print() function that captures output for the REPL.
int ReplPrint(lua_State* L) {
  int nargs = lua_gettop(L);
  FixedStringBuffer<4096> buf;
  buf.AllowTruncation();
  for (int i = 1; i <= nargs; ++i) {
    if (i > 1) buf.Append("\t");
    if (lua_isstring(L, i)) {
      buf.Append(lua_tostring(L, i));
    } else if (lua_isnil(L, i)) {
      buf.Append("nil");
    } else if (lua_isboolean(L, i)) {
      buf.Append(lua_toboolean(L, i) ? "true" : "false");
    } else if (lua_isnumber(L, i)) {
      buf.Append(lua_tonumber(L, i));
    } else {
      buf.Append(lua_typename(L, lua_type(L, i)));
    }
  }
  buf.Append("\n");

  // Send as output message if we have a capture context.
  if (g_print_capture && g_print_capture->server) {
    char response[ReplServer::kMaxMessageSize];
    size_t len = BuildOutputResponse(g_print_capture->request_id, buf.str(),
                                     buf.size(), response, sizeof(response));
    if (len > 0) {
      g_print_capture->server->EnqueueResponse(g_print_capture->client_index,
                                               g_print_capture->request_id,
                                               response, len);
    }
  }
  return 0;
}

// Extracts a string field from a parsed JSON object. Returns empty
// string_view if the field is missing or not a string.
std::string_view GetJsonString(const JsonValue& json, std::string_view key) {
  const auto& val = json[key];
  if (val.type == JsonValue::kString) return val.string_val;
  return {};
}

// Extracts an integer field from a parsed JSON object. Returns 0 if missing.
int GetJsonInt(const JsonValue& json, std::string_view key) {
  const auto& val = json[key];
  if (val.type == JsonValue::kNumber) return static_cast<int>(val.number_val);
  return 0;
}

}  // namespace

ReplServer::ReplServer() = default;

ReplServer::~ReplServer() { Stop(); }

void ReplServer::Init(uint16_t port, Allocator* allocator) {
  port_ = port;
  allocator_ = allocator;
  eval_queue_ =
      allocator_->New<CircularBuffer<EvalRequest>>(kMaxQueueSize, allocator_);
  response_queue_ =
      allocator_->New<CircularBuffer<Response>>(kMaxQueueSize, allocator_);
}

void ReplServer::Start() {
  if (running_) return;

  InitSockets();
  listen_socket_ = CreateListenSocket(port_);
  if (listen_socket_ == kInvalidSocket) {
    ELOG("REPL: failed to create listen socket on port ", port_);
    return;
  }

  running_ = true;
  listener_thread_ = std::thread([this] { ListenerLoop(); });
  LOG("REPL server listening on 127.0.0.1:", port_);
}

void ReplServer::Stop() {
  if (!running_) return;
  running_ = false;
  if (listener_thread_.joinable()) {
    listener_thread_.join();
  }

  // Close all client connections.
  for (int i = 0; i < kMaxClients; ++i) {
    if (clients_[i].socket != kInvalidSocket) {
      CloseSocket(clients_[i].socket);
      clients_[i].socket = kInvalidSocket;
      clients_[i].recv_len = 0;
    }
  }

  CloseSocket(listen_socket_);
  listen_socket_ = kInvalidSocket;
  ShutdownSockets();
  LOG("REPL server stopped");
}

void ReplServer::ListenerLoop() {
  while (running_) {
    // Build poll set: listen socket + connected clients.
    SocketHandle poll_sockets[1 + kMaxClients];
    bool poll_ready[1 + kMaxClients];
    int poll_count = 0;

    poll_sockets[poll_count] = listen_socket_;
    poll_count++;

    int client_poll_map[kMaxClients];
    for (int i = 0; i < kMaxClients; ++i) {
      client_poll_map[i] = -1;
      if (clients_[i].socket != kInvalidSocket) {
        client_poll_map[i] = poll_count;
        poll_sockets[poll_count] = clients_[i].socket;
        poll_count++;
      }
    }

    int ready = PollSockets(poll_sockets, poll_ready, poll_count,
                            /*timeout_ms=*/50);
    if (ready < 0) {
      SleepMs(10);
      continue;
    }

    // Check for new connections.
    if (poll_ready[0]) {
      SocketHandle new_client = AcceptConnection(listen_socket_);
      if (new_client != kInvalidSocket) {
        int slot = -1;
        for (int i = 0; i < kMaxClients; ++i) {
          if (clients_[i].socket == kInvalidSocket) {
            slot = i;
            break;
          }
        }
        if (slot >= 0) {
          clients_[slot].socket = new_client;
          clients_[slot].recv_len = 0;
          DLOG("REPL: client ", slot, " connected");
        } else {
          // No free slot, reject.
          const char* msg =
              "{\"type\":\"error\",\"error\":\"too many connections\"}\n";
          SocketSend(new_client, msg, static_cast<int>(strlen(msg)));
          CloseSocket(new_client);
        }
      }
    }

    // Read from connected clients.
    for (int i = 0; i < kMaxClients; ++i) {
      if (clients_[i].socket == kInvalidSocket) continue;
      int poll_idx = client_poll_map[i];
      if (poll_idx < 0 || !poll_ready[poll_idx]) continue;

      auto& client = clients_[i];
      size_t space = kMaxMessageSize - client.recv_len - 1;
      if (space == 0) {
        // Buffer full with no newline, discard.
        client.recv_len = 0;
        continue;
      }

      int bytes = SocketRecv(client.socket, client.recv_buf + client.recv_len,
                             static_cast<int>(space));
      if (bytes == 0) {
        // Disconnected.
        DLOG("REPL: client ", i, " disconnected");
        CloseSocket(client.socket);
        client.socket = kInvalidSocket;
        client.recv_len = 0;
        continue;
      }
      if (bytes < 0) continue;  // EAGAIN

      client.recv_len += bytes;

      // Extract complete lines (NDJSON: newline-delimited).
      char* buf = client.recv_buf;
      size_t len = client.recv_len;
      while (true) {
        char* nl = static_cast<char*>(memchr(buf, '\n', len));
        if (nl == nullptr) break;
        size_t line_len = nl - buf;
        if (line_len > 0) {
          ParseMessage(i, buf, line_len);
        }
        size_t consumed = line_len + 1;
        buf += consumed;
        len -= consumed;
      }
      // Move remaining partial line to start of buffer.
      if (buf != client.recv_buf) {
        if (len > 0) memmove(client.recv_buf, buf, len);
        client.recv_len = len;
      }
    }

    // Drain response queue and send to clients.
    {
      LockMutex lock(response_mu_);
      while (!response_queue_->empty()) {
        Response resp = response_queue_->Pop();
        if (resp.client_index >= 0 && resp.client_index < kMaxClients &&
            clients_[resp.client_index].socket != kInvalidSocket) {
          SocketSend(clients_[resp.client_index].socket, resp.data,
                     static_cast<int>(resp.data_len));
        }
      }
    }
  }
}

void ReplServer::ParseMessage(int client_index, const char* line, size_t len) {
  // Parse JSON using a small scratch arena.
  StaticAllocator<Kilobytes(8)> scratch;
  // Copy line to null-terminate it.
  char* line_copy = static_cast<char*>(scratch.Alloc(len + 1, 1));
  if (!line_copy) return;
  memcpy(line_copy, line, len);
  line_copy[len] = '\0';

  auto result = ParseJson(std::string_view(line_copy, len), &scratch);
  if (result.is_error()) {
    DLOG("REPL: failed to parse JSON from client ", client_index);
    return;
  }

  const JsonValue& json = *result.value();
  if (!json.IsObject()) return;

  int id = GetJsonInt(json, "id");
  auto op_str = GetJsonString(json, "op");

  EvalRequest req;
  req.client_index = client_index;
  req.id = id;

  if (op_str == "eval") {
    req.op = EvalRequest::kEval;
    auto code = GetJsonString(json, "code");
    size_t copy_len =
        code.size() < sizeof(req.code) - 1 ? code.size() : sizeof(req.code) - 1;
    memcpy(req.code, code.data(), copy_len);
    req.code[copy_len] = '\0';
    req.code_len = copy_len;
  } else if (op_str == "state") {
    req.op = EvalRequest::kState;
  } else if (op_str == "inspect") {
    req.op = EvalRequest::kInspect;
    auto expr = GetJsonString(json, "expr");
    size_t copy_len =
        expr.size() < sizeof(req.code) - 1 ? expr.size() : sizeof(req.code) - 1;
    memcpy(req.code, expr.data(), copy_len);
    req.code[copy_len] = '\0';
    req.code_len = copy_len;
  } else if (op_str == "complete") {
    req.op = EvalRequest::kComplete;
    auto prefix = GetJsonString(json, "prefix");
    size_t copy_len = prefix.size() < sizeof(req.code) - 1
                          ? prefix.size()
                          : sizeof(req.code) - 1;
    memcpy(req.code, prefix.data(), copy_len);
    req.code[copy_len] = '\0';
    req.code_len = copy_len;
  } else {
    DLOG("REPL: unknown op '", op_str, "' from client ", client_index);
    char resp[256];
    int n = snprintf(resp, sizeof(resp),
                     "{\"id\":%d,\"type\":\"result\",\"ok\":false,"
                     "\"error\":\"unknown op\"}\n",
                     id);
    if (n > 0) {
      LockMutex lock(response_mu_);
      if (!response_queue_->full()) {
        Response r;
        r.client_index = client_index;
        memcpy(r.data, resp, n);
        r.data_len = n;
        response_queue_->Push(r);
      }
    }
    return;
  }

  LockMutex lock(eval_mu_);
  if (!eval_queue_->full()) {
    eval_queue_->Push(req);
  }
}

void ReplServer::EnqueueResponse(int client_index, int id, const char* data,
                                 size_t data_len) {
  LockMutex lock(response_mu_);
  if (response_queue_->full()) return;
  Response r;
  r.client_index = client_index;
  size_t copy_len = data_len < sizeof(r.data) ? data_len : sizeof(r.data) - 1;
  memcpy(r.data, data, copy_len);
  r.data_len = copy_len;
  response_queue_->Push(r);
}

void ReplServer::ProcessQueue(lua_State* state, const Stats* stats) {
  // Drain all pending eval requests.
  for (;;) {
    EvalRequest req;
    {
      LockMutex lock(eval_mu_);
      if (eval_queue_->empty()) break;
      req = eval_queue_->Pop();
    }

    switch (req.op) {
      case EvalRequest::kEval:
        ProcessEval(state, req);
        break;
      case EvalRequest::kState:
        ProcessState(state, req, stats);
        break;
      case EvalRequest::kInspect:
        ProcessInspect(state, req);
        break;
      case EvalRequest::kComplete:
        ProcessComplete(state, req);
        break;
    }
  }
}

void ReplServer::ProcessEval(lua_State* state, const EvalRequest& req) {
  int top = lua_gettop(state);

  // Install print() capture.
  PrintCapture capture;
  capture.request_id = req.id;
  capture.client_index = req.client_index;
  capture.server = this;
  g_print_capture = &capture;

  // Save original print function.
  lua_getglobal(state, "print");
  int original_print_ref = luaL_ref(state, LUA_REGISTRYINDEX);

  // Install capture print.
  lua_pushcfunction(state, ReplPrint);
  lua_setglobal(state, "print");

  // Try "return <code>" first (expression evaluation).
  FixedStringBuffer<4096> return_code("return ");
  return_code.AllowTruncation();
  return_code.Append(std::string_view(req.code, req.code_len));

  int load_result =
      luaL_loadbuffer(state, return_code.str(), return_code.size(), "=repl");
  if (load_result != 0) {
    // Syntax error with "return" prefix, try as-is.
    lua_pop(state, 1);
    load_result = luaL_loadbuffer(state, req.code, req.code_len, "=repl");
  }

  char response[kMaxMessageSize];
  size_t response_len = 0;

  if (load_result != 0) {
    // Compile error.
    const char* err = lua_tostring(state, -1);
    if (!err) err = "unknown compile error";
    response_len = BuildResultResponse(req.id, false, err, strlen(err),
                                       response, sizeof(response));
    lua_pop(state, 1);
  } else {
    // Execute.
    int call_result = lua_pcall(state, 0, LUA_MULTRET, 0);
    if (call_result != 0) {
      // Runtime error.
      const char* err = lua_tostring(state, -1);
      if (!err) err = "unknown runtime error";
      response_len = BuildResultResponse(req.id, false, err, strlen(err),
                                         response, sizeof(response));
      lua_pop(state, 1);
    } else {
      // Collect return values.
      int nresults = lua_gettop(state) - top;
      if (nresults == 0) {
        response_len = BuildResultResponse(req.id, true, "nil", 3, response,
                                           sizeof(response));
      } else {
        FixedStringBuffer<4096> result_buf;
        result_buf.AllowTruncation();
        for (int i = 0; i < nresults; ++i) {
          if (i > 0) result_buf.Append("\t");
          int idx = top + 1 + i;
          Lua::LogValue(state, idx, /*depth=*/0, &result_buf);
        }
        response_len =
            BuildResultResponse(req.id, true, result_buf.str(),
                                result_buf.size(), response, sizeof(response));
        lua_pop(state, nresults);
      }
    }
  }

  // Restore original print function.
  lua_rawgeti(state, LUA_REGISTRYINDEX, original_print_ref);
  lua_setglobal(state, "print");
  luaL_unref(state, LUA_REGISTRYINDEX, original_print_ref);

  g_print_capture = nullptr;

  if (response_len > 0) {
    EnqueueResponse(req.client_index, req.id, response, response_len);
  }
}

void ReplServer::ProcessState(lua_State* state, const EvalRequest& req,
                              const Stats* stats) {
  double fps = 0;
  if (stats && stats->avg() > 0) {
    fps = 1000.0 / stats->avg();
  }

  size_t lua_mem_kb = lua_gc(state, LUA_GCCOUNT, 0);
  size_t lua_mem_b = lua_gc(state, LUA_GCCOUNTB, 0);
  double lua_mem_mb =
      static_cast<double>(lua_mem_kb * 1024 + lua_mem_b) / (1024.0 * 1024.0);

  char response[kMaxMessageSize];
  int n = snprintf(response, sizeof(response),
                   "{\"id\":%d,\"type\":\"result\",\"ok\":true,"
                   "\"value\":{\"fps\":%.1f,\"lua_memory_mb\":%.2f,"
                   "\"frame_time_ms\":%.2f}}\n",
                   req.id, fps, lua_mem_mb, stats ? stats->avg() : 0.0);

  if (n > 0) {
    EnqueueResponse(req.client_index, req.id, response, n);
  }
}

void ReplServer::ProcessInspect(lua_State* state, const EvalRequest& req) {
  int top = lua_gettop(state);

  // Evaluate the expression to get the value on the stack.
  FixedStringBuffer<4096> code("return ");
  code.AllowTruncation();
  code.Append(std::string_view(req.code, req.code_len));

  char response[kMaxMessageSize];
  size_t response_len = 0;

  int load_result = luaL_loadbuffer(state, code.str(), code.size(), "=inspect");
  if (load_result != 0) {
    const char* err = lua_tostring(state, -1);
    if (!err) err = "invalid expression";
    response_len = BuildResultResponse(req.id, false, err, strlen(err),
                                       response, sizeof(response));
    lua_pop(state, 1);
  } else {
    int call_result = lua_pcall(state, 0, 1, 0);
    if (call_result != 0) {
      const char* err = lua_tostring(state, -1);
      if (!err) err = "runtime error";
      response_len = BuildResultResponse(req.id, false, err, strlen(err),
                                         response, sizeof(response));
      lua_pop(state, 1);
    } else {
      FixedStringBuffer<4096> buf;
      buf.AllowTruncation();
      Lua::LogValue(state, -1, /*depth=*/0, &buf);
      response_len = BuildResultResponse(req.id, true, buf.str(), buf.size(),
                                         response, sizeof(response));
      lua_pop(state, 1);
    }
  }

  DCHECK(lua_gettop(state) == top, "stack leak in ProcessInspect");

  if (response_len > 0) {
    EnqueueResponse(req.client_index, req.id, response, response_len);
  }
}

void ReplServer::ProcessComplete(lua_State* state, const EvalRequest& req) {
  std::string_view prefix(req.code, req.code_len);

  // Split prefix into table path and partial field name.
  // e.g., "G.graphics.d" -> table path = "G.graphics", partial = "d"
  std::string_view table_path;
  std::string_view partial = prefix;

  size_t last_dot = prefix.size();
  for (size_t i = prefix.size(); i > 0; --i) {
    if (prefix[i - 1] == '.' || prefix[i - 1] == ':') {
      last_dot = i - 1;
      break;
    }
  }

  int top = lua_gettop(state);

  if (last_dot < prefix.size()) {
    table_path = prefix.substr(0, last_dot);
    partial = prefix.substr(last_dot + 1);

    // Resolve the table path by walking globals.
    // Split table_path by '.' and traverse.
    size_t pos = 0;
    bool first = true;
    while (pos <= table_path.size()) {
      size_t dot = pos;
      while (dot < table_path.size() && table_path[dot] != '.') ++dot;
      std::string_view segment = table_path.substr(pos, dot - pos);

      if (first) {
        lua_getglobal(state, "");
        lua_pop(state, 1);  // discard
        // Push the first segment as a global.
        char seg_buf[256];
        size_t seg_len = segment.size() < sizeof(seg_buf) - 1
                             ? segment.size()
                             : sizeof(seg_buf) - 1;
        memcpy(seg_buf, segment.data(), seg_len);
        seg_buf[seg_len] = '\0';
        lua_getglobal(state, seg_buf);
        if (lua_isnil(state, -1)) {
          lua_pop(state, 1);
          goto send_empty;
        }
        first = false;
      } else {
        if (!lua_istable(state, -1)) {
          lua_pop(state, 1);
          goto send_empty;
        }
        char seg_buf[256];
        size_t seg_len = segment.size() < sizeof(seg_buf) - 1
                             ? segment.size()
                             : sizeof(seg_buf) - 1;
        memcpy(seg_buf, segment.data(), seg_len);
        seg_buf[seg_len] = '\0';
        lua_getfield(state, -1, seg_buf);
        // Remove the parent table.
        lua_remove(state, -2);
        if (lua_isnil(state, -1)) {
          lua_pop(state, 1);
          goto send_empty;
        }
      }
      pos = dot + 1;
    }
  } else {
    // No dot: complete from globals.
    lua_pushvalue(state, LUA_GLOBALSINDEX);
  }

  // Now the table to complete from is on top of the stack.
  if (lua_istable(state, -1)) {
    FixedStringBuffer<4096> completions;
    completions.AllowTruncation();
    completions.Append("[");
    bool first_match = true;

    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
      if (lua_type(state, -2) == LUA_TSTRING) {
        const char* key = lua_tostring(state, -2);
        size_t key_len = strlen(key);
        // Check if key starts with partial.
        if (key_len >= partial.size() &&
            memcmp(key, partial.data(), partial.size()) == 0) {
          if (!first_match) completions.Append(",");
          first_match = false;
          char escaped_key[512];
          size_t ek_len =
              JsonEscape(key, key_len, escaped_key, sizeof(escaped_key));
          completions.Append("\"", std::string_view(escaped_key, ek_len), "\"");
        }
      }
      lua_pop(state, 1);  // pop value, keep key
    }
    completions.Append("]");
    lua_pop(state, 1);  // pop the table

    char response[kMaxMessageSize];
    int n =
        snprintf(response, sizeof(response),
                 "{\"id\":%d,\"type\":\"result\",\"ok\":true,\"value\":%s}\n",
                 req.id, completions.str());
    if (n > 0) {
      EnqueueResponse(req.client_index, req.id, response, n);
    }
    DCHECK(lua_gettop(state) == top, "stack leak in ProcessComplete");
    return;
  }

  lua_settop(state, top);

send_empty: {
  lua_settop(state, top);
  char response[kMaxMessageSize];
  int n = snprintf(response, sizeof(response),
                   "{\"id\":%d,\"type\":\"result\",\"ok\":true,"
                   "\"value\":[]}\n",
                   req.id);
  if (n > 0) {
    EnqueueResponse(req.client_index, req.id, response, n);
  }
}
}

}  // namespace G
