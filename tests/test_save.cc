#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"
#include "platform.h"
#include "save.h"

namespace G {

class SaveTest : public ::testing::Test {
 protected:
  SystemAllocator allocator_;
  char dir_[256] = {};

  void SetUp() override {
    // Use a temp directory for each test.
    const char* tmp = getenv("TMPDIR");
    if (tmp == nullptr) tmp = "/tmp";
    snprintf(dir_, sizeof(dir_), "%s/game_test_save_XXXXXX", tmp);
    ASSERT_NE(mkdtemp(dir_), nullptr);
  }

  void TearDown() override {
    // Clean up: remove database files and directory.
    char path[512];
    snprintf(path, sizeof(path), "%s/save.sqlite3", dir_);
    std::remove(path);
    snprintf(path, sizeof(path), "%s/save.sqlite3-wal", dir_);
    std::remove(path);
    snprintf(path, sizeof(path), "%s/save.sqlite3-shm", dir_);
    std::remove(path);
    std::remove(dir_);
  }

  Allocator* allocator() { return &allocator_; }
};

TEST_F(SaveTest, OpenAndClose) {
  Save save(allocator());
  ASSERT_FALSE(save.IsOpen());
  auto result = save.Open(dir_);
  ASSERT_FALSE(result.is_error()) << "Open failed";
  ASSERT_TRUE(save.IsOpen());
  save.Close();
  ASSERT_FALSE(save.IsOpen());
}

TEST_F(SaveTest, SetAndGet) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  const char* value = "hello";
  auto blob = MakeByteSlice(value, 5);
  ASSERT_FALSE(save.Set("test", "key1", blob).is_error());

  auto got = save.Get("test", "key1");
  ASSERT_FALSE(got.is_error());
  ByteSlice data = got.release_value();
  EXPECT_EQ(data.size(), 5u);
  EXPECT_EQ(std::memcmp(data.data(), "hello", 5), 0);
}

TEST_F(SaveTest, GetMissingKeyReturnsEmpty) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  auto got = save.Get("test", "nonexistent");
  ASSERT_FALSE(got.is_error());
  EXPECT_EQ(got.release_value().size(), 0u);
}

TEST_F(SaveTest, Has) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  EXPECT_FALSE(save.Has("test", "key1"));
  ASSERT_FALSE(save.Set("test", "key1", MakeByteSlice("x", 1)).is_error());
  EXPECT_TRUE(save.Has("test", "key1"));
  EXPECT_FALSE(save.Has("test", "key2"));
  EXPECT_FALSE(save.Has("other", "key1"));
}

TEST_F(SaveTest, Delete) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  ASSERT_FALSE(save.Set("test", "key1", MakeByteSlice("x", 1)).is_error());
  EXPECT_TRUE(save.Has("test", "key1"));
  ASSERT_FALSE(save.Delete("test", "key1").is_error());
  EXPECT_FALSE(save.Has("test", "key1"));
}

TEST_F(SaveTest, Clear) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  ASSERT_FALSE(save.Set("ns", "a", MakeByteSlice("1", 1)).is_error());
  ASSERT_FALSE(save.Set("ns", "b", MakeByteSlice("2", 1)).is_error());
  ASSERT_FALSE(save.Set("other", "c", MakeByteSlice("3", 1)).is_error());

  ASSERT_FALSE(save.Clear("ns").is_error());
  EXPECT_FALSE(save.Has("ns", "a"));
  EXPECT_FALSE(save.Has("ns", "b"));
  EXPECT_TRUE(save.Has("other", "c"));
}

TEST_F(SaveTest, ListKeys) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  ASSERT_FALSE(save.Set("ns", "alpha", MakeByteSlice("1", 1)).is_error());
  ASSERT_FALSE(save.Set("ns", "beta", MakeByteSlice("2", 1)).is_error());

  int count = 0;
  auto result = save.List(
      "ns",
      [](std::string_view key, ByteSlice, void* ud) {
        (*static_cast<int*>(ud))++;
      },
      &count);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(count, 2);
}

TEST_F(SaveTest, Namespaces) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  ASSERT_FALSE(save.Set("achievements", "a", MakeByteSlice("1", 1)).is_error());
  ASSERT_FALSE(save.Set("settings", "b", MakeByteSlice("2", 1)).is_error());

  int count = 0;
  auto result = save.Namespaces(
      [](std::string_view, void* ud) { (*static_cast<int*>(ud))++; }, &count);
  ASSERT_FALSE(result.is_error());
  EXPECT_EQ(count, 2);
}

TEST_F(SaveTest, PersistsAfterReopen) {
  {
    Save save(allocator());
    ASSERT_FALSE(save.Open(dir_).is_error());
    ASSERT_FALSE(
        save.Set("test", "persist", MakeByteSlice("data", 4)).is_error());
    save.Close();
  }
  {
    Save save(allocator());
    ASSERT_FALSE(save.Open(dir_).is_error());
    EXPECT_TRUE(save.Has("test", "persist"));
    auto got = save.Get("test", "persist");
    ASSERT_FALSE(got.is_error());
    ByteSlice data = got.release_value();
    EXPECT_EQ(data.size(), 4u);
    EXPECT_EQ(std::memcmp(data.data(), "data", 4), 0);
  }
}

TEST_F(SaveTest, OverwriteExistingKey) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());

  ASSERT_FALSE(save.Set("ns", "key", MakeByteSlice("old", 3)).is_error());
  ASSERT_FALSE(save.Set("ns", "key", MakeByteSlice("new", 3)).is_error());

  auto got = save.Get("ns", "key");
  ASSERT_FALSE(got.is_error());
  ByteSlice data = got.release_value();
  EXPECT_EQ(std::memcmp(data.data(), "new", 3), 0);
}

TEST_F(SaveTest, Flush) {
  Save save(allocator());
  ASSERT_FALSE(save.Open(dir_).is_error());
  ASSERT_FALSE(save.Set("test", "key", MakeByteSlice("val", 3)).is_error());
  ASSERT_FALSE(save.Flush().is_error());
}

}  // namespace G
