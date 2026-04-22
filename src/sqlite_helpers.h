#pragma once
#ifndef _GAME_SQLITE_HELPERS_H
#define _GAME_SQLITE_HELPERS_H

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "array.h"
#include "error.h"
#include "libraries/sqlite3.h"
#include "logging.h"

namespace G {

// RAII wrapper for a sqlite3 prepared statement. Logs errors and returns
// them instead of crashing. Check ok() after construction before use.
//
// Usage:
//   SqlStmt stmt(db, "SELECT name FROM scripts WHERE id = ?");
//   if (!stmt.ok()) return Error::Message("prepare failed");
//   stmt.BindInt(1, id);
//   auto step = TRY(stmt.Step());
//   if (step) {
//     auto name = stmt.ColumnText(0);
//   }
class SqlStmt {
 public:
  SqlStmt(sqlite3* db, std::string_view sql) : db_(db) {
    int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()),
                                &stmt_, nullptr);
    if (rc != SQLITE_OK) {
      ELOG("sqlite3_prepare_v2 failed: ", sqlite3_errmsg(db));
      stmt_ = nullptr;
    }
  }

  ~SqlStmt() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
  }

  SqlStmt(const SqlStmt&) = delete;
  SqlStmt& operator=(const SqlStmt&) = delete;

  // Returns true if the statement was prepared successfully.
  bool ok() const { return stmt_ != nullptr; }

  // Parameter binding (1-indexed). All text/blob use SQLITE_STATIC.
  void BindText(int idx, std::string_view v) {
    sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()),
                      SQLITE_STATIC);
  }

  void BindInt(int idx, int v) { sqlite3_bind_int(stmt_, idx, v); }

  void BindInt64(int idx, int64_t v) { sqlite3_bind_int64(stmt_, idx, v); }

  void BindBlob(int idx, ByteSlice data) {
    sqlite3_bind_blob(stmt_, idx, data.data(), static_cast<int>(data.size()),
                      SQLITE_STATIC);
  }

  void BindBlobTransient(int idx, ByteSlice data) {
    sqlite3_bind_blob(stmt_, idx, data.data(), static_cast<int>(data.size()),
                      SQLITE_TRANSIENT);
  }

  void BindTextTransient(int idx, std::string_view v) {
    sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()),
                      SQLITE_TRANSIENT);
  }

  // Steps the statement. Returns true on SQLITE_ROW, false on SQLITE_DONE.
  // Returns an error on any other result.
  [[nodiscard]] ErrorOr<bool> Step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    ELOG("sqlite3_step failed: ", sqlite3_errmsg(db_));
    return Error::Message("sqlite3_step failed");
  }

  // Resets the statement and clears bindings for reuse in loops.
  void Reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }

  // Column readers (0-indexed). Only valid after Step() returned true.
  std::string_view ColumnText(int col) {
    const char* text = reinterpret_cast<const char*>(
        sqlite3_column_text(stmt_, col));
    if (text == nullptr) return {};
    return text;
  }

  int ColumnInt(int col) { return sqlite3_column_int(stmt_, col); }

  int64_t ColumnInt64(int col) { return sqlite3_column_int64(stmt_, col); }

  // Returns the blob column as a ByteSlice (pointer + size in one value).
  ByteSlice ColumnBlob(int col) {
    const void* data = sqlite3_column_blob(stmt_, col);
    int size = sqlite3_column_bytes(stmt_, col);
    return MakeByteSlice(data, size);
  }

 private:
  sqlite3_stmt* stmt_ = nullptr;
  sqlite3* db_;
};

// Executes a single SQL statement without results. Logs and returns error
// on failure. The sql parameter must be null-terminated.
inline ErrorOr<void> SqlExec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    ELOG("sqlite3_exec failed: ", err ? err : "unknown");
    if (err != nullptr) sqlite3_free(err);
    return Error::Message("sqlite3_exec failed");
  }
  return {};
}

// RAII transaction scope. BEGIN on construction, END on destruction.
struct SqlTransaction {
  sqlite3* db;

  explicit SqlTransaction(sqlite3* database) : db(database) {
    auto result = SqlExec(db, "BEGIN TRANSACTION");
    if (result.is_error()) {
      ELOG("Failed to begin transaction");
    }
  }

  ~SqlTransaction() {
    auto result = SqlExec(db, "END TRANSACTION");
    if (result.is_error()) {
      ELOG("Failed to end transaction");
    }
  }

  SqlTransaction(const SqlTransaction&) = delete;
  SqlTransaction& operator=(const SqlTransaction&) = delete;
};

}  // namespace G

#endif  // _GAME_SQLITE_HELPERS_H
