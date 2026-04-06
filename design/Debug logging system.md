---
status: implemented
tags: [logging, debugging]
---

# Debug Logging System

## Problem

The engine has a minimal logging system with only two levels: `kInfo` and `kFatal`. There is no way to add debug or trace log lines that are compiled out in release builds. This means developers must either:

1. **Avoid adding debug logs** — losing valuable diagnostic output during development.
2. **Add `LOG()` calls and remove them later** — tedious and error-prone.
3. **Use `DCHECK` as a proxy** — assertions are not log lines; they crash when false.

Other engines solve this with multi-level logging that eliminates debug output at compile time, so instrumented code has **zero cost** in release builds — no string literals in the binary, no formatting work, no branch.

### Current architecture

```
LOG("pos = ", player_pos)
  → ::G::Log(__FILE__, __LINE__, "pos = ", player_pos)
    → FixedStringBuffer<511> buf("[file.cc:42] ")
      buf.Append("pos = ", player_pos)
      GetLogSink()(LogLevel::kInfo, buf.str())
```

Every `LOG()` call formats unconditionally. The sink function (`LogToSDL` in practice) always receives the formatted string. There is no short-circuit path.

| What exists | What's missing |
|-------------|---------------|
| `LOG(...)` — always emits | Debug/trace levels that compile out |
| `CHECK`/`DCHECK` — assertions | Per-subsystem filtering |
| `DIE(...)` — fatal crash | Runtime level control in dev builds |
| `DONOTSUBMIT` — alias for LOG | Structured categories/channels |
| Pluggable `LogSink` function | Multiple simultaneous sinks |

## Survey of other engines

### Unreal Engine — `UE_LOG` and categories

Unreal's macro system is the most mature category-based approach:

```cpp
// Declaration (header)
DECLARE_LOG_CATEGORY_EXTERN(LogPhysics, Log, /*CompileTimeVerbosity=*/All);

// Definition (source)
DEFINE_LOG_CATEGORY(LogPhysics);

// Usage
UE_LOG(LogPhysics, Verbose, TEXT("Body %s awake"), *Name);
```

The third parameter to `DECLARE_LOG_CATEGORY_EXTERN` sets a **compile-time verbosity ceiling**. Any `UE_LOG` call more verbose than this threshold is eliminated entirely by the compiler — no string data in the binary, no formatting code emitted. In shipping builds, categories are typically set to `Warning`, so all `Log`/`Verbose`/`VeryVerbose` calls vanish.

A blog series ("A Better UE_LOG" by Laura) demonstrates using `if constexpr(false)` for even stronger elimination:

```cpp
#if NO_LOGGING
#define IF_MY_TRACE_ENABLED() if constexpr (false)
#else
#define IF_MY_TRACE_ENABLED() if (GThisFileCVar.GetValueOnAnyThread() >= 2)
#endif
```

The `if constexpr (false)` form is the strongest: the compiler discards the entire block during compilation.

### Godot — error macros and deferred evaluation

Godot wraps error output in macros that defer argument evaluation:

```cpp
#define print_verbose(m_text)         \
    {                                 \
        if (is_print_verbose_enabled()) { \
            print_line(m_text);       \
        }                             \
    }
```

The key insight: `m_text` is only evaluated when verbose mode is active. No formatting work occurs on the disabled path. Error macros use `unlikely()` branch hints and `_NO_INLINE_` on the error-printing functions to keep cold code out of the instruction cache:

```cpp
#define ERR_FAIL_COND(m_cond)                                    \
    if (unlikely(m_cond)) {                                      \
        _err_print_error(FUNCTION_STR, __FILE__, __LINE__,       \
            "Condition \"" _STR(m_cond) "\" is true.");           \
        return;                                                   \
    } else                                                        \
        ((void)0)
```

Godot also uses an error handler chain (linked list of callback structs) so multiple sinks (editor panel, log file, stdout) can receive errors independently.

### id Tech (Quake 3) — `Com_DPrintf` and cvars

Quake 3 uses a runtime check against a cvar:

```c
void Com_DPrintf(const char *fmt, ...) {
    if (!com_developer || !com_developer->integer) return;
    // ... va_list, vsnprintf, Com_Printf ...
}
```

This is the simplest approach but has a flaw: `va_list` setup and argument evaluation happen before the early return. The format arguments are pushed onto the stack unconditionally. It's a runtime filter, not a compile-time one. For the engine's use case, this is insufficient — we want zero-cost elimination in release.

