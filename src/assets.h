#pragma once
#ifndef _GAME_ASSETS_H
#define _GAME_ASSETS_H

#include <stddef.h>

#include <cstdint>
#include <string_view>

#include "array.h"
#include "dictionary.h"
#include "error.h"
#include "libraries/sqlite3.h"
#include "logging.h"

namespace G {

class DbAssets {
 public:
  using ChecksumType = uint64_t;

  struct Image {
    std::string_view name;
    size_t width;
    size_t height;
    size_t size;
    ChecksumType checksum;
    uint8_t* contents;
  };

  struct Spritesheet {
    std::string_view name;
    std::string_view image;
    size_t width;
    size_t height;
    ChecksumType checksum;
  };

  struct Sprite {
    std::string_view name;
    std::string_view spritesheet;
    size_t x;
    size_t y;
    size_t width;
    size_t height;
  };

  struct Script {
    std::string_view name;
    size_t size;
    uint8_t* contents;
    ChecksumType checksum;
  };

  struct Sound {
    std::string_view name;
    size_t size;
    uint32_t channels;
    uint32_t samplerate;
    uint32_t samples;
    uint8_t* contents;
    ChecksumType checksum;
  };

  struct TextFile {
    std::string_view name;
    size_t size;
    uint8_t* contents;
    ChecksumType checksum;
  };

  struct Font {
    std::string_view name;
    size_t size;
    uint8_t* contents;
    ChecksumType checksum;
  };

  // Binary protobuf FileDescriptorSet compiled from a .proto file.
  struct ProtoDescriptor {
    std::string_view name;
    size_t size;
    uint8_t* contents;
    ChecksumType checksum;
  };

  enum class ShaderType { kVertex, kFragment };

  struct Shader {
    std::string_view name;
    ShaderType type;
    size_t size;
    uint8_t* contents;
    ChecksumType checksum;
  };

  template <typename T>
  class ArrayView {
   public:
    explicit ArrayView(const DynArray<T>* array) : array_(array) {};

    using const_iterator = const T*;

    const_iterator begin() const { return array_->cbegin(); }
    const_iterator end() const { return array_->cend(); }

   private:
    const DynArray<T>* const array_;
  };

  DbAssets(sqlite3* db, Allocator* allocator)
      : db_(db),
        allocator_(allocator),
        checksums_map_(allocator),
        checksums_(1 << 20, allocator),
        text_files_(256, allocator),
        text_files_table_(allocator) {}

  // Callback type for asset loaders. Returns an error on failure.
  template <typename T>
  using LoadCallback = ErrorOr<void> (*)(T*, void*);

  void Load();

  void RegisterShaderLoad(LoadCallback<Shader> load, void* ud) {
    shader_loader_.fn = load;
    shader_loader_.ud = ud;
  }

  void RegisterScriptLoad(LoadCallback<Script> load, void* ud) {
    script_loader_.fn = load;
    script_loader_.ud = ud;
  }

  void RegisterImageLoad(LoadCallback<Image> load, void* ud) {
    image_loader_.fn = load;
    image_loader_.ud = ud;
  }

  void RegisterSpritesheetLoad(LoadCallback<Spritesheet> load, void* ud) {
    spritesheet_loader_.fn = load;
    spritesheet_loader_.ud = ud;
  }

  void RegisterSpriteLoad(LoadCallback<Sprite> load, void* ud) {
    sprite_loader_.fn = load;
    sprite_loader_.ud = ud;
  }

  void RegisterSoundLoad(LoadCallback<Sound> load, void* ud) {
    sound_loader_.fn = load;
    sound_loader_.ud = ud;
  }

  void RegisterFontLoad(LoadCallback<Font> load, void* ud) {
    font_loader_.fn = load;
    font_loader_.ud = ud;
  }

  void RegisterProtoLoad(LoadCallback<ProtoDescriptor> load, void* ud) {
    proto_loader_.fn = load;
    proto_loader_.ud = ud;
  }

  // Returns the text file with the given name, or nullptr if not found.
  TextFile* LookupTextFile(std::string_view name);

  ChecksumType GetChecksum(std::string_view asset);

  void Trace(unsigned int sql_type, void* p, void* x);

 private:
  template <typename T>
  struct LoadFn {
    LoadCallback<T> fn = nullptr;
    void* ud;

    void Load(T* ptr) {
      if (!fn) {
        LOG("Skipping loading asset ", ptr->name);
        return;
      }
      auto result = fn(ptr, ud);
      if (result.is_error()) {
        ELOG("Failed to load asset ", ptr->name, ": ",
             result.error().message());
      }
    }
  };

  void LoadScript(std::string_view name, uint8_t* buffer, size_t size,
                  ChecksumType checksum, uint64_t blob_hash);
  void LoadImage(std::string_view name, uint8_t* buffer, size_t size,
                 ChecksumType checksum, uint64_t blob_hash);
  void LoadAudio(std::string_view name, uint8_t* buffer, size_t size,
                 ChecksumType checksum, uint64_t blob_hash);
  void LoadText(std::string_view name, uint8_t* buffer, size_t size,
                ChecksumType checksum, uint64_t blob_hash);
  void LoadShader(std::string_view name, uint8_t* buffer, size_t size,
                  ChecksumType checksum, uint64_t blob_hash);
  void LoadFont(std::string_view name, uint8_t* buffer, size_t size,
                ChecksumType checksum, uint64_t blob_hash);
  void LoadSpritesheet(std::string_view name, uint8_t* buffer, size_t size,
                       ChecksumType checksum, uint64_t blob_hash);
  void LoadProtoDescriptor(std::string_view name, uint8_t* buffer, size_t size,
                           ChecksumType checksum, uint64_t blob_hash);

  sqlite3* db_;
  Allocator* allocator_;

  struct Checksum {
    std::string_view asset;
    ChecksumType checksum;
  };

  Dictionary<Checksum*> checksums_map_;
  FixedArray<Checksum> checksums_;

  LoadFn<DbAssets::Shader> shader_loader_;
  LoadFn<DbAssets::Script> script_loader_;
  LoadFn<DbAssets::Image> image_loader_;
  LoadFn<DbAssets::Spritesheet> spritesheet_loader_;
  LoadFn<DbAssets::Sprite> sprite_loader_;
  LoadFn<DbAssets::Font> font_loader_;
  LoadFn<DbAssets::Sound> sound_loader_;
  LoadFn<DbAssets::ProtoDescriptor> proto_loader_;

  FixedArray<TextFile> text_files_;
  Dictionary<TextFile*> text_files_table_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H
