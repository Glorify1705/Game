#include <cstdio>
#include <cstring>

#include "blob_store.h"
#include "gtest/gtest.h"
#include "libraries/rapidhash.h"
#include "physfs.h"
#include "platform.h"

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace G {
namespace {

const char* TempDir() {
#ifdef _WIN32
  const char* tmp = std::getenv("TEMP");
  if (!tmp) tmp = std::getenv("TMP");
  if (!tmp) tmp = "C:\\Temp";
  return tmp;
#else
  return "/tmp";
#endif
}

// Removes every file in dir, then dir itself.
void RemoveDirRecursive(const char* dir) {
  IterateDirectory(
      dir,
      [](const DirEntry& entry, void* ud) {
        char path[768];
        std::snprintf(path, sizeof(path), "%s/%s", static_cast<const char*>(ud),
                      entry.name);
        std::remove(path);
      },
      const_cast<char*>(dir))
      .release_value();
  std::remove(dir);
}

class BlobStoreTest : public ::testing::Test {
 protected:
  char dir_[512] = {};
  bool mounted_ = false;

  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::snprintf(dir_, sizeof(dir_), "%s/game_blob_test_%d_%s", TempDir(),
                  static_cast<int>(getpid()), info->name());
    ASSERT_NE(PHYSFS_init("test"), 0);
  }

  void TearDown() override {
    if (mounted_) PHYSFS_unmount(dir_);
    PHYSFS_deinit();
    RemoveDirRecursive(dir_);
  }

  // Mounts the store directory at the blob mount point for ReadBlob tests.
  void Mount() {
    ASSERT_NE(PHYSFS_mount(dir_, kBlobMountPoint, /*append=*/0), 0)
        << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
    mounted_ = true;
  }

  size_t CountFiles() {
    size_t count = 0;
    IterateDirectory(
        dir_,
        [](const DirEntry& entry, void* ud) {
          if (entry.type == DirEntryType::kFile) {
            ++*static_cast<size_t*>(ud);
          }
        },
        &count)
        .release_value();
    return count;
  }
};

TEST_F(BlobStoreTest, FormatBlobNameMatchesPrintf) {
  char formatted[17];
  FormatBlobName(0x0123456789abcdefULL, formatted);
  EXPECT_STREQ(formatted, "0123456789abcdef");
  FormatBlobName(0, formatted);
  EXPECT_STREQ(formatted, "0000000000000000");
  char reference[17];
  std::snprintf(reference, sizeof(reference), "%016llx", 0xdeadbeef12345678ULL);
  FormatBlobName(0xdeadbeef12345678ULL, formatted);
  EXPECT_STREQ(formatted, reference);
}

TEST_F(BlobStoreTest, PutCreatesFileNamedByHash) {
  BlobStore store = MUST(BlobStore::Create(dir_));
  const uint8_t data[] = {1, 2, 3, 4};
  const uint64_t hash = MUST(store.Put(ByteSlice(data, sizeof(data))));
  EXPECT_EQ(hash, rapidhash(data, sizeof(data)));

  char name[17];
  FormatBlobName(hash, name);
  char path[768];
  std::snprintf(path, sizeof(path), "%s/%s", dir_, name);
  EXPECT_TRUE(FileExists(path));
  EXPECT_EQ(CountFiles(), 1u);
}

TEST_F(BlobStoreTest, PutIsIdempotentAndDedups) {
  BlobStore store = MUST(BlobStore::Create(dir_));
  const uint8_t data[] = {42, 43, 44};
  const uint64_t first = MUST(store.Put(ByteSlice(data, sizeof(data))));
  const uint64_t second = MUST(store.Put(ByteSlice(data, sizeof(data))));
  EXPECT_EQ(first, second);
  // One blob file and no leftover temp files.
  EXPECT_EQ(CountFiles(), 1u);
}

TEST_F(BlobStoreTest, ReadBlobRoundTrip) {
  BlobStore store = MUST(BlobStore::Create(dir_));
  const char text[] = "the quick brown fox";
  const uint64_t hash = MUST(store.Put(MakeByteSlice(text, sizeof(text) - 1)));
  Mount();

  uint8_t buffer[64] = {};
  auto result = ReadBlob(hash, buffer, sizeof(text) - 1);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(std::string_view(reinterpret_cast<char*>(buffer), sizeof(text) - 1),
            text);
}

TEST_F(BlobStoreTest, ReadBlobSizeMismatchFails) {
  BlobStore store = MUST(BlobStore::Create(dir_));
  const uint8_t data[] = {9, 9, 9};
  const uint64_t hash = MUST(store.Put(ByteSlice(data, sizeof(data))));
  Mount();

  uint8_t buffer[16];
  auto result = ReadBlob(hash, buffer, /*size=*/16);
  EXPECT_TRUE(result.is_error());
}

TEST_F(BlobStoreTest, ReadBlobMissingFails) {
  MUST(BlobStore::Create(dir_));
  Mount();
  uint8_t buffer[16];
  auto result = ReadBlob(0x1234, buffer, sizeof(buffer));
  EXPECT_TRUE(result.is_error());
}

TEST_F(BlobStoreTest, SweepUnreferencedRemovesOrphans) {
  BlobStore store = MUST(BlobStore::Create(dir_));
  const uint8_t a[] = {1};
  const uint8_t b[] = {2};
  const uint8_t c[] = {3};
  const uint64_t keep = MUST(store.Put(ByteSlice(a, 1)));
  MUST(store.Put(ByteSlice(b, 1)));
  MUST(store.Put(ByteSlice(c, 1)));
  ASSERT_EQ(CountFiles(), 3u);

  const uint64_t referenced[] = {keep};
  EXPECT_EQ(store.SweepUnreferenced(Slice<const uint64_t>(referenced, 1)), 2u);
  EXPECT_EQ(CountFiles(), 1u);

  // The referenced blob is still readable.
  Mount();
  uint8_t buffer[1];
  EXPECT_FALSE(ReadBlob(keep, buffer, 1).is_error());
  EXPECT_EQ(buffer[0], 1);
}

TEST_F(BlobStoreTest, SweepRemovesStaleTempFiles) {
  BlobStore store = MUST(BlobStore::Create(dir_));
  char temp_path[768];
  std::snprintf(temp_path, sizeof(temp_path), "%s/0123456789abcdef.tmp", dir_);
  FILE* f = fopen(temp_path, "wb");
  ASSERT_NE(f, nullptr);
  fputs("partial", f);
  fclose(f);

  EXPECT_EQ(store.SweepUnreferenced(Slice<const uint64_t>()), 1u);
  EXPECT_FALSE(FileExists(temp_path));
}

}  // namespace
}  // namespace G
