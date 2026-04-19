#pragma once
#ifndef _GAME_LUA_H
#define _GAME_LUA_H

#include <csetjmp>
#include <string_view>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "array.h"
#include "assets.h"
#include "clock.h"
#include "libraries/sqlite3.h"
#include "mat.h"
#include "stringlib.h"
#include "vec.h"

namespace G {

class LuaStackCheck {
 public:
  explicit LuaStackCheck(lua_State* s, const char* file, size_t line)
      : state_(s), file_(file), line_(line) {
    file_ = Basename(file_);
    start_ = lua_gettop(state_);
    end_ = start_;
  }

  ~LuaStackCheck() {
    CHECK(end_ == lua_gettop(state_), "Failed stack check at ", file_, ":",
          line_, " - ", end_, " vs ", lua_gettop(state_));
  }

 private:
  lua_State* const state_;
  int start_;
  int end_;
  std::string_view file_;
  size_t line_;
};

#define LUA_INTERNAL_ID_I1(x, y) x##y
#define LUA_INTERNAL_ID_I(x, y) INTERNAL_ID_I1(x, y)
#define LUA_INTERNAL_ID(x) INTERNAL_ID_I(x, __COUNTER__)

#define LUA_CHECK_STACK(state) \
  LuaStackCheck LUA_INTERNAL_ID(l)(state, __FILE__, __LINE__)

template <typename T>
constexpr const char* Typename() {
  return __PRETTY_FUNCTION__;
}

template <typename T>
class Registry {
 public:
  static void Register(lua_State* state, T* ptr) {
    lua_pushlightuserdata(state, static_cast<void*>(&kKey));
    lua_pushlightuserdata(state, static_cast<void*>(ptr));
    lua_settable(state, LUA_REGISTRYINDEX);
  }

  static T* Retrieve(lua_State* state) {
    lua_pushlightuserdata(state, (void*)&kKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    auto* result = static_cast<T*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (result == nullptr) {
      luaL_error(state, "Could not find a module for %s", Typename<T>());
      return nullptr;
    }
    return result;
  }

 private:
  inline static char kKey = 'k';
};

// Forward declare for the userdata name.
struct ByteBuffer;
struct Canvas;

template <typename T>
struct UserdataName;

#define USERDATA_ENTRY(type, name)                    \
  template <>                                         \
  struct UserdataName<type> {                         \
    inline static constexpr const char* kName = name; \
  }

USERDATA_ENTRY(FVec2, "fvec2");
USERDATA_ENTRY(FVec3, "fvec3");
USERDATA_ENTRY(FVec4, "fvec4");
USERDATA_ENTRY(FMat2x2, "fmat2x2");
USERDATA_ENTRY(FMat3x3, "fmat3x3");
USERDATA_ENTRY(FMat4x4, "fmat4x4");
USERDATA_ENTRY(DbAssets::Sprite, "asset_sprite_ptr");
USERDATA_ENTRY(ByteBuffer, "byte_buffer");
USERDATA_ENTRY(Canvas, "canvas");

#undef USERDATA_ENTRY

template <typename T, typename... Args>
T* NewUserdata(lua_State* state, Args... args) {
  auto* userdata = static_cast<T*>(lua_newuserdata(state, sizeof(T)));
  new (userdata) T(args...);
  luaL_getmetatable(state, UserdataName<T>::kName);
  lua_setmetatable(state, -2);
  return userdata;
}

template <typename T>
T* AsUserdata(lua_State* state, int index) {
  return reinterpret_cast<T*>(
      luaL_checkudata(state, index, UserdataName<T>::kName));
}

#define LUA_ERROR(state, ...)                                                \
  do {                                                                       \
    FixedStringBuffer<kMaxLogLineLength> _luaerror_buffer(kTruncating);      \
    _luaerror_buffer.Append(Basename(__FILE__), ":", __LINE__,               \
                            "]: ", ##__VA_ARGS__);                           \
    lua_pushlstring(state, _luaerror_buffer.str(), _luaerror_buffer.size()); \
    lua_error(state);                                                        \
    __builtin_unreachable();                                                 \
  } while (0);

inline std::string_view GetLuaString(lua_State* state, int index) {
  size_t len;
  const char* data = luaL_checklstring(state, index, &len);
  return std::string_view(data, len);
}

// Formats a userdata value at idx into buf. Tries __tostring first, falls
// back to __name, then "userdata". If show_ptr is true, appends ": 0xADDR"
// when __tostring is not available.
inline void FormatLuaUserdata(lua_State* L, int idx, StringBuffer* buf,
                              bool show_ptr = false) {
  if (lua_getmetatable(L, idx)) {
    lua_getfield(L, -1, "__tostring");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, idx);
      if (lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1)) {
        buf->Append(lua_tostring(L, -1));
        lua_pop(L, 2);
        return;
      }
      lua_pop(L, 1);
    } else {
      lua_pop(L, 1);
    }
    lua_getfield(L, -1, "__name");
    if (lua_isstring(L, -1)) {
      buf->Append(lua_tostring(L, -1));
      if (show_ptr) buf->AppendF(": %p", lua_topointer(L, idx));
    } else {
      buf->Append("userdata");
      if (show_ptr) buf->AppendF(": %p", lua_topointer(L, idx));
    }
    lua_pop(L, 2);
  } else {
    buf->Append("userdata");
    if (show_ptr) buf->AppendF(": %p", lua_topointer(L, idx));
  }
}

