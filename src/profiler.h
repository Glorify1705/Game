#pragma once
#ifndef _GAME_PROFILER_H
#define _GAME_PROFILER_H

#include <cstddef>
#include <cstdint>

#include "clock.h"

namespace G {

// Phase types for Chrome Tracing JSON format.
enum TracePhase : char {
  kComplete = 'X',  // Duration event (has ts + dur).
  kInstant = 'i',   // Instant marker (frame boundaries).
  kCounter = 'C',   // Counter event (line graph in Perfetto).
};

// A single trace event. Names are string literal pointers (never copied).
struct TraceEvent {
  const char* name;      // Zone name (must be a string literal).
  const char* category;  // Category string (must be a string literal).
  double timestamp;      // Start time in seconds (from NowInSeconds).
  double duration;       // Duration in seconds (zero for instant/counter).
  uint32_t thread_id;    // Thread that recorded the event.
  TracePhase phase;      // Event type.
  double counter_value;  // Value for counter events.
};

// Fixed-capacity ring buffer profiler. When full, oldest events are
// overwritten. Call ToggleRecording() to start/stop; on stop the buffer
// is flushed to a Chrome Tracing JSON file.
class Profiler {
 public:
  // Holds ~16 seconds at 60 FPS with ~1000 events/frame.
  static constexpr size_t kMaxEvents = 1 << 20;

  Profiler();

  // Records a complete (duration) event.
  void AddEvent(const char* name, const char* category, double start,
                double duration, uint32_t tid);

  // Records an instant event (e.g. frame boundary).
  void AddInstant(const char* name, const char* category, uint32_t tid);

  // Records a counter event (appears as line graph in Perfetto).
  void AddCounter(const char* name, double value, uint32_t tid);

  // Starts or stops recording. On stop, flushes to trace.json.
  void ToggleRecording();

  bool recording() const { return recording_; }

 private:
  // Writes all buffered events to a Chrome Tracing JSON file.
  void Flush();

  TraceEvent events_[kMaxEvents];
  size_t write_pos_ = 0;
  size_t count_ = 0;
  bool recording_ = false;
};

// Returns the global profiler instance.
Profiler* GetProfiler();

// RAII guard that records a duration event for the enclosing scope.
class ProfileZone {
 public:
  ProfileZone(const char* name, const char* category)
      : name_(name), category_(category), start_(NowInSeconds()) {}

  ~ProfileZone() {
    Profiler* p = GetProfiler();
    if (p->recording()) {
      p->AddEvent(name_, category_, start_, NowInSeconds() - start_,
                  /*tid=*/0);
    }
  }

 private:
  const char* name_;
  const char* category_;
  double start_;
};

}  // namespace G

#ifdef GAME_WITH_PROFILING

#define PROFILE_SCOPE \
  ::G::ProfileZone INTERNAL_ID(profile_zone)(__PRETTY_FUNCTION__, "engine")

#define PROFILE_SCOPE_N(name) \
  ::G::ProfileZone INTERNAL_ID(profile_zone)(name, "engine")

#define PROFILE_FRAME                                                 \
  do {                                                                \
    auto* p_ = ::G::GetProfiler();                                    \
    if (p_->recording()) p_->AddInstant("Frame", "frame", /*tid=*/0); \
  } while (0)

#define PROFILE_COUNTER(name, value)                             \
  do {                                                           \
    auto* p_ = ::G::GetProfiler();                               \
    if (p_->recording()) p_->AddCounter(name, value, /*tid=*/0); \
  } while (0)

#else

#define PROFILE_SCOPE (void)0
#define PROFILE_SCOPE_N(name) (void)0
#define PROFILE_FRAME (void)0
#define PROFILE_COUNTER(name, value) (void)0

#endif  // GAME_WITH_PROFILING

#endif  // _GAME_PROFILER_H
