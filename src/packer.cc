#include "packer.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <type_traits>
#include <vector>

#include "allocators.h"
#include "assets_generated.h"
#include "clock.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "logging.h"
#include "pugixml.h"
#include "qoi.h"

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
        assets_(fbs_),
        images_(allocator_),
        scripts_(allocator_),
        spritesheets_(allocator_),
        sounds_(allocator_) {}
  static void Init() {
    SetQoiAlloc(GlobalAllocate, GlobalDeallocate);
    pugi::set_memory_management_functions(GlobalAllocate, GlobalDeallocate);
  }

  void Finish(const char* output_file) {
    assets_.add_images(fbs_.CreateVector(images_));
    assets_.add_scripts(fbs_.CreateVector(scripts_));
    assets_.add_sprite_sheets(fbs_.CreateVector(spritesheets_));
    assets_.add_sounds(fbs_.CreateVector(sounds_));
    const auto assets = assets_.Finish();
    fbs_.Finish(assets);
    FILE* f = std::fopen(output_file, "wb");
    std::fwrite(fbs_.GetBufferPointer(), fbs_.GetSize(), 1, f);
    std::fclose(f);
    std::printf("Elapsed %.2lfms\n", NowInMillis() - start_ms_);
    std::printf("Used %ld out of %ld memory (%.2lf %%)\n", allocator_->used(),
                allocator_->total(),
                100.0 * allocator_->used() / allocator_->total());
  }

  void HandleImage(const char* filename, uint8_t* buf, size_t size) {
    qoi_desc desc;
    const auto* data = reinterpret_cast<const uint8_t*>(
        qoi_decode(buf, size, &desc, /*components=*/4));
    images_.push_back(assets::CreateImage(
        fbs_, fbs_.CreateString(filename), desc.width, desc.height,
        desc.channels,
        fbs_.CreateVector(data,
                          1ULL * desc.width * desc.height * desc.channels)));
  }
  void HandleScript(const char* filename, uint8_t* buf, size_t size) {
    scripts_.push_back(assets::CreateScript(fbs_, fbs_.CreateString(filename),
                                            fbs_.CreateVector(buf, size)));
  }

  void HandleSpritesheet(const char* filename, uint8_t* buf, size_t size) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer_inplace(buf, size);
    CHECK(result, "Could not parse ", filename, ": ", result.status);
    pugi::xml_node root = doc.child("TextureAtlas");
    WithAllocator<std::vector<flatbuffers::Offset<assets::Subtexture>>,
                  BumpAllocator>
        sub_textures(allocator_);
    auto str = fbs_.CreateString(root.attribute("imagePath").value());
    for (const auto& sub_texture : root) {
      uint32_t x, y, w, h;
      sscanf(sub_texture.attribute("width").value(), "%u", &w);
      sscanf(sub_texture.attribute("height").value(), "%u", &h);
      sscanf(sub_texture.attribute("x").value(), "%u", &x);
      sscanf(sub_texture.attribute("y").value(), "%u", &y);
      sub_textures.push_back(assets::CreateSubtexture(
          fbs_, fbs_.CreateString(sub_texture.attribute("name").value()), str,
          x, y, w, h));
    }
    spritesheets_.push_back(
        assets::CreateSpritesheet(fbs_, fbs_.CreateString(filename), str,
                                  fbs_.CreateVector(sub_textures)));
  }

  void HandleOggSound(const char* filename, uint8_t* buf, size_t size) {
    sounds_.push_back(assets::CreateSound(fbs_, fbs_.CreateString(filename),
                                          assets::SoundType::OGG,
                                          fbs_.CreateVector(buf, size)));
  }

 private:
  const double start_ms_;
  BumpAllocator* allocator_;

  FlatbufferAllocator<BumpAllocator> fbs_wrapper_;
  flatbuffers::FlatBufferBuilder fbs_;
  assets::AssetsBuilder assets_;
  WithAllocator<std::vector<flatbuffers::Offset<assets::Image>>, BumpAllocator>
      images_;
  WithAllocator<std::vector<flatbuffers::Offset<assets::Script>>, BumpAllocator>
      scripts_;
  WithAllocator<std::vector<flatbuffers::Offset<assets::Spritesheet>>,
                BumpAllocator>
      spritesheets_;
  WithAllocator<std::vector<flatbuffers::Offset<assets::Sound>>, BumpAllocator>
      sounds_;
};

struct FileHandler {
  const char* extension;
  void (Packer::*method)(const char* filename, uint8_t* buf, size_t size);
};
FileHandler kHandlers[] = {
    {".lua", &Packer::HandleScript},
    {".qoi", &Packer::HandleImage},
    {".xml", &Packer::HandleSpritesheet},
    {".ogg", &Packer::HandleOggSound},
};

}  // namespace

void PackerMain(const char* output_file, const std::vector<const char*> paths) {
  Packer::Init();
  Packer packer;
  for (const char* path : paths) {
    const char* fname = Basename(path);
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
    if (!handled) DIE("No handler for file ", fname);
  }
  packer.Finish(output_file);
}
