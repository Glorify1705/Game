#include "profiler.h"

#include <physfs.h>

#include <cstdio>

#include "constants.h"
#include "defer.h"
#include "logging.h"
#include "stringlib.h"

namespace G {

Profiler* GetProfiler() {
  static Profiler instance;
  return &instance;
}

void Profiler::AddEvent(std::string_view name, std::string_view category,
                        double start, double duration, uint32_t tid) {
  TraceEvent& e = events_[write_pos_ % kMaxEvents];
  e.name = name;
  e.category = category;
  e.timestamp = start;
  e.duration = duration;
  e.thread_id = tid;
  e.phase = kComplete;
  e.counter_value = 0;
  write_pos_++;
  if (count_ < kMaxEvents) count_++;
}

void Profiler::AddInstant(std::string_view name, std::string_view category,
                          uint32_t tid) {
  TraceEvent& e = events_[write_pos_ % kMaxEvents];
  e.name = name;
  e.category = category;
  e.timestamp = NowInSeconds();
  e.duration = 0;
  e.thread_id = tid;
  e.phase = kInstant;
  e.counter_value = 0;
  write_pos_++;
  if (count_ < kMaxEvents) count_++;
}

void Profiler::AddCounter(std::string_view name, double value, uint32_t tid) {
  TraceEvent& e = events_[write_pos_ % kMaxEvents];
  e.name = name;
  e.category = "counters";
  e.timestamp = NowInSeconds();
  e.duration = 0;
  e.thread_id = tid;
  e.phase = kCounter;
  e.counter_value = value;
  write_pos_++;
  if (count_ < kMaxEvents) count_++;
}

void Profiler::ToggleRecording() {
  if (recording_) {
    recording_ = false;
    Flush();
    const char* write_dir = PHYSFS_getWriteDir();
    LOG("Profiler: stopped recording, wrote ", count_, " events to ",
        write_dir ? write_dir : "", "trace.json");
    write_pos_ = 0;
    count_ = 0;
  } else {
    write_pos_ = 0;
    count_ = 0;
    recording_ = true;
    LOG("Profiler: started recording");
  }
}

void Profiler::Flush() {
  if (count_ == 0) return;

  const char* write_dir = PHYSFS_getWriteDir();
  if (write_dir == nullptr) {
    LOG("Profiler: no PhysFS write directory set");
    return;
  }
  FixedStringBuffer<kMaxPathLength> path(write_dir, "trace.json");
  FILE* f = fopen(path.str(), "w");
  if (f == nullptr) {
    LOG("Profiler: failed to open ", path.str(), " for writing");
    return;
  }
  DEFER([f] { fclose(f); });

  fputs("[\n", f);

  // Walk the ring buffer from oldest to newest.
  size_t start = (count_ == kMaxEvents) ? write_pos_ : 0;
  for (size_t i = 0; i < count_; ++i) {
    const TraceEvent& e = events_[(start + i) % kMaxEvents];
    // Chrome Tracing uses microseconds.
    double ts_us = e.timestamp * 1000000.0;

    if (i > 0) fputs(",\n", f);

    switch (e.phase) {
      case kComplete:
        fprintf(f,
                R"({"ph":"X","name":"%.*s","cat":"%.*s","pid":1,"tid":%u,)"
                R"("ts":%.3f,"dur":%.3f})",
                (int)e.name.size(), e.name.data(), (int)e.category.size(),
                e.category.data(), e.thread_id, ts_us, e.duration * 1000000.0);
        break;
      case kInstant:
        fprintf(f,
                R"({"ph":"i","name":"%.*s","cat":"%.*s","pid":1,"tid":%u,)"
                R"("ts":%.3f,"s":"g"})",
                (int)e.name.size(), e.name.data(), (int)e.category.size(),
                e.category.data(), e.thread_id, ts_us);
        break;
      case kCounter:
        fprintf(f,
                R"({"ph":"C","name":"%.*s","cat":"%.*s","pid":1,"tid":%u,)"
                R"("ts":%.3f,"args":{"%.*s":%.6f}})",
                (int)e.name.size(), e.name.data(), (int)e.category.size(),
                e.category.data(), e.thread_id, ts_us, (int)e.name.size(),
                e.name.data(), e.counter_value);
        break;
    }
  }

  fputs("\n]\n", f);
  LOG("Profiler: wrote ", count_, " events to ", path.str());
}

}  // namespace G
