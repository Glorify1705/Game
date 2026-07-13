#pragma once
#ifndef _GAME_ZIP_WRITER_H
#define _GAME_ZIP_WRITER_H

#include <cstdint>
#include <cstdio>
#include <string_view>

#include "allocators.h"
#include "array.h"
#include "error.h"

namespace G {

// CRC-32 (IEEE, reflected polynomial 0xEDB88320) of the buffer.
uint32_t Crc32(ByteSlice data);

// Minimal zip archive writer. Entries are stored uncompressed (method 0,
// STORE) with CRC-32, zeroed timestamps, and no zip64 support, so output is
// byte-for-byte deterministic for a given sequence of AddEntry calls.
// Readable by PhysFS and any standard zip tool.
class ZipWriter {
 public:
  // allocator backs the central directory entry list.
  explicit ZipWriter(Allocator* allocator) : entries_(allocator) {}
  // Crashes if the archive was opened but Finish() was never called.
  ~ZipWriter();

  ZipWriter(const ZipWriter&) = delete;
  ZipWriter& operator=(const ZipWriter&) = delete;

  // Creates (truncating) the archive at the given native path.
  ErrorOr<void> Open(const char* path);

  // Appends one entry. name must be a relative path shorter than 64 bytes;
  // contents are written immediately. Crashes if the archive would exceed
  // non-zip64 limits (4 GiB offsets/sizes, 65535 entries).
  ErrorOr<void> AddEntry(std::string_view name, ByteSlice contents);

  // Writes the central directory and end-of-central-directory record and
  // closes the file. Must be called exactly once after all entries.
  ErrorOr<void> Finish();

 private:
  struct Entry {
    char name[64];
    uint16_t name_len;
    uint32_t crc32;
    uint32_t size;
    // Offset of this entry's local file header from the start of the file.
    uint32_t offset;
  };

  FILE* file_ = nullptr;
  DynArray<Entry> entries_;
};

}  // namespace G

#endif  // _GAME_ZIP_WRITER_H
