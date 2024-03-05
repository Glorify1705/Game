#include "clock.h"
#include "filesystem.h"
#include "lua.h"
#include "packer.h"
#include "physfs.h"
#include "zip.h"

namespace G {
namespace {

template <typename T>
class FlatbufferAllocator : public flatbuffers::Allocator {
 public:
  explicit FlatbufferAllocator(T* allocator) : allocator_(allocator) {}
  uint8_t* allocate(size_t size) override {
    return reinterpret_cast<uint8_t*>(allocator_->Alloc(size, /*align=*/1));
  }
  void deallocate(uint8_t* p, size_t size) override {
    allocator_->Dealloc(p, size);
  }

 private:
  T* const allocator_ = nullptr;
};

class Packer {
 public:
  Packer(Allocator* allocator);

  Assets* HandleFiles();

 private:
  Assets* Finish();

  void* Alloc(void* ptr, size_t osize, size_t nsize);

  static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<Packer*>(ud)->Alloc(ptr, osize, nsize);
  }

  void HandleQoiImage(std::string_view filename, uint8_t* buf, size_t size);

  void HandleScript(std::string_view filename, uint8_t* buf, size_t size);

  void HandleSpritesheet(std::string_view filename, uint8_t* buf, size_t size);

  void HandleOggSound(std::string_view filename, uint8_t* buf, size_t size);

  void HandleWavSound(std::string_view filename, uint8_t* buf, size_t size);

  void HandleFont(std::string_view filename, uint8_t* buf, size_t size);

  void HandleVertexShader(std::string_view filename, uint8_t* buf, size_t size);

  void HandleFragmentShader(std::string_view filename, uint8_t* buf,
                            size_t size);

  void HandleTextFile(std::string_view filename, uint8_t* buf, size_t size);

  const int64_t start_secs_;
  Allocator* allocator_;

  FlatbufferAllocator<Allocator> fbs_wrapper_;
  flatbuffers::FlatBufferBuilder fbs_;
  WithAllocator<std::vector<flatbuffers::Offset<ImageAsset>>, Allocator>
      images_;
  WithAllocator<std::vector<flatbuffers::Offset<ScriptAsset>>, Allocator>
      scripts_;
  WithAllocator<std::vector<flatbuffers::Offset<SpritesheetAsset>>, Allocator>
      spritesheets_;
  WithAllocator<std::vector<flatbuffers::Offset<SpriteAsset>>, Allocator>
      sprites_;
  WithAllocator<std::vector<flatbuffers::Offset<SoundAsset>>, Allocator>
      sounds_;
  WithAllocator<std::vector<flatbuffers::Offset<FontAsset>>, Allocator> fonts_;
  WithAllocator<std::vector<flatbuffers::Offset<ShaderAsset>>, Allocator>
      shaders_;
  WithAllocator<std::vector<flatbuffers::Offset<TextFileAsset>>, Allocator>
      text_files_;
};

Packer::Packer(Allocator* allocator)
    : start_secs_(NowInSeconds()),
      allocator_(allocator),
      fbs_wrapper_(allocator_),
      fbs_(Gigabytes(1), &fbs_wrapper_),
      images_(allocator_),
      scripts_(allocator_),
      spritesheets_(allocator_),
      sprites_(allocator_),
      sounds_(allocator_),
      fonts_(allocator_),
      shaders_(allocator_),
      text_files_(allocator_) {}

Assets* Packer::Finish() {
  auto image_vec = fbs_.CreateVector(images_);
  auto scripts_vec = fbs_.CreateVector(scripts_);
  auto spritesheets_vec = fbs_.CreateVector(spritesheets_);
  auto sprites_vec = fbs_.CreateVector(sprites_);
  auto sounds_vec = fbs_.CreateVector(sounds_);
  auto fonts_vec = fbs_.CreateVector(fonts_);
  auto shaders_vec = fbs_.CreateVector(shaders_);
  auto text_files_vec = fbs_.CreateVector(text_files_);
  AssetsPackBuilder assets(fbs_);
  assets.add_images(image_vec);
  assets.add_scripts(scripts_vec);
  assets.add_spritesheets(spritesheets_vec);
  assets.add_sprites(sprites_vec);
  assets.add_sounds(sounds_vec);
  assets.add_fonts(fonts_vec);
  assets.add_shaders(shaders_vec);
  assets.add_texts(text_files_vec);
  fbs_.Finish(assets.Finish());
  // TODO: Avoid the copy here by releasing the pointer explicitly.
  auto* buffer = allocator_->Alloc(fbs_.GetSize(), /*align=*/16);
  std::memcpy(buffer, fbs_.GetBufferPointer(), fbs_.GetSize());
  return New<Assets>(allocator_, GetAssetsPack(buffer), fbs_.GetSize());
}

void* Packer::Alloc(void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    if (ptr != nullptr) allocator_->Dealloc(ptr, osize);
    return nullptr;
  }
  if (ptr == nullptr) {
    return allocator_->Alloc(nsize, /*align=*/1);
  }
  return allocator_->Realloc(ptr, osize, nsize, /*align=*/1);
}

