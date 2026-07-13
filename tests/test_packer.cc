#include <algorithm>
#include <cstdio>
#include <cstring>

#include "assets.h"
#include "blob_store.h"
#include "executor.h"
#include "gtest/gtest.h"
#include "libraries/rapidhash.h"
#include "libraries/sqlite3.h"
#include "libraries/stb_image_write.h"
#include "packer.h"
#include "physfs.h"
#include "platform.h"
#include "sqlite_helpers.h"
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

// Removes every file in dir, then dir itself.
void RemoveDirRecursive(const char* dir) {
  if (!DirectoryExists(dir)) return;
  IterateDirectory(
      dir,
      [](const DirEntry& entry, void* ud) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/%s", static_cast<const char*>(ud),
                      entry.name);
        std::remove(path);
      },
      const_cast<char*>(dir))
      .release_value();
  std::remove(dir);
}

constexpr char kMainLua[] = "return { init = function() end }\n";
constexpr char kConfJson[] = "{\"title\": \"packer test\"}\n";
constexpr char kNotesTxt[] = "some notes\n";
constexpr char kVertShader[] = "void main() { gl_Position = vec4(0.0); }\n";

class PackerTest : public ::testing::Test {
 protected:
  SystemAllocator allocator_;
  // The packer and DbAssets treat their allocator as an arena (content
  // buffers are never individually freed), so give them a real one.
  ArenaAllocator arena_{&allocator_, Megabytes(512)};
  char root_[512] = {};
  char source_dir_[640] = {};
  char blobs_dir_[640] = {};
  char db_path_[640] = {};
  sqlite3* db_ = nullptr;

  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::snprintf(root_, sizeof(root_), "%s/game_packer_test_%d_%s", TempDir(),
                  static_cast<int>(getpid()), info->name());
    std::snprintf(source_dir_, sizeof(source_dir_), "%s/src", root_);
    std::snprintf(blobs_dir_, sizeof(blobs_dir_), "%s/blobs", root_);
    std::snprintf(db_path_, sizeof(db_path_), "%s/assets.sqlite3", root_);
    ASSERT_FALSE(MakeDirs(source_dir_).is_error());
    ASSERT_NE(PHYSFS_init("test"), 0);

    WriteSource("main.lua", kMainLua);
    WriteSource("conf.json", kConfJson);
    WriteSource("notes.txt", kNotesTxt);
    WriteSource("test.vert", kVertShader);
    WriteTestPng();

    ASSERT_EQ(sqlite3_open(db_path_, &db_), SQLITE_OK);
    InitializeAssetDb(db_);
  }

  void TearDown() override {
    if (db_ != nullptr) sqlite3_close(db_);
    PHYSFS_deinit();
    RemoveDirRecursive(source_dir_);
    RemoveDirRecursive(blobs_dir_);
    std::remove(db_path_);
    std::remove(root_);
  }

  void WriteSource(const char* name, const char* contents) {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", source_dir_, name);
    ASSERT_FALSE(WriteFile(path, contents).is_error());
  }

  // Writes a 2x2 RGBA PNG that the packer transcodes to QOI.
  void WriteTestPng() {
    const uint8_t pixels[16] = {255, 0, 0,   255, 0,   255, 0,   255,
                                0,   0, 255, 255, 255, 255, 255, 255};
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/pixel.png", source_dir_);
    ASSERT_NE(stbi_write_png(path, 2, 2, 4, pixels, 8), 0);
  }

  AssetWriteResult Pack() {
    BlobStore blobs = MUST(BlobStore::Create(blobs_dir_));
    InlineExecutor executor;
    return MUST(WriteAssetsToDb(source_dir_, db_, &blobs, &arena_, &executor));
  }

  bool TableExists(const char* name) {
    SqlStmt stmt(db_,
                 "SELECT 1 FROM sqlite_master WHERE type = 'table' "
                 "AND name = ?");
    EXPECT_TRUE(stmt.ok());
    stmt.BindText(1, name);
    return MUST(stmt.Step());
  }

  int UserVersion() {
    SqlStmt stmt(db_, "PRAGMA user_version");
    EXPECT_TRUE(stmt.ok());
    EXPECT_TRUE(MUST(stmt.Step()));
    return stmt.ColumnInt(0);
  }
};

