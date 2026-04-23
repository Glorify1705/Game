#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"
#include "platform.h"
#include "test_fixture.h"

namespace G {
namespace {

// Returns a platform-appropriate temporary directory path (no trailing slash).
const char* TempDir() {
#ifdef _WIN32
  static char buf[MAX_PATH];
  DWORD len = GetTempPathA(MAX_PATH, buf);
  // Strip trailing backslash if present.
  if (len > 0 && (buf[len - 1] == '\\' || buf[len - 1] == '/')) {
    buf[len - 1] = '\0';
  }
  return buf;
#else
  return "/tmp";
#endif
}

// Returns true if the path is absolute on the current platform.
bool IsAbsolutePath(const char* path) {
#ifdef _WIN32
  return path && std::isalpha(static_cast<unsigned char>(path[0])) &&
         path[1] == ':';
#else
  return path && path[0] == '/';
#endif
}

}  // namespace

// Helper: create a unique temp directory for each test.
class PlatformTest : public BaseTest {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    snprintf(tmp_dir_, sizeof(tmp_dir_), "%s/game_test_%d_%s", TempDir(),
             static_cast<int>(getpid()), info->name());
    ASSERT_FALSE(MakeDirs(tmp_dir_).is_error());
  }

  void TearDown() override {
    // Clean up files we created.
    for (const char* f : created_files_) {
      remove(f);
    }
    // Remove directories in reverse order.
    for (int i = static_cast<int>(created_dirs_.size()) - 1; i >= 0; --i) {
      rmdir(created_dirs_[i]);
    }
    rmdir(tmp_dir_);
  }

  // Track created paths for cleanup.
  const char* TmpPath(const char* name) {
    auto& buf = path_bufs_[next_buf_++ % kMaxPaths];
    snprintf(buf, sizeof(buf), "%s/%s", tmp_dir_, name);
    return buf;
  }

  void TrackFile(const char* path) { created_files_.push_back(path); }
  void TrackDir(const char* path) { created_dirs_.push_back(path); }

  char tmp_dir_[256];

 private:
  static constexpr int kMaxPaths = 16;
  char path_bufs_[kMaxPaths][512];
  int next_buf_ = 0;
  std::vector<const char*> created_files_;
  std::vector<const char*> created_dirs_;
};

// FileExists

TEST_F(PlatformTest, FileExistsTrue) {
  const char* path = TmpPath("exists.txt");
  ASSERT_FALSE(WriteFile(path, "hello").is_error());
  TrackFile(path);
  EXPECT_TRUE(FileExists(path));
}

TEST_F(PlatformTest, FileExistsFalse) {
  EXPECT_FALSE(FileExists(TmpPath("nonexistent.txt")));
}

// DirectoryExists

TEST_F(PlatformTest, DirectoryExistsTrue) {
  EXPECT_TRUE(DirectoryExists(tmp_dir_));
}

TEST_F(PlatformTest, DirectoryExistsFalse) {
  EXPECT_FALSE(DirectoryExists(TmpPath("no_such_dir")));
}

TEST_F(PlatformTest, DirectoryExistsForFile) {
  const char* path = TmpPath("afile.txt");
  ASSERT_FALSE(WriteFile(path, "data").is_error());
  TrackFile(path);
  EXPECT_FALSE(DirectoryExists(path));
}

// MakeDir / MakeDirs

TEST_F(PlatformTest, MakeDirSingle) {
  const char* path = TmpPath("newdir");
  EXPECT_FALSE(MakeDir(path).is_error());
  TrackDir(path);
  EXPECT_TRUE(DirectoryExists(path));
}

TEST_F(PlatformTest, MakeDirIdempotent) {
  const char* path = TmpPath("idempotent");
  EXPECT_FALSE(MakeDir(path).is_error());
  TrackDir(path);
  // Creating again should succeed (already exists).
  EXPECT_FALSE(MakeDir(path).is_error());
}

TEST_F(PlatformTest, MakeDirsRecursive) {
  const char* path = TmpPath("a/b/c");
  EXPECT_FALSE(MakeDirs(path).is_error());
  EXPECT_TRUE(DirectoryExists(path));
  // Track for cleanup (deepest first).
  TrackDir(TmpPath("a/b/c"));
  TrackDir(TmpPath("a/b"));
  TrackDir(TmpPath("a"));
}

// WriteFile / ReadEntireFile

TEST_F(PlatformTest, WriteAndReadFile) {
  const char* path = TmpPath("readwrite.txt");
  ASSERT_FALSE(WriteFile(path, "test content").is_error());
  TrackFile(path);

  uint8_t* data = nullptr;
  auto result = ReadEntireFile(path, &data, alloc);
  ASSERT_FALSE(result.is_error());
  size_t len = result.value();
  EXPECT_EQ(len, 12u);
  EXPECT_EQ(std::string_view(reinterpret_cast<char*>(data), len),
            "test content");
  alloc->Dealloc(data, len);
}

