#include "config.h"

#include <cerrno>

#include "clock.h"
#include "defer.h"
#include "json.h"
#include "logging.h"

namespace G {
namespace {

void CopyString(std::string_view s, char* dst, size_t cap) {
  const size_t size = std::min(s.size(), cap - 1);
  std::memcpy(dst, s.data(), size);
  dst[size] = '\0';
}

void ParseVersionFromString(const char* str, GameConfig* config) {
  int matched =
      std::sscanf(str, "%d.%d", &config->version.major, &config->version.minor);
  CHECK(matched == 2, "invalid version string: ", str);
}

}  // namespace

void LoadConfig(std::string_view json_configuration, GameConfig* config,
                Allocator* allocator) {
  TIMER("Loading configuration");
  ArenaAllocator scratch(allocator, Kilobytes(16));
  JsonValue* json = MUST(ParseJson(json_configuration, &scratch));
  CHECK(json->IsObject(), "config must be a json object");
  json->ForEachMember([&](std::string_view key, const JsonValue& value) {
    if (key == "width") {
      config->window_width = value.GetLong();
    } else if (key == "height") {
      config->window_height = value.GetNumber();
    } else if (key == "msaa_samples") {
      config->msaa_samples = value.GetNumber();
    } else if (key == "borderless") {
      config->borderless = value.GetBool();
    } else if (key == "centered") {
      config->centered = value.GetBool();
    } else if (key == "fullscreen") {
      config->fullscreen = value.GetBool();
    } else if (key == "enable_joystick") {
      config->enable_joystick = value.GetBool();
    } else if (key == "enable_debug_rendering") {
      config->enable_debug_rendering = value.GetBool();
    } else if (key == "title") {
      CopyString(value.GetString(), config->window_title,
                 sizeof(config->window_title));
    } else if (key == "org_name") {
      CopyString(value.GetString(), config->org_name, sizeof(config->org_name));
    } else if (key == "app_name") {
      CopyString(value.GetString(), config->app_name, sizeof(config->app_name));
    } else if (key == "version") {
      // GetString() points into allocator memory and is null-terminated
      // when escape processing occurs, but for safety copy to a local buffer.
      char ver[32];
      CopyString(value.GetString(), ver, sizeof(ver));
      ParseVersionFromString(ver, config);
    }
  });
}

void LoadConfigFromDatabase(sqlite3* db, GameConfig* config,
                            Allocator* allocator) {
  LOG("Reading configuration from database");
  constexpr std::string_view query =
      "SELECT contents FROM text_files WHERE name = 'conf.json'";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, query.data(), query.size(), &stmt, nullptr) !=
      SQLITE_OK) {
    DIE("Failed to prepare statement ", query, ": ", sqlite3_errmsg(db));
  }
  DEFER([stmt] { sqlite3_finalize(stmt); });
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    LOG("No conf.json file for configuration in database, skipping");
    return;
  }
  auto contents = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  LoadConfig(contents, config, allocator);
}

ErrorOr<void> LoadConfigFromFile(const char* path, GameConfig* config,
                                 Allocator* allocator) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return Error::Errno(errno);
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* contents = static_cast<char*>(allocator->Alloc(size + 1, 1));
  [[maybe_unused]] size_t read_bytes = fread(contents, 1, size, f);
  contents[size] = '\0';
  fclose(f);
  LoadConfig(std::string_view(contents, size), config, allocator);
  allocator->Dealloc(contents, size + 1);
  return {};
}

}  // namespace G