void Packer::HandleQoiImage(std::string_view filename, uint8_t* buf,
                            size_t size) {
  QoiDesc desc;
  QoiDecode(buf, size, &desc, /*components=*/4, allocator_);
  images_.push_back(CreateImageAsset(fbs_, fbs_.CreateString(filename),
                                     desc.width, desc.height, desc.channels,
                                     fbs_.CreateVector(buf, size)));
}

void Packer::HandleScript(std::string_view filename, uint8_t* buf,
                          size_t size) {
  scripts_.push_back(CreateScriptAsset(fbs_, fbs_.CreateString(filename),
                                       fbs_.CreateVector(buf, size)));
}

void Packer::HandleSpritesheet(std::string_view filename, uint8_t* buf,
                               size_t size) {
  WithAllocator<std::vector<flatbuffers::Offset<SpriteAsset>>, Allocator>
      sprites(allocator_);
  uint32_t spritesheet_index = spritesheets_.size();
  auto* state = lua_newstate(&Packer::LuaAlloc, this);
  CHECK(luaL_loadbuffer(state, reinterpret_cast<const char*>(buf), size,
                        filename.data()) == 0,
        "Failed to load ", filename, ": ", luaL_checkstring(state, -1));
  CHECK(lua_pcall(state, 0, LUA_MULTRET, 0) == 0,
        "Failed to load script: ", filename, ": ", luaL_checkstring(state, -1));
  lua_pushstring(state, "atlas");
  lua_gettable(state, -2);
  auto str = fbs_.CreateString(luaL_checkstring(state, -1));
  lua_pop(state, 1);
  lua_pushstring(state, "width");
  lua_gettable(state, -2);
  const int width = lua_tonumber(state, -1);
  lua_pop(state, 1);
  lua_pushstring(state, "height");
  lua_gettable(state, -2);
  const int height = lua_tonumber(state, -1);
  lua_pop(state, 1);
  lua_pushstring(state, "sprites");
  lua_gettable(state, -2);
  for (lua_pushnil(state); lua_next(state, -2); lua_pop(state, 1)) {
    lua_pushstring(state, "name");
    lua_gettable(state, -2);
    FixedStringBuffer<kMaxLogLineLength> namebuf(luaL_checkstring(state, -1));
    auto name = fbs_.CreateString(namebuf.str());
    lua_pop(state, 1);

    auto get_number = [&](const char* name) {
      lua_pushstring(state, name);
      lua_gettable(state, -2);
      uint32_t result = luaL_checknumber(state, -1);
      lua_pop(state, 1);
      return result;
    };

    const uint32_t x = get_number("x");
    const uint32_t y = get_number("y");
    const uint32_t w = get_number("width");
    const uint32_t h = get_number("height");

    auto sprite = CreateSpriteAsset(fbs_, name, spritesheet_index, x, y, w, h);
    sprites.push_back(sprite);
    sprites_.push_back(sprite);
  }
  lua_pop(state, 1);
  lua_close(state);
  spritesheets_.push_back(
      CreateSpritesheetAsset(fbs_, fbs_.CreateString(filename), str, width,
                             height, fbs_.CreateVector(sprites)));
}

