#include <cstdio>
#include <cstring>

#include "gtest/gtest.h"
#include "physfs.h"
#include "platform.h"
#include "zip_writer.h"

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

class ZipWriterTest : public ::testing::Test {
 protected:
  SystemAllocator allocator_;
  char dir_[512] = {};
  char zip_path_[768] = {};

  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::snprintf(dir_, sizeof(dir_), "%s/game_zip_test_%d_%s", TempDir(),
                  static_cast<int>(getpid()), info->name());
    ASSERT_FALSE(MakeDirs(dir_).is_error());
    std::snprintf(zip_path_, sizeof(zip_path_), "%s/test.zip", dir_);
    ASSERT_NE(PHYSFS_init("test"), 0);
  }

  void TearDown() override {
    PHYSFS_deinit();
    std::remove(zip_path_);
    std::remove(dir_);
  }

  // Reads the whole file at path into buf, returning its size.
  size_t ReadFileBytes(const char* path, uint8_t* buf, size_t buf_size) {
    FILE* f = fopen(path, "rb");
    EXPECT_NE(f, nullptr);
    size_t n = fread(buf, 1, buf_size, f);
    fclose(f);
    return n;
  }
};

TEST_F(ZipWriterTest, Crc32KnownVectors) {
  EXPECT_EQ(Crc32(MakeByteSlice("123456789", 9)), 0xCBF43926u);
  EXPECT_EQ(Crc32(ByteSlice()), 0u);
  EXPECT_EQ(Crc32(MakeByteSlice("a", 1)), 0xE8B7BE43u);
}

TEST_F(ZipWriterTest, EmptyArchiveIsJustEocd) {
  ZipWriter writer(&allocator_);
  ASSERT_FALSE(writer.Open(zip_path_).is_error());
  ASSERT_FALSE(writer.Finish().is_error());

  uint8_t buf[64];
  ASSERT_EQ(ReadFileBytes(zip_path_, buf, sizeof(buf)), 22u);
  // EOCD signature.
  EXPECT_EQ(buf[0], 0x50);
  EXPECT_EQ(buf[1], 0x4b);
  EXPECT_EQ(buf[2], 0x05);
  EXPECT_EQ(buf[3], 0x06);
  // Zero entries.
  EXPECT_EQ(buf[10], 0);
  EXPECT_EQ(buf[11], 0);
}

TEST_F(ZipWriterTest, RoundTripThroughPhysfs) {
  const char* names[] = {"first.txt", "second.bin", "third"};
  const char* contents[] = {"hello world", "some\nmore\ndata", ""};

  ZipWriter writer(&allocator_);
  ASSERT_FALSE(writer.Open(zip_path_).is_error());
  for (int i = 0; i < 3; ++i) {
    ASSERT_FALSE(
        writer
            .AddEntry(names[i], MakeByteSlice(contents[i], strlen(contents[i])))
            .is_error());
  }
  ASSERT_FALSE(writer.Finish().is_error());

  ASSERT_NE(PHYSFS_mount(zip_path_, "/ziptest", /*append=*/0), 0)
      << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
  for (int i = 0; i < 3; ++i) {
    char vfs_path[128];
    std::snprintf(vfs_path, sizeof(vfs_path), "/ziptest/%s", names[i]);
    PHYSFS_File* f = PHYSFS_openRead(vfs_path);
    ASSERT_NE(f, nullptr) << "missing entry " << names[i];
    const size_t expected = strlen(contents[i]);
    EXPECT_EQ(static_cast<size_t>(PHYSFS_fileLength(f)), expected);
    char buf[128] = {};
    PHYSFS_readBytes(f, buf, sizeof(buf));
    EXPECT_EQ(std::string_view(buf, expected), contents[i]);
    PHYSFS_close(f);
  }
  ASSERT_NE(PHYSFS_unmount(zip_path_), 0);
}