TEST_F(PackerTest, WritesMetadataOnlyDatabase) {
  const AssetWriteResult result = Pack();
  // main.lua, conf.json, notes.txt, test.vert, pixel.png (+ debug font is
  // not counted in written_files).
  EXPECT_EQ(result.written_files, 5u);

  // Blob-content tables are gone from the schema.
  EXPECT_FALSE(TableExists("scripts"));
  EXPECT_FALSE(TableExists("shaders"));
  EXPECT_FALSE(TableExists("fonts"));
  EXPECT_FALSE(TableExists("text_files"));
  EXPECT_FALSE(TableExists("proto_descriptors"));
  EXPECT_EQ(UserVersion(), kAssetDbSchemaVersion);

  // Every asset row references a blob.
  SqlStmt stmt(db_, "SELECT name, blob_hash FROM asset_metadata");
  ASSERT_TRUE(stmt.ok());
  size_t rows = 0;
  while (MUST(stmt.Step())) {
    rows++;
    EXPECT_NE(stmt.ColumnInt64(1), 0) << "no blob for " << stmt.ColumnText(0);
  }
  EXPECT_EQ(rows, 6u);  // 5 sources + debug_font.ttf.

  // The image row kept its dimensions but has no contents column.
  SqlStmt img(db_,
              "SELECT width, height, components FROM images "
              "WHERE name = 'pixel.png'");
  ASSERT_TRUE(img.ok());
  ASSERT_TRUE(MUST(img.Step()));
  EXPECT_EQ(img.ColumnInt(0), 2);
  EXPECT_EQ(img.ColumnInt(1), 2);
}

TEST_F(PackerTest, BlobFilesMatchHashesAndSizes) {
  Pack();
  SqlStmt stmt(db_, "SELECT name, size, blob_hash FROM asset_metadata");
  ASSERT_TRUE(stmt.ok());
  while (MUST(stmt.Step())) {
    const size_t size = stmt.ColumnInt64(1);
    const uint64_t blob_hash = static_cast<uint64_t>(stmt.ColumnInt64(2));
    char name[17];
    FormatBlobName(blob_hash, name);
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", blobs_dir_, name);
    uint8_t* contents = nullptr;
    const size_t read = MUST(ReadEntireFile(path, &contents, &allocator_));
    EXPECT_EQ(read, size) << "blob size mismatch for " << stmt.ColumnText(0);
    EXPECT_EQ(rapidhash(contents, read), blob_hash)
        << "blob content mismatch for " << stmt.ColumnText(0);
    allocator_.Dealloc(contents, read);
  }
}

TEST_F(PackerTest, IdenticalContentDeduplicatesBlobs) {
  WriteSource("copy_a.txt", kNotesTxt);
  WriteSource("copy_b.txt", kNotesTxt);
  Pack();

  SqlStmt stmt(db_,
               "SELECT blob_hash FROM asset_metadata "
               "WHERE name IN ('notes.txt', 'copy_a.txt', 'copy_b.txt')");
  ASSERT_TRUE(stmt.ok());
  int64_t hashes[3] = {};
  size_t rows = 0;
  while (MUST(stmt.Step())) {
    ASSERT_LT(rows, 3u);
    hashes[rows++] = stmt.ColumnInt64(0);
  }
  ASSERT_EQ(rows, 3u);
  EXPECT_EQ(hashes[0], hashes[1]);
  EXPECT_EQ(hashes[1], hashes[2]);

  // All three rows point at a single blob file.
  char name[17];
  FormatBlobName(static_cast<uint64_t>(hashes[0]), name);
  char path[1024];
  std::snprintf(path, sizeof(path), "%s/%s", blobs_dir_, name);
  EXPECT_TRUE(FileExists(path));
}

TEST_F(PackerTest, RepackSkipsUnchangedFiles) {
  const AssetWriteResult first = Pack();
  EXPECT_EQ(first.written_files, 5u);
  const AssetWriteResult second = Pack();
  EXPECT_EQ(second.written_files, 0u);
}

TEST_F(PackerTest, LegacySchemaIsWipedAndRebuilt) {
  // Recreate a v1-style database: blob tables, no user_version stamp.
  sqlite3_close(db_);
  std::remove(db_path_);
  ASSERT_EQ(sqlite3_open(db_path_, &db_), SQLITE_OK);
  ASSERT_FALSE(SqlExec(db_,
                       "CREATE TABLE scripts(id INTEGER PRIMARY KEY, "
                       "name VARCHAR(255), contents BLOB)")
                   .is_error());
  ASSERT_FALSE(
      SqlExec(db_, "INSERT INTO scripts (name, contents) VALUES ('x', 'y')")
          .is_error());
  ASSERT_EQ(UserVersion(), 0);

  InitializeAssetDb(db_);

  EXPECT_EQ(UserVersion(), kAssetDbSchemaVersion);
  EXPECT_FALSE(TableExists("scripts"));
  EXPECT_TRUE(TableExists("asset_metadata"));
}