struct LuaApiFunctionArg {
  const char* name = {0};
  const char* docs = {0};
  const char* type = {0};
};

struct LuaApiFunctionArgList {
  size_t argc;
  LuaApiFunctionArg args[64];

  LuaApiFunctionArgList(std::initializer_list<LuaApiFunctionArg> a)
      : argc(a.size()) {
    size_t i = 0;
    for (const auto& arg : a) {
      if (i >= 64) break;
      args[i++] = arg;
    }
  }

  const LuaApiFunctionArg& operator[](size_t i) const { return args[i]; }
};

struct LuaApiFunction {
  const char* name = {0};
  const char* docstring = {0};
  LuaApiFunctionArgList args;
  LuaApiFunctionArgList returns;
  lua_CFunction func = nullptr;
};

struct LuaUserdataField {
  const char* name = {0};
  const char* type = {0};
  const char* docs = {0};
};

struct LuaUserdataOperator {
  const char* op = {0};            // LuaLS operator: "add", "sub", "mul", etc.
  const char* operand_type = {0};  // Right operand type (nullptr for unary)
  const char* return_type = {0};
};

struct LuaUserdataMethod {
  const char* name = {0};
  const char* docstring = {0};
  LuaApiFunctionArgList params;
  LuaApiFunctionArgList returns;
};

struct LuaUserdataType {
  const char* metatable_name = {0};
  const char* luals_alias = {0};
  const char* docstring = {0};
  const LuaUserdataField* fields = nullptr;
  size_t field_count = 0;
  const LuaUserdataMethod* methods = nullptr;
  size_t method_count = 0;
  const LuaUserdataOperator* operators = nullptr;
  size_t operator_count = 0;
};

// Static metadata for a Lua library, used for stub generation without a Lua
// runtime.  A single source file may contribute multiple libraries (e.g.
// system + clock) and multiple userdata types.
struct LuaLibraryDef {
  struct Library {
    const char* name;
    const LuaApiFunction* funcs;
    size_t count;
  };

  const Library* libraries;
  size_t library_count;
  const LuaUserdataType* types;
  size_t type_count;
};

// Writes LuaLS stub definitions to |output_path| from the given library defs.
void WriteLuaLSStubs(const char* output_path, const LuaLibraryDef* defs,
                     size_t def_count);

class Lua {
 public:
  Lua(Slice<const char*> args, sqlite3* db, DbAssets* assets,
      Allocator* allocator);
  ~Lua() { lua_close(state_); }

  template <typename T>
  void Register(T* t) {
    Registry<T>::Register(state_, t);
  }

  void LoadLibraries();

  // Generates LuaLS stub file (definitions/game.lua) from registered metadata.
  void GenerateLuaLSStubs(const char* output_path);

  void LoadScript(const DbAssets::Script& script);

  void LoadMain();

  void Init();

  void Update(float t, float dt);

  void Draw();

  void BuildCompilationCache();

  void FlushCompilationCache();

  bool LoadFromCache(std::string_view script_name, lua_State* state);

  void InsertIntoCache(std::string_view script_name, lua_State* state);

  static void LogValue(lua_State* state, int pos, int depth, StringBuffer* buf);