TEST_F(ZipWriterTest, BinaryContentsRoundTrip) {
  uint8_t data[256];
  for (int i = 0; i < 256; ++i) data[i] = static_cast<uint8_t>(i);

  ZipWriter writer(&allocator_);
  ASSERT_FALSE(writer.Open(zip_path_).is_error());
  ASSERT_FALSE(writer.AddEntry("bytes", ByteSlice(data, 256)).is_error());
  ASSERT_FALSE(writer.Finish().is_error());

  ASSERT_NE(PHYSFS_mount(zip_path_, "/ziptest", /*append=*/0), 0);
  PHYSFS_File* f = PHYSFS_openRead("/ziptest/bytes");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(PHYSFS_fileLength(f), 256);
  uint8_t read_back[256];
  ASSERT_EQ(PHYSFS_readBytes(f, read_back, 256), 256);
  EXPECT_EQ(std::memcmp(read_back, data, 256), 0);
  PHYSFS_close(f);
  ASSERT_NE(PHYSFS_unmount(zip_path_), 0);
}

TEST_F(ZipWriterTest, DeterministicOutput) {
  char other_path[768];
  std::snprintf(other_path, sizeof(other_path), "%s/other.zip", dir_);

  for (const char* path : {static_cast<const char*>(zip_path_),
                           static_cast<const char*>(other_path)}) {
    ZipWriter writer(&allocator_);
    ASSERT_FALSE(writer.Open(path).is_error());
    ASSERT_FALSE(writer.AddEntry("a", MakeByteSlice("aaa", 3)).is_error());
    ASSERT_FALSE(writer.AddEntry("b", MakeByteSlice("bbb", 3)).is_error());
    ASSERT_FALSE(writer.Finish().is_error());
  }

  uint8_t first[512], second[512];
  const size_t first_size = ReadFileBytes(zip_path_, first, sizeof(first));
  const size_t second_size = ReadFileBytes(other_path, second, sizeof(second));
  EXPECT_EQ(first_size, second_size);
  EXPECT_EQ(std::memcmp(first, second, first_size), 0);
  std::remove(other_path);
}

TEST_F(ZipWriterTest, CentralDirectoryStructure) {
  ZipWriter writer(&allocator_);
  ASSERT_FALSE(writer.Open(zip_path_).is_error());
  ASSERT_FALSE(
      writer.AddEntry("entry.txt", MakeByteSlice("data", 4)).is_error());
  ASSERT_FALSE(writer.Finish().is_error());

  uint8_t buf[512];
  const size_t size = ReadFileBytes(zip_path_, buf, sizeof(buf));
  // Layout: local header (30) + name (9) + data (4) + central dir entry
  // (46 + 9) + EOCD (22).
  ASSERT_EQ(size, 30u + 9 + 4 + 46 + 9 + 22);

  const uint8_t* eocd = buf + size - 22;
  EXPECT_EQ(eocd[0], 0x50);
  EXPECT_EQ(eocd[3], 0x06);
  // One entry total.
  EXPECT_EQ(eocd[10], 1);
  // Central directory offset points at its signature.
  const uint32_t cd_offset = static_cast<uint32_t>(eocd[16]) |
                             (static_cast<uint32_t>(eocd[17]) << 8) |
                             (static_cast<uint32_t>(eocd[18]) << 16) |
                             (static_cast<uint32_t>(eocd[19]) << 24);
  ASSERT_EQ(cd_offset, 30u + 9 + 4);
  const uint8_t* cd = buf + cd_offset;
  EXPECT_EQ(cd[0], 0x50);
  EXPECT_EQ(cd[1], 0x4b);
  EXPECT_EQ(cd[2], 0x01);
  EXPECT_EQ(cd[3], 0x02);
  // Method STORE.
  EXPECT_EQ(cd[10], 0);
  EXPECT_EQ(cd[11], 0);
  // Sizes match the uncompressed data.
  EXPECT_EQ(cd[20], 4);
  EXPECT_EQ(cd[24], 4);
  // Name follows the fixed part.
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(cd + 46), 9),
            "entry.txt");
}

}  // namespace
}  // namespace G