struct CapturedAssets {
  char script_contents[256] = {};
  size_t script_size = 0;
  int script_loads = 0;
  int shader_loads = 0;
  DbAssets::ShaderType shader_type = DbAssets::ShaderType::kFragment;
};

ErrorOr<void> CaptureScript(DbAssets::Script* script, void* ud) {
  auto* captured = static_cast<CapturedAssets*>(ud);
  captured->script_loads++;
  captured->script_size = script->size;
  std::memcpy(captured->script_contents, script->contents,
              std::min(script->size + 1, sizeof(captured->script_contents)));
  return {};
}

ErrorOr<void> CaptureShader(DbAssets::Shader* shader, void* ud) {
  auto* captured = static_cast<CapturedAssets*>(ud);
  captured->shader_loads++;
  captured->shader_type = shader->type;
  return {};
}

TEST_F(PackerTest, DbAssetsLoadsFromBlobStore) {
  Pack();
  ASSERT_NE(PHYSFS_mount(blobs_dir_, kBlobMountPoint, /*append=*/0), 0);

  DbAssets assets(db_, &arena_);
  CapturedAssets captured;
  assets.RegisterScriptLoad(CaptureScript, &captured);
  assets.RegisterShaderLoad(CaptureShader, &captured);
  assets.Load();

  EXPECT_EQ(captured.script_loads, 1);
  EXPECT_EQ(captured.script_size, sizeof(kMainLua) - 1);
  // Contents round-trip NUL-terminated.
  EXPECT_STREQ(captured.script_contents, kMainLua);
  EXPECT_EQ(captured.shader_loads, 1);
  EXPECT_EQ(captured.shader_type, DbAssets::ShaderType::kVertex);

  // Text files are retained for lookup.
  DbAssets::TextFile* conf = assets.LookupTextFile("conf.json");
  ASSERT_NE(conf, nullptr);
  EXPECT_EQ(
      std::string_view(reinterpret_cast<char*>(conf->contents), conf->size),
      kConfJson);

  // Checksums track the source file hash.
  EXPECT_EQ(assets.GetChecksum("main.lua"),
            rapidhash(kMainLua, sizeof(kMainLua) - 1));

  // A second Load() skips everything by checksum.
  assets.Load();
  EXPECT_EQ(captured.script_loads, 1);

  ASSERT_NE(PHYSFS_unmount(blobs_dir_), 0);
}

TEST_F(PackerTest, PackagedZipFormatEndToEnd) {
  Pack();

  // Build assets.zip the same way `game package` does: every referenced
  // blob, named by hash.
  char zip_path[640];
  std::snprintf(zip_path, sizeof(zip_path), "%s/assets.zip", root_);
  {
    ZipWriter zip(&allocator_);
    ASSERT_FALSE(zip.Open(zip_path).is_error());
    SqlStmt stmt(db_,
                 "SELECT DISTINCT blob_hash FROM asset_metadata "
                 "WHERE blob_hash != 0");
    ASSERT_TRUE(stmt.ok());
    while (MUST(stmt.Step())) {
      char name[17];
      FormatBlobName(static_cast<uint64_t>(stmt.ColumnInt64(0)), name);
      char blob_path[1024];
      std::snprintf(blob_path, sizeof(blob_path), "%s/%s", blobs_dir_, name);
      uint8_t* contents = nullptr;
      const size_t size =
          MUST(ReadEntireFile(blob_path, &contents, &allocator_));
      ASSERT_FALSE(zip.AddEntry(name, ByteSlice(contents, size)).is_error());
      allocator_.Dealloc(contents, size);
    }
    ASSERT_FALSE(zip.Finish().is_error());
  }

  // Load assets with only the zip mounted, as a packaged game would.
  ASSERT_NE(PHYSFS_mount(zip_path, kBlobMountPoint, /*append=*/0), 0);
  DbAssets assets(db_, &arena_);
  CapturedAssets captured;
  assets.RegisterScriptLoad(CaptureScript, &captured);
  assets.Load();
  EXPECT_EQ(captured.script_loads, 1);
  EXPECT_STREQ(captured.script_contents, kMainLua);
  ASSERT_NE(PHYSFS_unmount(zip_path), 0);
  std::remove(zip_path);
}

}  // namespace
}  // namespace G