The cvar system itself is worth noting: `com_speeds` prints per-frame timing, `com_showtrace` enables collision debug visualization, `com_logfile` controls file output (0=off, 1=buffered, 2=flush-per-write). All toggled at runtime via the console.

### Source Engine — ConVar flags

Source extends id Tech's cvars with flag-based access control:

- `FCVAR_DEVELOPMENTONLY` — stripped from release builds entirely (compile-time)
- `FCVAR_CHEAT` — only active when `sv_cheats 1` (runtime)
- Constructor-based registration with callbacks: `ConVar sv_cheats("sv_cheats", "0", FCVAR_NOTIFY, "Allow cheats", SV_CheatsChanged_f);`

### Bevy — `tracing` crate

Bevy uses Rust's `tracing` with per-module filter strings:

```
"info,wgpu_core=warn,mygame::physics=debug"
```

This is the most flexible runtime filtering but relies on string parsing at startup. The structured span model (begin/end timing around operations) goes beyond flat log lines into execution tracing.

### Game-specific libraries

- **logog** (Logger Optimized for Games): All logging compiles to no-ops when the level is disabled. The do-not-log path requires exactly one boolean comparison.
- **BqLog** (Tencent, used in Honor of Kings): Binary structured logging — serializes format string pointer + raw argument bytes. Formatting happens on a background thread or host machine. Maximizes hot-path throughput.
- **binlog** (Morgan Stanley): Similar binary approach. Store structured binary, format only when reading.

## Design

### Log levels

Extend `LogLevel` from two values to six:

```cpp
enum class LogLevel : uint8_t {
    kFatal,   // Unrecoverable — crash after logging
    kError,   // Wrong but recoverable
    kWarn,    // Suspicious but possibly fine
    kInfo,    // Normal operational messages (current LOG behavior)
    kDebug,   // Developer diagnostics — compiled out in release
    kTrace,   // High-frequency diagnostics — compiled out in release
};
```

`kInfo` remains the default. Everything at or below `kInfo` is always compiled in (matching current behavior). `kDebug` and `kTrace` are eliminated at compile time in release builds.

### Compile-time elimination

The core mechanism: a `constexpr` threshold that the compiler evaluates at compile time.

```cpp
#ifdef NDEBUG
inline constexpr LogLevel kCompileTimeLogLevel = LogLevel::kInfo;
#else
inline constexpr LogLevel kCompileTimeLogLevel = LogLevel::kTrace;
#endif

#define LOG_AT(level, ...)                                              \
    do {                                                                \
        if constexpr ((level) <= kCompileTimeLogLevel) {                \
            ::G::LogAt((level), __FILE__, __LINE__, ##__VA_ARGS__);     \
        }                                                               \
    } while (0)
```

When `NDEBUG` is defined, `kCompileTimeLogLevel` is `kInfo`. A call like `LOG_AT(LogLevel::kDebug, ...)` expands to `if constexpr (kDebug <= kInfo)` which is `if constexpr (false)` — the compiler discards the entire block. No string literals, no formatting code, no branch in the emitted binary.

### Macro API

```cpp
// Existing behavior, unchanged:
#define LOG(...)    LOG_AT(LogLevel::kInfo, __VA_ARGS__)
#define DIE(...)    LOG_AT(LogLevel::kFatal, __VA_ARGS__)

// New:
#define ELOG(...)   LOG_AT(LogLevel::kError, __VA_ARGS__)
#define WLOG(...)   LOG_AT(LogLevel::kWarn, __VA_ARGS__)
#define DLOG(...)   LOG_AT(LogLevel::kDebug, __VA_ARGS__)
#define TLOG(...)   LOG_AT(LogLevel::kTrace, __VA_ARGS__)
```

`DLOG` and `TLOG` are the primary additions. They behave identically to `LOG` in debug builds and compile to nothing in release.

### Per-subsystem filtering (runtime, debug builds only)

In debug builds, add an optional runtime filter so developers can silence noisy subsystems without recompiling:

```cpp
enum class LogChannel : uint8_t {
    kGeneral,
    kGraphics,
    kPhysics,
    kAudio,
    kInput,
    kAssets,
    kLua,
    kCount,
};

// Runtime level per channel (debug builds only)
inline LogLevel g_channel_levels[static_cast<size_t>(LogChannel::kCount)] = {
    LogLevel::kTrace, LogLevel::kTrace, /* ... */
};

#define CLOG(channel, level, ...)                                          \
    do {                                                                   \
        if constexpr ((level) <= kCompileTimeLogLevel) {                   \
            if ((level) <= g_channel_levels[static_cast<size_t>(channel)]) \
                ::G::LogAt((level), __FILE__, __LINE__, ##__VA_ARGS__);    \
        }                                                                  \
    } while (0)
```