  // Handles events if callbacks are present
  void HandleKeypressed(int scancode);
  void HandleKeyreleased(int scancode);
  void HandleMousePressed(int button);
  void HandleMouseReleased(int button);
  void HandleMouseMoved(FVec2 pos, FVec2 delta);
  void HandleTextInput(std::string_view input);
  void HandleQuit();

  // Evaluates a Lua string and writes the result (or error) to output.
  // Returns true on success, false on error.
  bool EvalString(std::string_view code, StringBuffer* output);

  // Returns the underlying Lua state. Only safe to call from the main
  // thread between update and draw (e.g. debug UI rendering).
  lua_State* state() const { return state_; }

  void Stop() { stopped_ = true; }
  bool Stopped() const { return stopped_; }

  // Looks up `_Game.test_inputs`, creates a Lua coroutine bound to it, and
  // anchors it in the registry so it isn't collected. No-op if `test_inputs`
  // is missing or not a function. Must be called after `Init()`.
  void StartTestCoroutine();

  // Resumes the test coroutine for one step. Sets stopped_ + test_exit_code_
  // when the coroutine finishes (0 on success, 1 on error). No-op if no
  // coroutine is active.
  void ResumeTestCoroutine();

  // True if a test coroutine has been started and has not yet finished.
  bool TestCoroutineActive() const { return test_co_ != nullptr; }

  // Exit code reported when the test coroutine ends. 0 = success, 1 = error.
  int TestExitCode() const { return test_exit_code_; }

  Allocator* allocator() const { return allocator_; }

  // Checks whether there is a permanent error.
  bool Error(StringBuffer* buffer);
  bool HasError() { return !error_.empty(); }

  void ClearError() { error_.Clear(); }

  void SetError(std::string_view file, int line, std::string_view error);

  void Crash();

  double time() const { return t_; }
  double dt() const { return dt_; }

  // Returns the unscaled real elapsed time.
  double RealTime() const { return real_t_; }

  // Returns the unscaled real delta time.
  double RealDt() const { return real_dt_; }

  // Sets the time scale multiplier (0 = paused, 1 = normal, 2 = double).
  void SetTimeScale(float scale) { time_scale_ = scale; }

  // Returns the current time scale multiplier.
  float TimeScale() const { return time_scale_; }

  // Sets the real (unscaled) time and delta for the current frame.
  void SetRealTime(double real_t, double real_dt) {
    real_t_ = real_t;
    real_dt_ = real_dt;
  }

  size_t argc() const { return args_.size(); }
  std::string_view argv(size_t i) const { return args_[i]; }

  void RequestHotload() { hotload_requested_ = true; }

  void RunGc() {
    TIMER("GC");
    lua_gc(state_, LUA_GCCOLLECT, /*data=*/0);
  }

  bool HotloadRequested() {
    const bool result = hotload_requested_;
    hotload_requested_ = false;
    return result;
  }

  size_t MemoryUsage() {
    size_t mem_kb = lua_gc(state_, LUA_GCCOUNT, 0);
    size_t mem_b = lua_gc(state_, LUA_GCCOUNTB, 0);
    return Kilobytes(mem_kb) + mem_b;
  }

  int PackageLoader();

  friend void AddGraphicsLibrary(Lua* lua);
  friend void AddMathLibrary(Lua* lua);
  friend void AddRandomLibrary(Lua* lua);
  friend void AddSystemLibrary(Lua* lua);
  friend void AddInputLibrary(Lua* lua);
  friend void AddLogLibrary(Lua* lua);
  friend void AddPhysicsLibrary(Lua* lua);
  friend void AddSoundLibrary(Lua* lua);
  friend void AddBufferLibrary(Lua* lua);
  friend void AddFilesystemLibrary(Lua* lua);
  friend void AddJsonLibrary(Lua* lua);
  friend void AddByteBufferLibrary(Lua* lua);
  friend void AddAssetsLibrary(Lua* lua);
  friend void AddCollisionLibrary(Lua* lua);
  friend void AddCameraLibrary(Lua* lua);
  friend void AddTimerLibrary(Lua* lua);
  friend void AddTestLibrary(Lua* lua);
  friend void AddDataLibrary(Lua* lua);
  friend void AddNetworkLibrary(Lua* lua);

 private:
  int LoadLuaAsset(std::string_view filename, std::string_view script_contents,
                   int traceback_handler = INT_MAX);

  int LoadFennelAsset(std::string_view filename,
                      std::string_view script_contents,
                      int traceback_handler = INT_MAX);

