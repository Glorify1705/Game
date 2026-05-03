#include "config.h"

#include <cerrno>

#include "clock.h"
#include "json_alc.h"
#include "sqlite_helpers.h"
#include "libraries/yyjson.h"
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
  ArenaAllocator scratch(allocator, Kilobytes(64));
  yyjson_read_err err{};
  yyjson_doc* doc = ReadJson(&scratch, json_configuration, &err);
  CHECK(doc != nullptr, "config parse failed: ", err.msg);
  yyjson_val* root = yyjson_doc_get_root(doc);
  CHECK(yyjson_is_obj(root), "config must be a json object");

  yyjson_val* key;
  yyjson_val* value;
  yyjson_obj_iter iter = yyjson_obj_iter_with(root);
  while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
    value = yyjson_obj_iter_get_val(key);
    std::string_view k = YyjsonStrView(key);
    if (k == "width") {
      config->window_width = yyjson_get_int(value);
    } else if (k == "height") {
      config->window_height = yyjson_get_int(value);
    } else if (k == "msaa_samples") {
      config->msaa_samples = yyjson_get_int(value);
    } else if (k == "borderless") {
      config->borderless = yyjson_get_bool(value);
    } else if (k == "centered") {
      config->centered = yyjson_get_bool(value);
    } else if (k == "fullscreen") {
      config->fullscreen = yyjson_get_bool(value);
    } else if (k == "enable_joystick") {
      config->enable_joystick = yyjson_get_bool(value);
    } else if (k == "enable_debug_rendering") {
      config->enable_debug_rendering = yyjson_get_bool(value);
    } else if (k == "title") {
      CopyString(YyjsonStrView(value), config->window_title,
                 sizeof(config->window_title));
    } else if (k == "org_name") {
      CopyString(YyjsonStrView(value), config->org_name,
                 sizeof(config->org_name));
    } else if (k == "app_name") {
      CopyString(YyjsonStrView(value), config->app_name,
                 sizeof(config->app_name));
    } else if (k == "version") {
      char ver[32];
      CopyString(YyjsonStrView(value), ver, sizeof(ver));
      ParseVersionFromString(ver, config);
    }
  }
}

void LoadConfigFromDatabase(sqlite3* db, GameConfig* config,
                            Allocator* allocator) {
  LOG("Reading configuration from database");
  SqlStmt stmt(db,
               "SELECT contents FROM text_files WHERE name = 'conf.json'");
  CHECK(stmt.ok(), "Failed to prepare config query");
  auto row = MUST(stmt.Step());
  if (!row) {
    LOG("No conf.json file for configuration in database, skipping");
    return;
  }
  auto contents = stmt.ColumnText(0);
  LoadConfig(contents.data(), config, allocator);
}

ErrorOr<void> LoadConfigFromFile(const char* path, GameConfig* config,
                                 Allocator* allocator) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return Error::Errno(errno);
  DEFER([f] { fclose(f); });
  if (fseek(f, 0, SEEK_END) != 0) return Error::Errno(errno);
  long size = ftell(f);
  if (size < 0) return Error::Errno(errno);
  if (fseek(f, 0, SEEK_SET) != 0) return Error::Errno(errno);
  char* contents = static_cast<char*>(allocator->Alloc(size + 1, 1));
  CHECK(contents != nullptr, "Failed to allocate bytes for config file");
  DEFER([&] { allocator->Dealloc(contents, size + 1); });
  size_t read_bytes = fread(contents, 1, size, f);
  if (read_bytes != static_cast<size_t>(size))
    return Error::Message("Short read loading config");
  contents[size] = '\0';
  LoadConfig(std::string_view(contents, size), config, allocator);
  return {};
}

}  // namespace G
