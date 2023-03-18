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
#include "logging.h"
#include "pugixml.h"
#include "qoi.h"
#include "zip.h"

namespace G {
namespace {

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

static BumpAllocator* GlobalBumpAllocator() {
  static BumpAllocator allocator(4 * 1024 * 1024 * 1024ULL);  // 4 Gigabytes.
  return &allocator;
}

void* GlobalAllocate(size_t s) {
  return GlobalBumpAllocator()->Alloc(s, /*align=*/1);
}

void GlobalDeallocate(void*) {}

const char* Basename(const char* p) {
  size_t pos = std::strlen(p) - 1;
  const char* c;
  for (c = p + pos; c != p && *c != '/';) {
    c--;
  }
  return *c == '/' ? ++c : c;
}

std::pair<uint8_t*, size_t> ReadWholeFile(const char* path) {
  FILE* f = std::fopen(path, "rb");
  CHECK(f != nullptr, "Could not read ", path);
  std::fseek(f, 0, SEEK_END);
  const size_t fsize = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  auto* buffer = GlobalBumpAllocator()->AllocArray<uint8_t>(fsize);
  CHECK(std::fread(buffer, fsize, 1, f) == 1, " failed to read ", path);
  fclose(f);
  return {buffer, fsize};
}

class Packer {
 public:
  Packer()
      : start_ms_(NowInMillis()),
        allocator_(GlobalBumpAllocator()),
        fbs_wrapper_(allocator_),
        fbs_(1024 * 1024 * 1024, &fbs_wrapper_),
        images_(allocator_),
        scripts_(allocator_),
        spritesheets_(allocator_),
        sounds_(allocator_),
        fonts_(allocator_),
        shaders_(allocator_) {}

  static void Init() {
    SetQoiAlloc(GlobalAllocate, GlobalDeallocate);
    pugi::set_memory_management_functions(GlobalAllocate, GlobalDeallocate);
  }

  void Finish(const char* output_file) {
    auto image_vec = fbs_.CreateVector(images_);
    auto scripts_vec = fbs_.CreateVector(scripts_);
    auto sprite_sheets_vec = fbs_.CreateVector(spritesheets_);
    auto sounds_vec = fbs_.CreateVector(sounds_);
    auto fonts_vec = fbs_.CreateVector(fonts_);
    auto shaders_vec = fbs_.CreateVector(shaders_);
    AssetsPackBuilder assets(fbs_);
    assets.add_images(image_vec);
    assets.add_scripts(scripts_vec);
    assets.add_sprite_sheets(sprite_sheets_vec);
    assets.add_sounds(sounds_vec);
    assets.add_fonts(fonts_vec);
    assets.add_shaders(shaders_vec);
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
    std::printf("Elapsed %lldms\n", NowInMillis() - start_ms_);
    std::printf("Used %llud out of %llu memory (%.2lf %%)\n",
                allocator_->used(), allocator_->total(),
                100.0 * allocator_->used() / allocator_->total());
  }

  void HandleImage(const char* filename, uint8_t* buf, size_t size) {
    qoi_desc desc;
    const auto* data = reinterpret_cast<const uint8_t*>(
        qoi_decode(buf, size, &desc, /*components=*/4));
    images_.push_back(CreateImageFile(
        fbs_, fbs_.CreateString(filename), desc.width, desc.height,
        desc.channels,
        fbs_.CreateVector(data,
                          1ULL * desc.width * desc.height * desc.channels)));
  }
  void HandleScript(const char* filename, uint8_t* buf, size_t size) {
    scripts_.push_back(CreateScriptFile(fbs_, fbs_.CreateString(filename),
                                        fbs_.CreateVector(buf, size)));
  }

