#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <type_traits>
#include <vector>

#include "allocators.h"
#include "assets_generated.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "image.h"
#include "libraries/pugixml.h"
#include "logging.h"
#include "physfs.h"
#include "units.h"
#include "zip.h"

namespace G {
namespace {

#define PHYSFS_CHECK(call, ...)                                \
  CHECK(call != 0, "Failed to execute " #call " with error: ", \
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()), ##__VA_ARGS__)

using Clock = std::chrono::high_resolution_clock;
using Time = std::chrono::time_point<Clock>;

int64_t NowInMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

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

constexpr size_t kMemorySize = Gigabytes(4);

using PackerAllocator = StaticAllocator<kMemorySize>;

static PackerAllocator* GlobalAllocator() {
  static auto* allocator = new PackerAllocator;
  return allocator;
}

void* GlobalAlloc(size_t s) { return GlobalAllocator()->Alloc(s, /*align=*/1); }

void GlobalDealloc(void*) {}

void* GlobalRealloc(void* p, size_t old_size, size_t new_size) {
  return GlobalAllocator()->Realloc(p, old_size, new_size, /*align=*/1);
}

#define STBI_MALLOC GlobalAlloc
#define STBI_FREE GlobalDealloc
#define STBI_REALLOC_SIZED GlobalRealloc

#define STB_IMAGE_IMPLEMENTATION
#include "libraries/stb_image.h"

std::string_view Basename(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '/';) {
    pos--;
  }
  return p[pos] == '/' ? p.substr(pos + 1) : p;
}

std::string_view WithoutExt(std::string_view p) {
  size_t pos = p.size() - 1;
  for (; pos != 0 && p[pos] != '.';) {
    pos--;
  }
  return p[pos] == '.' ? p.substr(0, pos) : p;
}

std::pair<uint8_t*, size_t> ReadWholeFile(const char* path) {
  auto* handle = PHYSFS_openRead(path);
  CHECK(handle != nullptr, "Could not read ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  const int64_t bytes = PHYSFS_fileLength(handle);
  auto* buffer = static_cast<uint8_t*>(GlobalAlloc(bytes));
  CHECK(PHYSFS_readBytes(handle, buffer, bytes) == bytes, " failed to read ",
        path, " error = ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  CHECK(PHYSFS_close(handle), "failed to finish reading ", path, ": ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  return {buffer, bytes};
}

class Packer {
 public:
  Packer()
      : start_ms_(NowInMillis()),
        allocator_(GlobalAllocator()),
        fbs_wrapper_(allocator_),
        fbs_(1024 * 1024 * 1024, &fbs_wrapper_),
        images_(allocator_),
        scripts_(allocator_),
        spritesheets_(allocator_),
        sounds_(allocator_),
        fonts_(allocator_),
        shaders_(allocator_),
        text_files_(allocator_) {}

  static void Init() {
    pugi::set_memory_management_functions(GlobalAlloc, GlobalDealloc);
  }

  void Finish(const char* output_file) {
    auto image_vec = fbs_.CreateVector(images_);
    auto scripts_vec = fbs_.CreateVector(scripts_);
    auto sprite_sheets_vec = fbs_.CreateVector(spritesheets_);
    auto sounds_vec = fbs_.CreateVector(sounds_);
    auto fonts_vec = fbs_.CreateVector(fonts_);
    auto shaders_vec = fbs_.CreateVector(shaders_);
    auto text_files_vec = fbs_.CreateVector(text_files_);
    AssetsPackBuilder assets(fbs_);
    assets.add_images(image_vec);
    assets.add_scripts(scripts_vec);
    assets.add_sprite_sheets(sprite_sheets_vec);
    assets.add_sounds(sounds_vec);
    assets.add_fonts(fonts_vec);
    assets.add_shaders(shaders_vec);
    assets.add_texts(text_files_vec);
    fbs_.Finish(assets.Finish());
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
        zip_file, fbs_.GetBufferPointer(), fbs_.GetSize(), /*freep=*/0);
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
      DIE("Could not close archive ", output_file, ": ",
          zip_strerror(zip_file));
    }
    std::printf("Elapsed %llums\n", NowInMillis() - start_ms_);
    std::printf("Used %llud out of %llu memory (%.2lf %%)\n",
                allocator_->used_memory(), allocator_->total_memory(),
                100.0 * allocator_->used_memory() / allocator_->total_memory());
  }

  void HandleQoiImage(std::string_view filename, uint8_t* buf, size_t size) {
    QoiDesc desc;
    QoiDecode(buf, size, &desc, /*components=*/4, GlobalAllocator());
    images_.push_back(CreateImageAsset(fbs_, fbs_.CreateString(filename),
                                       desc.width, desc.height, desc.channels,
                                       fbs_.CreateVector(buf, size)));
  }

  void HandleNonQoiImage(std::string_view filename, uint8_t* buf, size_t size) {
    int width, height, channels;
    const uint8_t* img =
        stbi_load_from_memory(buf, size, &width, &height, &channels, 4);
    CHECK(img != nullptr, "Could not decode image ", filename);
    QoiDesc desc;
    desc.width = width;
    desc.height = height;
    desc.channels = 4;
    desc.colorspace = QOI_SRGB;
    int out = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(QoiEncode(
        reinterpret_cast<const void*>(img), &desc, &out, GlobalAllocator()));
    CHECK(data != nullptr, "Could not encode image ", filename, " as QOI");
    CHECK(out > 0, "Could not encode image ", filename, " as qoi");
    FixedStringBuffer<256> image_filename(WithoutExt(Basename(filename)),
                                          ".qoi");
    images_.push_back(CreateImageAsset(
        fbs_, fbs_.CreateString(image_filename.str()), desc.width, desc.height,
        desc.channels, fbs_.CreateVector(data, out)));
  }

  void HandleScript(std::string_view filename, uint8_t* buf, size_t size) {
    scripts_.push_back(CreateScriptAsset(fbs_, fbs_.CreateString(filename),
                                         fbs_.CreateVector(buf, size)));
  }

  void HandleSpritesheet(std::string_view filename, uint8_t* buf, size_t size) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer_inplace(buf, size);
    CHECK(result, "Could not parse ", filename, ": ", result.status);
    pugi::xml_node root = doc.child("TextureAtlases");
    for (const auto& atlas : root) {
      WithAllocator<std::vector<flatbuffers::Offset<SpriteAsset>>,
                    PackerAllocator>
          sprites(allocator_);
      auto str = fbs_.CreateString(atlas.attribute("imagePath").value());
      for (const auto& sprite : atlas) {
        uint32_t x, y, w, h;
        sscanf(sprite.attribute("width").value(), "%u", &w);
        sscanf(sprite.attribute("height").value(), "%u", &h);
        sscanf(sprite.attribute("x").value(), "%u", &x);
        sscanf(sprite.attribute("y").value(), "%u", &y);
        sprites.push_back(CreateSpriteAsset(
            fbs_, fbs_.CreateString(sprite.attribute("name").value()), str, x,
            y, w, h));
      }
      spritesheets_.push_back(CreateSpritesheetAsset(
          fbs_, fbs_.CreateString(filename), str, fbs_.CreateVector(sprites)));
    }
  }