TEST_F(PlatformTest, ReadNonexistentFile) {
  uint8_t* data = nullptr;
  auto result = ReadEntireFile(TmpPath("nope.txt"), &data, alloc);
  EXPECT_TRUE(result.is_error());
}

// WriteEntireFile

TEST_F(PlatformTest, WriteEntireFileRoundTrip) {
  const char* path = TmpPath("binary.bin");
  uint8_t payload[] = {0x00, 0xFF, 0xAB, 0xCD, 0x00, 0x42};
  ByteSlice slice(payload, sizeof(payload));
  ASSERT_FALSE(WriteEntireFile(path, slice).is_error());
  TrackFile(path);

  uint8_t* out = nullptr;
  auto result = ReadEntireFile(path, &out, alloc);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), sizeof(payload));
  EXPECT_EQ(std::memcmp(out, payload, sizeof(payload)), 0);
  alloc->Dealloc(out, result.value());
}

// CopyFile

TEST_F(PlatformTest, CopyFileContents) {
  const char* src = TmpPath("src.txt");
  const char* dst = TmpPath("dst.txt");
  ASSERT_FALSE(WriteFile(src, "copy me").is_error());
  TrackFile(src);
  ASSERT_FALSE(CopyFile(src, dst).is_error());
  TrackFile(dst);

  uint8_t* data = nullptr;
  auto result = ReadEntireFile(dst, &data, alloc);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(std::string_view(reinterpret_cast<char*>(data), result.value()),
            "copy me");
  alloc->Dealloc(data, result.value());
}

TEST_F(PlatformTest, CopyNonexistentFile) {
  auto result = CopyFile(TmpPath("no.txt"), TmpPath("also_no.txt"));
  EXPECT_TRUE(result.is_error());
}

// MakeExecutable

#ifndef _WIN32
TEST_F(PlatformTest, MakeExecutableSetsPermission) {
  const char* path = TmpPath("script.sh");
  ASSERT_FALSE(WriteFile(path, "#!/bin/sh\necho hi\n").is_error());
  TrackFile(path);
  ASSERT_FALSE(MakeExecutable(path).is_error());

  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  EXPECT_TRUE(st.st_mode & S_IXUSR);
}
#endif

// IterateDirectory

TEST_F(PlatformTest, IterateDirectoryListsEntries) {
  const char* f1 = TmpPath("file1.txt");
  const char* f2 = TmpPath("file2.txt");
  const char* sub = TmpPath("subdir");
  ASSERT_FALSE(WriteFile(f1, "a").is_error());
  TrackFile(f1);
  ASSERT_FALSE(WriteFile(f2, "b").is_error());
  TrackFile(f2);
  ASSERT_FALSE(MakeDir(sub).is_error());
  TrackDir(sub);

  struct Collector {
    int files = 0;
    int dirs = 0;
  } collector;

  auto result = IterateDirectory(
      tmp_dir_,
      [](const DirEntry& entry, void* ud) {
        auto* c = static_cast<Collector*>(ud);
        if (entry.type == DirEntryType::kFile) c->files++;
        if (entry.type == DirEntryType::kDirectory) c->dirs++;
      },
      &collector);

  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(collector.files, 2);
  EXPECT_EQ(collector.dirs, 1);
}

// GetExePath / GetCwd

TEST_F(PlatformTest, GetExePathSucceeds) {
  char buf[1024];
  EXPECT_FALSE(GetExePath(buf, sizeof(buf)).is_error());
  EXPECT_GT(std::strlen(buf), 0u);
}

TEST_F(PlatformTest, GetCwdSucceeds) {
  char buf[1024];
  EXPECT_FALSE(GetCwd(buf, sizeof(buf)).is_error());
  EXPECT_GT(std::strlen(buf), 0u);
}

// AbsolutePath

TEST_F(PlatformTest, AbsolutePathResolves) {
  const char* abs = AbsolutePath(tmp_dir_);
  EXPECT_NE(abs, nullptr);
  EXPECT_TRUE(IsAbsolutePath(abs));
}

// WriteFileF

TEST_F(PlatformTest, WriteFileFFormatted) {
  const char* path = TmpPath("formatted.txt");
  ASSERT_FALSE(WriteFileF(path, "x=%d y=%s", 42, "hello").is_error());
  TrackFile(path);

  uint8_t* data = nullptr;
  auto result = ReadEntireFile(path, &data, alloc);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(std::string_view(reinterpret_cast<char*>(data), result.value()),
            "x=42 y=hello");
  alloc->Dealloc(data, result.value());
}

// Empty file

TEST_F(PlatformTest, WriteAndReadEmptyFile) {
  const char* path = TmpPath("empty.txt");
  ASSERT_FALSE(WriteFile(path, "").is_error());
  TrackFile(path);

  uint8_t* data = nullptr;
  auto result = ReadEntireFile(path, &data, alloc);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(result.value(), 0u);
  alloc->Dealloc(data, 0);
}

}  // namespace G