  void HandleSpritesheet(const char* filename, uint8_t* buf, size_t size) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer_inplace(buf, size);
    CHECK(result, "Could not parse ", filename, ": ", result.status);
    pugi::xml_node root = doc.child("TextureAtlas");
    WithAllocator<std::vector<flatbuffers::Offset<Subtexture>>, BumpAllocator>
        sub_textures(allocator_);
    auto str = fbs_.CreateString(root.attribute("imagePath").value());
    for (const auto& sub_texture : root) {
      uint32_t x, y, w, h;
      sscanf(sub_texture.attribute("width").value(), "%u", &w);
      sscanf(sub_texture.attribute("height").value(), "%u", &h);
      sscanf(sub_texture.attribute("x").value(), "%u", &x);
      sscanf(sub_texture.attribute("y").value(), "%u", &y);
      sub_textures.push_back(CreateSubtexture(
          fbs_, fbs_.CreateString(sub_texture.attribute("name").value()), str,
          x, y, w, h));
    }
    spritesheets_.push_back(
        CreateSpritesheetFile(fbs_, fbs_.CreateString(filename), str,
                              fbs_.CreateVector(sub_textures)));
  }

  void HandleOggSound(const char* filename, uint8_t* buf, size_t size) {
    sounds_.push_back(CreateSoundFile(fbs_, fbs_.CreateString(filename),
                                      SoundType::OGG,
                                      fbs_.CreateVector(buf, size)));
  }

  void HandleWavSound(const char* filename, uint8_t* buf, size_t size) {
    sounds_.push_back(CreateSoundFile(fbs_, fbs_.CreateString(filename),
                                      SoundType::WAV,
                                      fbs_.CreateVector(buf, size)));
  }

  void HandleFont(const char* filename, uint8_t* buf, size_t size) {
    fonts_.push_back(CreateFontFile(fbs_, fbs_.CreateString(filename),
                                    fbs_.CreateVector(buf, size)));
  }

  void HandleShader(const char* filename, uint8_t* buf, size_t size) {
    shaders_.push_back(CreateShaderFile(
        fbs_, fbs_.CreateString(filename),
        fbs_.CreateString(reinterpret_cast<const char*>(buf), size)));
  }

 private:
  int64_t start_ms_;
  BumpAllocator* allocator_;

  FlatbufferAllocator<BumpAllocator> fbs_wrapper_;
  flatbuffers::FlatBufferBuilder fbs_;
  WithAllocator<std::vector<flatbuffers::Offset<ImageFile>>, BumpAllocator>
      images_;
  WithAllocator<std::vector<flatbuffers::Offset<ScriptFile>>, BumpAllocator>
      scripts_;
  WithAllocator<std::vector<flatbuffers::Offset<SpritesheetFile>>,
                BumpAllocator>
      spritesheets_;
  WithAllocator<std::vector<flatbuffers::Offset<SoundFile>>, BumpAllocator>
      sounds_;
  WithAllocator<std::vector<flatbuffers::Offset<FontFile>>, BumpAllocator>
      fonts_;
  WithAllocator<std::vector<flatbuffers::Offset<ShaderFile>>, BumpAllocator>
      shaders_;
};

struct FileHandler {
  const char* extension;
  void (Packer::*method)(const char* filename, uint8_t* buf, size_t size);
};
FileHandler kHandlers[] = {
    {".lua", &Packer::HandleScript},      {".qoi", &Packer::HandleImage},
    {".xml", &Packer::HandleSpritesheet}, {".ogg", &Packer::HandleOggSound},
    {".ttf", &Packer::HandleFont},        {".wav", &Packer::HandleWavSound},
    {".frag", &Packer::HandleShader}};

}  // namespace

void PackerMain(const char* output_file, const std::vector<const char*> paths) {
  Packer::Init();
  Packer packer;
  for (const char* path : paths) {
    const char* fname = Basename(path);
    bool handled = false;
    for (auto& handler : kHandlers) {
      if (HasSuffix(path, handler.extension)) {
        LOG("Handling ", path, " with basename ", fname);
        auto [buffer, fsize] = ReadWholeFile(path);
        auto method = handler.method;
        (packer.*method)(fname, buffer, fsize);
        handled = true;
        break;
      }
    }
    if (!handled) DIE("No handler for file ", path);
  }
  LOG("Finished with files, packing to ", output_file);
  packer.Finish(output_file);
}

}  // namespace G

int main(int argc, const char* argv[]) {
  CHECK(argc > 2, "Usage: <output file> <files to pack>");
  G::PackerMain(argv[1], std::vector(argv + 2, argv + argc));
}