  void HandleOggSound(std::string_view filename, uint8_t* buf, size_t size) {
    sounds_.push_back(CreateSoundAsset(fbs_, fbs_.CreateString(filename),
                                       SoundType::OGG,
                                       fbs_.CreateVector(buf, size)));
  }

  void HandleWavSound(std::string_view filename, uint8_t* buf, size_t size) {
    sounds_.push_back(CreateSoundAsset(fbs_, fbs_.CreateString(filename),
                                       SoundType::WAV,
                                       fbs_.CreateVector(buf, size)));
  }

  void HandleFont(std::string_view filename, uint8_t* buf, size_t size) {
    fonts_.push_back(CreateFontAsset(fbs_, fbs_.CreateString(filename),
                                     fbs_.CreateVector(buf, size)));
  }

  void HandleVertexShader(std::string_view filename, uint8_t* buf,
                          size_t size) {
    shaders_.push_back(CreateShaderAsset(
        fbs_, fbs_.CreateString(filename), ShaderType::VERTEX,
        fbs_.CreateString(reinterpret_cast<const char*>(buf), size)));
  }

  void HandleFragmentShader(std::string_view filename, uint8_t* buf,
                            size_t size) {
    shaders_.push_back(CreateShaderAsset(
        fbs_, fbs_.CreateString(filename), ShaderType::FRAGMENT,
        fbs_.CreateString(reinterpret_cast<const char*>(buf), size)));
  }

