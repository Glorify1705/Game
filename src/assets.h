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
  struct Image {
    std::string_view name;
    std::size_t width;
    std::size_t height;
    std::size_t size;
    uint8_t* contents;
  };

  struct Spritesheet {
    std::string_view name;
    std::string_view image;
    std::size_t width;
    std::size_t height;
  };

  struct Sprite {
    std::string_view name;
    std::string_view spritesheet;
    std::size_t x;
    std::size_t y;
    std::size_t width;
    std::size_t height;
  };

  struct Script {
    std::string_view name;
    std::size_t size;
    uint8_t* contents;
  };

  struct Sound {
    std::string_view name;
    std::size_t size;
    uint8_t* contents;
  };

  struct TextFile {
    std::string_view name;
    std::size_t size;
    uint8_t* contents;
  };

  struct Font {
    std::string_view name;
    std::size_t size;
    uint8_t* contents;
  };

  enum class ShaderType { kVertex, kFragment };

  struct Shader {
    std::string_view name;
    ShaderType type;
    std::size_t size;
    uint8_t* contents;
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
        images_map_(allocator),
        images_(allocator),
        sprites_map_(allocator),
        sprites_(allocator),
        spritesheets_map_(allocator),
        spritesheets_(allocator),
        scripts_map_(allocator),
        scripts_(allocator),
        sounds_map_(allocator),
        sounds_(allocator),
        fonts_map_(allocator),
        fonts_(allocator),
        shaders_map_(allocator),
        shaders_(allocator),
        text_files_map_(allocator),
        text_files_(allocator) {}

  void Load();

  Image* GetImage(std::string_view name) const {
    Image* image;
    if (!images_map_.Lookup(name, &image)) return nullptr;
    return image;
  }

  Script* GetScript(std::string_view name) const {
    Script* script;
    if (!scripts_map_.Lookup(name, &script)) return nullptr;
    return script;
  }

  ArrayView<Script> GetScripts() const { return MakeArrayView(&scripts_); }

  Spritesheet* GetSpritesheet(std::string_view name) const {
    Spritesheet* spritesheet;
    if (!spritesheets_map_.Lookup(name, &spritesheet)) return nullptr;
    return spritesheet;
  }

  Sprite* GetSprite(std::string_view name) const {
    Sprite* sprite;
    if (!sprites_map_.Lookup(name, &sprite)) return nullptr;
    return sprite;
  }

  ArrayView<Sprite> GetSprites() const { return MakeArrayView(&sprites_); }

  Font* GetFont(std::string_view name) const {
    Font* font;
    if (!fonts_map_.Lookup(name, &font)) return nullptr;
    return font;
  }

  TextFile* GetText(std::string_view name) const {
    TextFile* text_file;
    if (!text_files_map_.Lookup(name, &text_file)) return nullptr;
    return text_file;
  }

  Sound* GetSound(std::string_view name) const {
    Sound* sound;
    if (!sounds_map_.Lookup(name, &sound)) return nullptr;
    return sound;
  }

  Shader* GetShader(std::string_view name) const {
    Shader* shader;
    if (!shaders_map_.Lookup(name, &shader)) return nullptr;
    return shader;
  }

  ArrayView<Shader> GetShaders() const { return MakeArrayView(&shaders_); }

  void Trace(unsigned int sql_type, void* p, void* x);

 private:
  template <typename T>
  static ArrayView<T> MakeArrayView(const DynArray<T>* a) {
    return ArrayView<T>(a);
  }

  std::string_view PushName(std::string_view s) {
    std::string_view result(&name_buffer_[name_size_], s.size());
    std::memcpy(&name_buffer_[name_size_], s.data(), s.size());
    name_size_ += s.size();
    name_buffer_[name_size_++] = '\0';
    return result;
  }

  void LoadScript(std::string_view name, uint8_t* buffer, std::size_t size);
  void LoadImage(std::string_view name, uint8_t* buffer, std::size_t size);
  void LoadAudio(std::string_view name, uint8_t* buffer, std::size_t size);
  void LoadText(std::string_view name, uint8_t* buffer, std::size_t size);
  void LoadShader(std::string_view name, uint8_t* buffer, std::size_t size);
  void LoadFont(std::string_view name, uint8_t* buffer, std::size_t size);
  void LoadSpritesheet(std::string_view name, uint8_t* buffer,
                       std::size_t size);
  void ReserveBufferForType(std::string_view type, std::size_t count);

  sqlite3* db_;
  Allocator* allocator_;

  std::size_t name_size_;
  char* name_buffer_;

  std::size_t content_size_;
  uint8_t* content_buffer_;

  Dictionary<Image*> images_map_;
  DynArray<Image> images_;
  Dictionary<Sprite*> sprites_map_;
  DynArray<Sprite> sprites_;
  Dictionary<Spritesheet*> spritesheets_map_;
  DynArray<Spritesheet> spritesheets_;
  Dictionary<Script*> scripts_map_;
  DynArray<Script> scripts_;
  Dictionary<Sound*> sounds_map_;
  DynArray<Sound> sounds_;
  Dictionary<Font*> fonts_map_;
  DynArray<Font> fonts_;
  Dictionary<Shader*> shaders_map_;
  DynArray<Shader> shaders_;
  Dictionary<TextFile*> text_files_map_;
  DynArray<TextFile> text_files_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H
