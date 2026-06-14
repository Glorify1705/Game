#pragma once
#ifndef _GAME_SAVE_H
#define _GAME_SAVE_H

#include <string_view>

#include "allocators.h"
#include "array.h"
#include "error.h"
#include "libraries/sqlite3.h"

namespace G {

// Callback for iterating key-value pairs in a namespace.
using SaveListCallback = void (*)(std::string_view key, ByteSlice value,
                                  void* userdata);

// Callback for iterating namespace names.
using SaveNamespacesCallback = void (*)(std::string_view name, void* userdata);

// Persistent namespaced key-value store backed by SQLite. Values are opaque
// blobs (the Lua binding layer handles serialization). Each namespace groups
// related keys (e.g. "save", "settings", "achievements").
class Save {
 public:
  explicit Save(Allocator* allocator);
  ~Save();

  Save(const Save&) = delete;
  Save& operator=(const Save&) = delete;

  // Opens (or creates) the database at the given directory path.
  // Creates the directory if needed, enables WAL mode, and prepares
  // all cached statements.
  ErrorOr<void> Open(const char* save_dir);

  // Closes the database and finalizes all statements.
  void Close();

  // Returns true if the database is open.
  bool IsOpen() const { return db_ != nullptr; }

  // Stores a blob value for the given namespace and key.
  ErrorOr<void> Set(std::string_view ns, std::string_view key, ByteSlice value);

  // Retrieves the blob value for the given namespace and key.
  // Returns an empty slice if the key does not exist.
  // The returned data is valid until the next Get/List call.
  ErrorOr<ByteSlice> Get(std::string_view ns, std::string_view key);

  // Returns true if the key exists in the given namespace.
  ErrorOr<bool> Has(std::string_view ns, std::string_view key);

  // Deletes a single key from a namespace.
  ErrorOr<void> Delete(std::string_view ns, std::string_view key);

  // Deletes all keys in a namespace.
  ErrorOr<void> Clear(std::string_view ns);

  // Calls the callback for each key-value pair in a namespace.
  ErrorOr<void> List(std::string_view ns, SaveListCallback callback,
                     void* userdata);

  // Calls the callback for each distinct namespace.
  ErrorOr<void> Namespaces(SaveNamespacesCallback callback, void* userdata);

  // Checkpoints the WAL to the main database file.
  ErrorOr<void> Flush();

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* set_stmt_ = nullptr;
  sqlite3_stmt* get_stmt_ = nullptr;
  sqlite3_stmt* has_stmt_ = nullptr;
  sqlite3_stmt* delete_stmt_ = nullptr;
  sqlite3_stmt* clear_stmt_ = nullptr;
  sqlite3_stmt* list_stmt_ = nullptr;
  sqlite3_stmt* namespaces_stmt_ = nullptr;

  // Scratch buffer for Get() to copy blob data into (survives statement reset).
  DynArray<uint8_t> fetch_buf_;

  // Prepares a statement and stores it. Returns error on failure.
  ErrorOr<void> Prepare(const char* sql, sqlite3_stmt** out);
};

}  // namespace G

#endif  // _GAME_SAVE_H