  void HandleTextFile(std::string_view filename, uint8_t* buf, size_t size) {
    text_files_.push_back(CreateTextFileAsset(fbs_, fbs_.CreateString(filename),
                                              fbs_.CreateVector(buf, size)));
  }

 private:
  int64_t start_ms_;
  PackerAllocator* allocator_;

  FlatbufferAllocator<PackerAllocator> fbs_wrapper_;
  flatbuffers::FlatBufferBuilder fbs_;
  WithAllocator<std::vector<flatbuffers::Offset<ImageAsset>>, PackerAllocator>
      images_;
  WithAllocator<std::vector<flatbuffers::Offset<ScriptAsset>>, PackerAllocator>
      scripts_;
  WithAllocator<std::vector<flatbuffers::Offset<SpritesheetAsset>>,
                PackerAllocator>
      spritesheets_;
  WithAllocator<std::vector<flatbuffers::Offset<SoundAsset>>, PackerAllocator>
      sounds_;
  WithAllocator<std::vector<flatbuffers::Offset<FontAsset>>, PackerAllocator>
      fonts_;
  WithAllocator<std::vector<flatbuffers::Offset<ShaderAsset>>, PackerAllocator>
      shaders_;
  WithAllocator<std::vector<flatbuffers::Offset<TextFileAsset>>,
                PackerAllocator>
      text_files_;
};

struct FileHandler {
  std::string_view extension;
  void (Packer::*method)(std::string_view filename, uint8_t* buf, size_t size);
};
FileHandler kHandlers[] = {{".lua", &Packer::HandleScript},
                           {".qoi", &Packer::HandleQoiImage},
                           {".xml", &Packer::HandleSpritesheet},
                           {".ogg", &Packer::HandleOggSound},
                           {".ttf", &Packer::HandleFont},
                           {".wav", &Packer::HandleWavSound},
                           {".vert", &Packer::HandleVertexShader},
                           {".frag", &Packer::HandleFragmentShader},
                           {".png", &Packer::HandleNonQoiImage},
                           {".jpg", &Packer::HandleNonQoiImage},
                           {".bmp", &Packer::HandleNonQoiImage},
                           {".txt", &Packer::HandleTextFile}};

}  // namespace

void PackerMain(const char* output_file) {
  Packer::Init();
  Packer packer;
  char** rc = PHYSFS_enumerateFiles("/");
  CHECK(rc != nullptr, "Failed to enumerate files: ",
        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
  for (char** p = rc; *p; p++) {
    const char* path = *p;
    LOG("Found file: ", path);
    std::string_view fname = Basename(path);
    bool handled = false;
    for (auto& handler : kHandlers) {
      if (HasSuffix(path, handler.extension)) {
        auto [buffer, fsize] = ReadWholeFile(path);
        auto method = handler.method;
        (packer.*method)(fname, buffer, fsize);
        handled = true;
        break;
      }
    }
    if (!handled) LOG("No handler for file ", path);
  }
  PHYSFS_freeList(rc);
  LOG("Finished with files, packing to ", output_file);
  packer.Finish(output_file);
}

}  // namespace G

int main(int argc, const char* argv[]) {
  PHYSFS_CHECK(PHYSFS_init(argv[0]));
  CHECK(argc > 2, "Usage: <output file> <directory with assets>");
  PHYSFS_CHECK(PHYSFS_mount(argv[2], "/", 1));
  G::PackerMain(argv[1]);
  PHYSFS_deinit();
}