void Packer::HandleOggSound(std::string_view filename, uint8_t* buf,
                            size_t size) {
  sounds_.push_back(CreateSoundAsset(fbs_, fbs_.CreateString(filename),
                                     SoundType::OGG,
                                     fbs_.CreateVector(buf, size)));
}

void Packer::HandleWavSound(std::string_view filename, uint8_t* buf,
                            size_t size) {
  sounds_.push_back(CreateSoundAsset(fbs_, fbs_.CreateString(filename),
                                     SoundType::WAV,
                                     fbs_.CreateVector(buf, size)));
}

void Packer::HandleFont(std::string_view filename, uint8_t* buf, size_t size) {
  fonts_.push_back(CreateFontAsset(fbs_, fbs_.CreateString(filename),
                                   fbs_.CreateVector(buf, size)));
}

void Packer::HandleVertexShader(std::string_view filename, uint8_t* buf,
                                size_t size) {
  shaders_.push_back(CreateShaderAsset(
      fbs_, fbs_.CreateString(filename), ShaderType::VERTEX,
      fbs_.CreateString(reinterpret_cast<const char*>(buf), size)));
}

void Packer::HandleFragmentShader(std::string_view filename, uint8_t* buf,
                                  size_t size) {
  shaders_.push_back(CreateShaderAsset(
      fbs_, fbs_.CreateString(filename), ShaderType::FRAGMENT,
      fbs_.CreateString(reinterpret_cast<const char*>(buf), size)));
}

void Packer::HandleTextFile(std::string_view filename, uint8_t* buf,
                            size_t size) {
  text_files_.push_back(CreateTextFileAsset(fbs_, fbs_.CreateString(filename),
                                            fbs_.CreateVector(buf, size)));
}

