#include "blob_store.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "libraries/rapidhash.h"
#include "logging.h"
#include "physfs.h"
#include "platform.h"
#include "stringlib.h"

namespace G {

namespace {

// Parses a 16-lowercase-hex-character blob filename back into its hash.
// Returns false for any other name (e.g. stale temp files).
bool ParseBlobName(std::string_view name, uint64_t* hash) {
  if (name.size() != 16) return false;
  uint64_t result = 0;
  for (char c : name) {
    uint64_t digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else {
      return false;
    }
    result = (result << 4) | digit;
  }
  *hash = result;
  return true;
}

struct SweepContext {
  const char* directory;
  Slice<const uint64_t> referenced;
  size_t removed;
};

void SweepEntry(const DirEntry& entry, void* userdata) {
  auto* ctx = static_cast<SweepContext*>(userdata);
  if (entry.type != DirEntryType::kFile) return;
  uint64_t hash;
  if (ParseBlobName(entry.name, &hash) &&
      std::binary_search(ctx->referenced.begin(), ctx->referenced.end(),
                         hash)) {
    return;
  }
  PathBuffer path(ctx->directory, "/", entry.name);
  if (remove(path.str()) == 0) ctx->removed++;
}

}  // namespace

void FormatBlobName(uint64_t hash, char out[17]) {
  snprintf(out, 17, "%016llx", static_cast<unsigned long long>(hash));
}

BlobStore::BlobStore(const char* directory) {
  const int written = snprintf(dir_, sizeof(dir_), "%s", directory);
  CHECK(written >= 0 && static_cast<size_t>(written) < sizeof(dir_),
        "blob store directory path too long: ", directory);
}

ErrorOr<BlobStore> BlobStore::Create(const char* directory) {
  TRY(MakeDirs(directory));
  return BlobStore(directory);
}

ErrorOr<uint64_t> BlobStore::Put(ByteSlice contents) {
  const uint64_t hash = rapidhash(contents.data(), contents.size());
  TRY(PutWithHash(hash, contents));
  return hash;
}

ErrorOr<void> BlobStore::PutWithHash(uint64_t hash, ByteSlice contents) {
  char name[17];
  FormatBlobName(hash, name);
  PathBuffer path(dir_, "/", name);
  if (FileExists(path.str())) return {};

  PathBuffer temp_path(dir_, "/", name, ".tmp");
  FILE* f = fopen(temp_path.str(), "wb");
  if (f == nullptr) return Error::Errno(errno);
  if (!contents.empty() &&
      fwrite(contents.data(), 1, contents.size(), f) != contents.size()) {
    fclose(f);
    remove(temp_path.str());
    return Error::Message("failed to write blob");
  }
  if (fclose(f) != 0) {
    remove(temp_path.str());
    return Error::Errno(errno);
  }
  if (rename(temp_path.str(), path.str()) != 0) {
    remove(temp_path.str());
    // A concurrent Put of identical content already renamed the blob into
    // place; the content is the same, so this is a success.
    if (FileExists(path.str())) return {};
    return Error::Errno(errno);
  }
  return {};
}

size_t BlobStore::SweepUnreferenced(Slice<const uint64_t> referenced) {
  SweepContext ctx;
  ctx.directory = dir_;
  ctx.referenced = referenced;
  ctx.removed = 0;
  auto result = IterateDirectory(dir_, SweepEntry, &ctx);
  if (result.is_error()) {
    LOG("Failed to sweep blob store ", dir_, ": ", result.error().message());
  }
  return ctx.removed;
}

ErrorOr<void> ReadBlob(uint64_t hash, uint8_t* buffer, size_t size) {
  char name[17];
  FormatBlobName(hash, name);
  PathBuffer path(kBlobMountPoint, "/", name);
  PHYSFS_File* file = PHYSFS_openRead(path.str());
  if (file == nullptr) return Error::Message("blob not found");
  const auto length = PHYSFS_fileLength(file);
  if (length < 0 || static_cast<size_t>(length) != size) {
    PHYSFS_close(file);
    return Error::Message("blob size mismatch");
  }
  const auto read = PHYSFS_readBytes(file, buffer, size);
  PHYSFS_close(file);
  if (read < 0 || static_cast<size_t>(read) != size) {
    return Error::Message("failed to read blob");
  }
  return {};
}

}  // namespace G