The runtime check is inside the `if constexpr` block, so it only exists in debug builds. In release, the entire macro is still eliminated. Channel levels can be exposed to Lua or a debug console for runtime toggling.

`CLOG` is opt-in. `DLOG`/`TLOG` use `kGeneral` by default and don't require a channel argument.

### Runtime level control

Expose channel levels to the Lua API so developers can filter at runtime:

```lua
G.log.set_level("physics", "warn")   -- silence physics below warn
G.log.set_level("*", "debug")        -- reset all to debug
```

This only exists in debug builds. The `g_channel_levels` array is not compiled into release.

### LogAt implementation

```cpp
template <typename... T>
void LogAt(LogLevel level, std::string_view file, int line, T&&... ts) {
    FixedStringBuffer<kMaxLogLineLength> buf("[", TrimPath(file), ":", line, "] ");
    buf.Append<T...>(std::forward<T>(ts)...);
    GetLogSink()(level, buf.str());
}
```

Identical to the current `Log()` function but takes a `LogLevel` parameter. The existing `Log()` function becomes a thin wrapper that calls `LogAt(LogLevel::kInfo, ...)`.

### Sink changes

The `LogSink` signature changes to receive the level:

```cpp
using LogSink = void (*)(LogLevel level, const char* message);
```

This is already the current signature. The SDL sink (`LogToSDL`) maps levels to SDL log priorities:

```cpp
void LogToSDL(LogLevel level, const char* message) {
    switch (level) {
        case LogLevel::kFatal: SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
        case LogLevel::kError: SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
        case LogLevel::kWarn:  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
        case LogLevel::kInfo:  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
        case LogLevel::kDebug: SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
        case LogLevel::kTrace: SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
    }
}
```

### Performance characteristics

| Build | `LOG(...)` | `DLOG(...)` | `TLOG(...)` | `CLOG(ch, level, ...)` |
|-------|-----------|-------------|-------------|----------------------|
| Debug | Always runs | Always runs | Always runs | Runtime channel check |
| Release | Always runs | **Zero cost** (compiled out) | **Zero cost** (compiled out) | **Zero cost** (compiled out) |

"Zero cost" means: no instructions emitted, no string literals in the binary, no branch. The `if constexpr` guarantee is stronger than a runtime `if` — the compiler is required to discard the dead branch and all its contents.

### What this does NOT include

- **Structured logging / key-value fields.** Plain text with levels and channels is sufficient for the engine's scale. Structured logging adds macro complexity for little benefit when one developer is reading the output.
- **Binary logging.** The engine's log volume doesn't justify the tooling overhead of a binary format.
- **Multiple simultaneous sinks.** The single `LogSink` function pointer is sufficient. If needed later, a sink that dispatches to multiple callbacks is trivial to add without changing the API.
- **File logging.** SDL already handles routing to platform-appropriate outputs. Adding a dedicated file sink is orthogonal to this design.
- **Debug overlay / in-game console.** This is a separate feature (Dear ImGui integration or custom immediate-mode UI). The logging system feeds data to whatever display exists, but the display is not part of this design.

## File changes summary

| File | What changes |
|------|-------------|
| `src/logging.h` | Add `kError`/`kWarn`/`kDebug`/`kTrace` to `LogLevel`. Add `kCompileTimeLogLevel`. Add `LOG_AT` macro. Add `ELOG`/`WLOG`/`DLOG`/`TLOG` macros. Add `LogChannel` enum and `g_channel_levels` (debug only). Add `CLOG` macro. Add `LogAt()` template. |
| `src/logging.cc` | Update `DefaultLog` to handle new levels. |
| `src/game.cc` | Update `LogToSDL` to map new levels to SDL priorities. |
| `src/lua_*.cc` | Optionally expose `G.log.set_level()` for runtime channel control. |

## Migration

Existing `LOG()` calls are unchanged. Migration is purely additive:

1. Add the new levels, macros, and `LogAt()`.
2. Developers start using `DLOG()`/`TLOG()` for diagnostics they previously would have added and removed.
3. Optionally add `CLOG()` calls with channels for subsystems that produce high log volume.
4. No existing code breaks. No existing behavior changes.
