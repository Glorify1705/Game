#include "config.h"

#include "defer.h"
#include "libraries/json.h"
#include "logging.h"
#include "lua.h"

namespace G {
namespace {

void CopyString(std::string_view s, char* dst, size_t cap) {
  const size_t size = std::min(s.size(), cap - 1);
  std::memcpy(dst, s.data(), size);
  dst[size] = '\0';
}

void ParseVersionFromString(const char* str, GameConfig* config) {
  // TODO: error handling.
  std::sscanf(str, "%d.%d", &config->version.major, &config->version.minor);
}

}  // namespace

void LoadConfig(std::string_view json_configuration, GameConfig* config,
                Allocator* /*allocator*/) {
  TIMER("Loading configuration");
  auto [status, json] = jt::Json::parse(json_configuration);
  CHECK(status == jt::Json::success,
        "failed to parse conf.json: ", jt::Json::StatusToString(status));
  CHECK(json.isObject(), "config must be a json object");
  for (auto& [key, value] : json.getObject()) {
    if (key == "width") {
      config->window_width = value.getLong();
    } else if (key == "height") {
      config->window_height = value.getNumber();
    } else if (key == "msaa_samples") {
      config->msaa_samples = value.getNumber();
    } else if (key == "borderless") {
      config->borderless = value.getBool();
    } else if (key == "centered") {
      config->centered = value.getBool();
    } else if (key == "fullscreen") {
      config->fullscreen = value.getBool();
    } else if (key == "enable_joystick") {
      config->enable_joystick = value.getBool();
    } else if (key == "enable_debug_rendering") {
      config->enable_debug_rendering = value.getBool();
    } else if (key == "title") {
      CopyString(value.getString(), config->window_title,
                 sizeof(config->window_title));
    } else if (key == "org_name") {
      CopyString(value.getString(), config->org_name, sizeof(config->org_name));
    } else if (key == "app_name") {
      CopyString(value.getString(), config->app_name, sizeof(config->app_name));
    } else if (key == "version") {
      ParseVersionFromString(value.getString().c_str(), config);
    }
  }
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

}  // namespace G
