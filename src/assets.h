#pragma once
#ifndef _GAME_ASSETS_H
#define _GAME_ASSETS_H

#include <stddef.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "array.h"
#include "dictionary.h"
#include "logging.h"
#include "sqlite3.h"

namespace G {

class DbAssets {
 public:
  using ChecksumType = XXH64_hash_t;

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
    explicit ArrayView(const DynArray<T>* array) : array_(array){};

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
        checksums_(1 << 20, allocator) {}

  void Load();

  void RegisterShaderLoad(void (*load)(DbAssets::Shader* shader,
                                       StringBuffer* err, void* ud),
                          void* ud) {
    shader_loader_.fn = load;
    shader_loader_.ud = ud;
  }

  void RegisterScriptLoad(void (*load)(DbAssets::Script* script,
                                       StringBuffer* err, void* ud),
                          void* ud) {
    script_loader_.fn = load;
    script_loader_.ud = ud;
  }

  void RegisterImageLoad(void (*load)(DbAssets::Image* image, StringBuffer* err,
                                      void* ud),
                         void* ud) {
    image_loader_.fn = load;
    image_loader_.ud = ud;
  }

  void RegisterSpritesheetLoad(void (*load)(DbAssets::Spritesheet* script,
                                            StringBuffer* err, void* ud),
                               void* ud) {
    spritesheet_loader_.fn = load;
    spritesheet_loader_.ud = ud;
  }

  void RegisterSpriteLoad(void (*load)(DbAssets::Sprite* sprite,
                                       StringBuffer* err, void* ud),
                          void* ud) {
    sprite_loader_.fn = load;
    sprite_loader_.ud = ud;
  }

  void RegisterSoundLoad(void (*load)(DbAssets::Sound* sound, StringBuffer* err,
                                      void* ud),
                         void* ud) {
    sound_loader_.fn = load;
    sound_loader_.ud = ud;
  }

  void RegisterFontLoad(void (*load)(DbAssets::Font* sound, StringBuffer* err,
                                     void* ud),
                        void* ud) {
    font_loader_.fn = load;
    font_loader_.ud = ud;
  }

  ChecksumType GetChecksum(std::string_view asset);

  void Trace(unsigned int sql_type, void* p, void* x);

 private:
  template <typename T>
  struct LoadFn {
    char err[kMaxLogLineLength];
    void (*fn)(T*, StringBuffer*, void*) = nullptr;
    void* ud;

    void Load(T* ptr) {
      if (!fn) {
        LOG("Skipping loading asset ", ptr->name);
        return;
      }
      StringBuffer buf(err, kMaxLogLineLength);
      fn(ptr, &buf, ud);
    }
  };

  void LoadScript(std::string_view name, uint8_t* buffer, size_t size,
                  ChecksumType checksum);
  void LoadImage(std::string_view name, uint8_t* buffer, size_t size,
                 ChecksumType checksum);
  void LoadAudio(std::string_view name, uint8_t* buffer, size_t size,
                 ChecksumType checksum);
  void LoadText(std::string_view name, uint8_t* buffer, size_t size,
                ChecksumType checksum);
  void LoadShader(std::string_view name, uint8_t* buffer, size_t size,
                  ChecksumType checksum);
  void LoadFont(std::string_view name, uint8_t* buffer, size_t size,
                ChecksumType checksum);
  void LoadSpritesheet(std::string_view name, uint8_t* buffer, size_t size,
                       ChecksumType checksum);

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
  LoadFn<DbAssets::TextFile> text_file_loader_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H
