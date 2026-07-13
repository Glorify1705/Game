#pragma once
#ifndef _GAME_BLOB_STORE_H
#define _GAME_BLOB_STORE_H

#include <cstdint>

#include "array.h"
#include "constants.h"
#include "error.h"

namespace G {

// PhysFS mount point under which asset blobs are visible at runtime: the
// loose blob directory in dev mode, assets.zip in packaged mode.
inline constexpr const char* kBlobMountPoint = "/blobs";

// Formats a blob hash as 16 lowercase hex characters plus a NUL terminator.
void FormatBlobName(uint64_t hash, char out[17]);

// Content-addressed store of loose blob files named by the rapidhash64 of
// their contents (16 lowercase hex chars). Writes go to a temp file in the
// same directory followed by a rename, so concurrent readers never observe a
// partial blob. Put is idempotent: existing blobs are not rewritten.
class BlobStore {
 public:
  // Creates the blob directory if missing and returns a store over it.
  static ErrorOr<BlobStore> Create(const char* directory);

  // Hashes contents with rapidhash64, writes the blob if absent, and returns
  // the content hash.
  ErrorOr<uint64_t> Put(ByteSlice contents);

  // Writes contents under the given precomputed hash if absent.
  ErrorOr<void> PutWithHash(uint64_t hash, ByteSlice contents);

  // Deletes files in the store that are not named by a hash in `referenced`
  // (which must be sorted ascending), including stale temp files. Returns the
  // number of files removed.
  size_t SweepUnreferenced(Slice<const uint64_t> referenced);

  // Native filesystem directory backing this store.
  const char* directory() const { return dir_; }

 private:
  explicit BlobStore(const char* directory);

  // Plain array rather than PathBuffer: the store is returned by value from
  // Create() through ErrorOr, and FixedStringBuffer is not movable.
  char dir_[kMaxPathLength + 1];
};

// Reads the blob with the given hash from the kBlobMountPoint PhysFS mount
// into buffer. Fails if the blob is missing or its size differs from `size`.
ErrorOr<void> ReadBlob(uint64_t hash, uint8_t* buffer, size_t size);

}  // namespace G

#endif  // _GAME_BLOB_STORE_H