  // Compiles the fennel asset and leaves the result in the top of the Lua
  // stack.
  bool CompileFennelAsset(std::string_view filename,
                          std::string_view script_contents,
                          int traceback_handler = INT_MAX);

  void LoadAssets();
  void LoadMetatable(const char* metatable_name, const luaL_Reg* registers,
                     size_t register_count);

  void SetPackagePreload(std::string_view filename);

  void* Alloc(void* ptr, size_t osize, size_t nsize);

  static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<Lua*>(ud)->Alloc(ptr, osize, nsize);
  }

  void AddLibrary(const char* name, Slice<const luaL_Reg> funcs);
  void AddLibraryWithMetadata(const char* name,
                              Slice<const LuaApiFunction> funcs);

  template <size_t N>
  void AddLibrary(const char* name, const luaL_Reg (&funcs)[N]) {
    AddLibrary(name, Slice<const luaL_Reg>(funcs, N));
  }

  template <size_t N>
  void AddLibrary(const char* name, const LuaApiFunction (&funcs)[N]) {
    AddLibraryWithMetadata(name, Slice<const LuaApiFunction>(funcs, N));
  }

  int StackTop() { return lua_gettop(state_); }

  Slice<const char*> args_;

  lua_State* state_ = nullptr;
  bool stopped_ = false;

  int traceback_handler_;

  Allocator* allocator_;
  sqlite3* db_;
  DbAssets* assets_;

  struct RegisteredLibrary {
    const char* name;
    const LuaApiFunction* funcs;
    size_t count;
  };

  static constexpr size_t kMaxLibraries = 32;
  RegisteredLibrary registered_libraries_[kMaxLibraries];
  size_t registered_library_count_ = 0;

  void RegisterUserdataType(const LuaUserdataType& type);

  static constexpr size_t kMaxUserdataTypes = 16;
  LuaUserdataType registered_types_[kMaxUserdataTypes];
  size_t registered_type_count_ = 0;

  CmdBuffer error_{kTruncating};
  std::jmp_buf on_error_buf_;

  struct CachedScript {
    DbAssets::ChecksumType checksum;
    std::string_view contents;
  };

  struct Script {
    enum Language { kLuaScript, kFennelScript };

    Language language;
    std::string_view name;
    std::string_view contents;
  };

  Dictionary<Script*> scripts_by_name_;
  FixedArray<Script> scripts_;
  Dictionary<CachedScript> compilation_cache_;

  double t_ = 0;
  double dt_ = 0;
  double real_t_ = 0;
  double real_dt_ = 0;
  float time_scale_ = 1.0f;

  bool hotload_requested_ = false;

  // Coroutine running _Game.test_inputs in test mode. nullptr when test
  // mode is disabled or after the coroutine has finished or errored.
  lua_State* test_co_ = nullptr;
  // Registry reference that keeps test_co_ alive across GC cycles. Released
  // (back to LUA_NOREF) when the coroutine finishes.
  int test_co_ref_ = LUA_NOREF;
  // Exit code reported by RunGame when --test is used: 0 on coroutine
  // success, 1 if it errored or the engine quit before it finished.
  int test_exit_code_ = 0;
  // Frames remaining before the next ResumeTestCoroutine actually resumes.
  // Set from the value passed to lua_yield (e.g. by G.test.wait_frames).
  int test_co_wait_frames_ = 0;
  // True until the first lua_resume call. The first resume passes `self`
  // (the _Game module) as an argument; subsequent resumes pass none.
  bool test_co_first_resume_ = true;
};

}  // namespace G

// Compile-time checked wrapper for Lua::LoadMetatable. The template overload
// can't static_assert on the array contents because function parameters are
// never constant expressions, but at the macro call-site the array name IS
// a constant expression (assuming constexpr arrays, which all callers use).
#define LOAD_METATABLE(lua_ptr, mt_name, arr)                                  \
  do {                                                                         \
    static_assert(                                                             \
        std::size(arr) == 0 || (arr)[std::size(arr) - 1].name != nullptr, #arr \
        " must not end with {nullptr, nullptr} — "                             \
        "LoadMetatable uses array size, not a sentinel");                      \
    (lua_ptr)->LoadMetatable(mt_name, arr, std::size(arr));                    \
  } while (0)

#endif  // _GAME_LUA_H