Assets* Packer::HandleFiles() {
  TIMER("Packing assets from directory");
  // TODO: This uses insane amounts of memory and needs to be reduced.
  struct FileHandler {
    std::string_view extension;
    void (Packer::*method)(std::string_view filename, uint8_t* buf,
                           size_t size);
  };

  static constexpr FileHandler kHandlers[] = {
      {".lua", &Packer::HandleScript},
      {".fnl", &Packer::HandleScript},
      {".qoi", &Packer::HandleQoiImage},
      {".sprites", &Packer::HandleSpritesheet},
      {".ogg", &Packer::HandleOggSound},
      {".ttf", &Packer::HandleFont},
      {".wav", &Packer::HandleWavSound},
      {".vert", &Packer::HandleVertexShader},
      {".frag", &Packer::HandleFragmentShader},
      {".txt", &Packer::HandleTextFile}};

  char** rc = PHYSFS_enumerateFiles("/");
  CHECK(rc != nullptr, "Failed to enumerate files: ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  auto& packer = *this;
  for (char** p = rc; *p; p++) {
    const char* path = *p;
    std::string_view fname = Basename(path);
    bool handled = false;
    for (auto& handler : kHandlers) {
      if (HasSuffix(path, handler.extension)) {
        TIMER("Handling file ", path);
        auto* handle = PHYSFS_openRead(path);
        CHECK(handle != nullptr, "Could not read ", path, ": ",
              PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        const size_t bytes = PHYSFS_fileLength(handle);
        auto* buffer =
            static_cast<uint8_t*>(allocator_->Alloc(bytes, /*align=*/1));
        const size_t read_bytes = PHYSFS_readBytes(handle, buffer, bytes);
        CHECK(read_bytes == bytes, " failed to read ", path,
              " error = ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        CHECK(PHYSFS_close(handle), "failed to finish reading ", path, ": ",
              PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        auto method = handler.method;
        (packer.*method)(fname, buffer, bytes);
        allocator_->Dealloc(buffer, bytes);
        handled = true;
        break;
      }
    }
    CHECK(handled, "No handler for file ", path);
  }
  PHYSFS_freeList(rc);
  return Finish();
}

}  // namespace

Assets* PackFiles(const char* source_directory, Allocator* allocator) {
  PHYSFS_CHECK(PHYSFS_mount(source_directory, "/", 1),
               " while trying to mount directory ", source_directory);
  Packer packer(allocator);
  return packer.HandleFiles();
}

Assets* ReadAssets(const char* assets_zip_file, Allocator* allocator) {
  TIMER("Reading assets from file");
  constexpr char kAssetFileName[] = "assets.bin";
  zip_error_t zip_error;
  int zip_error_code;
  zip_t* zip_file = zip_open(assets_zip_file, ZIP_RDONLY, &zip_error_code);
  if (zip_file == nullptr) {
    zip_error_init_with_code(&zip_error, zip_error_code);
    DIE("Failed to open ", assets_zip_file,
        " as a zip file: ", zip_error_strerror(&zip_error));
  }
  const int64_t num_entries = zip_get_num_entries(zip_file, ZIP_FL_UNCHANGED);
  if (num_entries == 0) LOG("Zip file has no entries");
  CHECK(num_entries == 1, "Expected one file");
  const char* filename = zip_get_name(zip_file, 0, ZIP_FL_ENC_RAW);
  CHECK(!std::strcmp(filename, kAssetFileName), "Expected a file named ",
        kAssetFileName, " found ", filename);
  for (int i = 0; i < num_entries; ++i) {
    LOG("Found file ", zip_get_name(zip_file, i, ZIP_FL_ENC_RAW),
        " in zip file");
  }
  zip_stat_t stat;
  if (zip_stat(zip_file, kAssetFileName, ZIP_FL_ENC_UTF_8, &stat) == -1) {
    DIE("Failed to open ", assets_zip_file,
        " as a zip file: ", zip_strerror(zip_file));
  }
  auto* assets_file = zip_fopen(zip_file, kAssetFileName, /*flags=*/0);
  if (assets_file == nullptr) {
    DIE("Failed to decompress ", assets_zip_file, ": ", zip_strerror(zip_file));
  }
  auto* buffer = NewArray<uint8_t>(stat.size, allocator);
  if (zip_fread(assets_file, buffer, stat.size) == -1) {
    DIE("Failed to read decompressed file");
  }
  LOG("Read assets file (", stat.size, " bytes)");
  return New<Assets>(allocator, GetAssetsPack(buffer), stat.size);
}

bool WriteAssets(const Assets& assets, const char* output_file) {
  zip_error_t zip_error;
  int zip_error_code;
  zip_t* zip_file =
      zip_open(output_file, ZIP_CREATE | ZIP_TRUNCATE, &zip_error_code);
  if (zip_file == nullptr) {
    zip_error_init_with_code(&zip_error, zip_error_code);
    DIE("Failed to open ", output_file,
        " as a zip file: ", zip_error_strerror(&zip_error));
  }
  zip_source_t* zip_source = zip_source_buffer(
      zip_file, assets.PackedAssets(), assets.PackerAssetSize(), /*freep=*/0);
  if (zip_source == nullptr) {
    DIE("Failed to create zip file ", output_file, ": ",
        zip_strerror(zip_file));
  }
  if (zip_file_add(zip_file, "assets.bin", zip_source, ZIP_FL_ENC_UTF_8) ==
      -1) {
    DIE("Could not add file to the archive ", output_file, ": ",
        zip_strerror(zip_file));
  }
  if (zip_close(zip_file) == -1) {
    DIE("Could not close archive ", output_file, ": ", zip_strerror(zip_file));
  }
  return true;
}

}  // namespace G