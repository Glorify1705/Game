#include "save.h"

#include <cstring>
#include <ctime>

#include "logging.h"
#include "platform.h"
#include "stringlib.h"

namespace G {

Save::Save(Allocator* allocator) : fetch_buf_(allocator) {}

Save::~Save() { Close(); }

ErrorOr<void> Save::Prepare(const char* sql, sqlite3_stmt** out) {
  int rc = sqlite3_prepare_v2(db_, sql, -1, out, nullptr);
  if (rc != SQLITE_OK) {
    ELOG("sqlite3_prepare_v2 failed: ", sqlite3_errmsg(db_));
    return Error::Message("failed to prepare statement");
  }
  return {};
}

ErrorOr<void> Save::Open(const char* save_dir) {
  if (db_ != nullptr) Close();

  TRY(MakeDirs(save_dir));

  PathBuffer path(save_dir, "/save.sqlite3");

  int rc = sqlite3_open(path.str(), &db_);
  if (rc != SQLITE_OK) {
    ELOG("sqlite3_open failed: ", sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return Error::Message("failed to open save database");
  }

  // Retry internally for up to 1 second on SQLITE_BUSY.
  sqlite3_busy_timeout(db_, /*ms=*/1000);

  // Enable WAL mode for crash safety and concurrent reads.
  char* err = nullptr;
  sqlite3_exec(db_, "PRAGMA journal_mode = WAL", nullptr, nullptr, &err);
  if (err != nullptr) sqlite3_free(err);
  sqlite3_exec(db_, "PRAGMA synchronous = NORMAL", nullptr, nullptr, &err);
  if (err != nullptr) sqlite3_free(err);

  // Create the KV table if it doesn't exist.
  rc = sqlite3_exec(db_,
                    "CREATE TABLE IF NOT EXISTS kv ("
                    "  namespace  TEXT NOT NULL,"
                    "  key        TEXT NOT NULL,"
                    "  value      BLOB,"
                    "  updated_at INTEGER NOT NULL,"
                    "  PRIMARY KEY (namespace, key)"
                    ") WITHOUT ROWID",
                    nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    ELOG("CREATE TABLE failed: ", err ? err : "unknown");
    if (err != nullptr) sqlite3_free(err);
    sqlite3_close(db_);
    db_ = nullptr;
    return Error::Message("failed to create kv table");
  }

  // Prepare cached statements. Close the database on any failure so we don't
  // leak the handle and partially-prepared statements.
  auto prepare_all = [&]() -> ErrorOr<void> {
    TRY(
        Prepare("INSERT OR REPLACE INTO kv (namespace, key, value, updated_at) "
                "VALUES (?1, ?2, ?3, ?4)",
                &set_stmt_));
    TRY(Prepare("SELECT value FROM kv WHERE namespace = ?1 AND key = ?2",
                &get_stmt_));
    TRY(Prepare("SELECT 1 FROM kv WHERE namespace = ?1 AND key = ?2 LIMIT 1",
                &has_stmt_));
    TRY(Prepare("DELETE FROM kv WHERE namespace = ?1 AND key = ?2",
                &delete_stmt_));
    TRY(Prepare("DELETE FROM kv WHERE namespace = ?1", &clear_stmt_));
    TRY(Prepare("SELECT key, value FROM kv WHERE namespace = ?1 ORDER BY key",
                &list_stmt_));
    TRY(Prepare("SELECT DISTINCT namespace FROM kv ORDER BY namespace",
                &namespaces_stmt_));
    return {};
  };
  auto prepare_result = prepare_all();
  if (prepare_result.is_error()) {
    Close();
    return prepare_result;
  }

  LOG("Save database opened: ", path.str());
  return {};
}

void Save::Close() {
  if (db_ == nullptr) return;
  if (set_stmt_) sqlite3_finalize(set_stmt_);
  if (get_stmt_) sqlite3_finalize(get_stmt_);
  if (has_stmt_) sqlite3_finalize(has_stmt_);
  if (delete_stmt_) sqlite3_finalize(delete_stmt_);
  if (clear_stmt_) sqlite3_finalize(clear_stmt_);
  if (list_stmt_) sqlite3_finalize(list_stmt_);
  if (namespaces_stmt_) sqlite3_finalize(namespaces_stmt_);
  set_stmt_ = get_stmt_ = has_stmt_ = delete_stmt_ = nullptr;
  clear_stmt_ = list_stmt_ = namespaces_stmt_ = nullptr;
  sqlite3_close(db_);
  db_ = nullptr;
}

ErrorOr<void> Save::Set(std::string_view ns, std::string_view key,
                        ByteSlice value) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(set_stmt_);
  sqlite3_clear_bindings(set_stmt_);
  sqlite3_bind_text(set_stmt_, 1, ns.data(), static_cast<int>(ns.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(set_stmt_, 2, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  sqlite3_bind_blob(set_stmt_, 3, value.data(), static_cast<int>(value.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int64(set_stmt_, 4, static_cast<int64_t>(std::time(nullptr)));
  int rc = sqlite3_step(set_stmt_);
  if (rc != SQLITE_DONE) {
    ELOG("save set failed: ", sqlite3_errmsg(db_));
    return Error::Message("save set failed");
  }
  return {};
}

ErrorOr<ByteSlice> Save::Get(std::string_view ns, std::string_view key) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(get_stmt_);
  sqlite3_clear_bindings(get_stmt_);
  sqlite3_bind_text(get_stmt_, 1, ns.data(), static_cast<int>(ns.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(get_stmt_, 2, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  int rc = sqlite3_step(get_stmt_);
  if (rc == SQLITE_DONE) {
    return ByteSlice{};
  }
  if (rc != SQLITE_ROW) {
    ELOG("save get failed: ", sqlite3_errmsg(db_));
    return Error::Message("save get failed");
  }
  const void* blob = sqlite3_column_blob(get_stmt_, 0);
  int blob_size = sqlite3_column_bytes(get_stmt_, 0);
  // Copy into fetch_buf_ so the data survives statement reset.
  fetch_buf_.Clear();
  fetch_buf_.Reserve(blob_size);
  fetch_buf_.Insert(static_cast<const uint8_t*>(blob), blob_size);
  return MakeByteSlice(fetch_buf_.data(), fetch_buf_.size());
}

ErrorOr<bool> Save::Has(std::string_view ns, std::string_view key) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(has_stmt_);
  sqlite3_clear_bindings(has_stmt_);
  sqlite3_bind_text(has_stmt_, 1, ns.data(), static_cast<int>(ns.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(has_stmt_, 2, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  int rc = sqlite3_step(has_stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  ELOG("save has failed: ", sqlite3_errmsg(db_));
  return Error::Message("save has failed");
}

ErrorOr<void> Save::Delete(std::string_view ns, std::string_view key) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(delete_stmt_);
  sqlite3_clear_bindings(delete_stmt_);
  sqlite3_bind_text(delete_stmt_, 1, ns.data(), static_cast<int>(ns.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(delete_stmt_, 2, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  int rc = sqlite3_step(delete_stmt_);
  if (rc != SQLITE_DONE) {
    ELOG("save delete failed: ", sqlite3_errmsg(db_));
    return Error::Message("save delete failed");
  }
  return {};
}

ErrorOr<void> Save::Clear(std::string_view ns) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(clear_stmt_);
  sqlite3_clear_bindings(clear_stmt_);
  sqlite3_bind_text(clear_stmt_, 1, ns.data(), static_cast<int>(ns.size()),
                    SQLITE_STATIC);
  int rc = sqlite3_step(clear_stmt_);
  if (rc != SQLITE_DONE) {
    ELOG("save clear failed: ", sqlite3_errmsg(db_));
    return Error::Message("save clear failed");
  }
  return {};
}

ErrorOr<void> Save::List(std::string_view ns, SaveListCallback callback,
                         void* userdata) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(list_stmt_);
  sqlite3_clear_bindings(list_stmt_);
  sqlite3_bind_text(list_stmt_, 1, ns.data(), static_cast<int>(ns.size()),
                    SQLITE_STATIC);
  while (true) {
    int rc = sqlite3_step(list_stmt_);
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      ELOG("save list failed: ", sqlite3_errmsg(db_));
      return Error::Message("save list failed");
    }
    const char* k =
        reinterpret_cast<const char*>(sqlite3_column_text(list_stmt_, 0));
    if (k == nullptr) continue;
    int k_len = sqlite3_column_bytes(list_stmt_, 0);
    const void* v = sqlite3_column_blob(list_stmt_, 1);
    int v_len = sqlite3_column_bytes(list_stmt_, 1);
    callback(std::string_view(k, k_len), MakeByteSlice(v, v_len), userdata);
  }
  return {};
}

ErrorOr<void> Save::Namespaces(SaveNamespacesCallback callback,
                               void* userdata) {
  CHECK(db_ != nullptr, "Save database not open");
  sqlite3_reset(namespaces_stmt_);
  while (true) {
    int rc = sqlite3_step(namespaces_stmt_);
    if (rc == SQLITE_DONE) break;
    if (rc != SQLITE_ROW) {
      ELOG("save namespaces failed: ", sqlite3_errmsg(db_));
      return Error::Message("save namespaces failed");
    }
    const char* name =
        reinterpret_cast<const char*>(sqlite3_column_text(namespaces_stmt_, 0));
    if (name == nullptr) continue;
    int name_len = sqlite3_column_bytes(namespaces_stmt_, 0);
    callback(std::string_view(name, name_len), userdata);
  }
  return {};
}

ErrorOr<void> Save::Flush() {
  CHECK(db_ != nullptr, "Save database not open");
  int rc = sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE,
                                     nullptr, nullptr);
  if (rc != SQLITE_OK) {
    ELOG("WAL checkpoint failed: ", sqlite3_errmsg(db_));
    return Error::Message("WAL checkpoint failed");
  }
  return {};
}

}  // namespace G
