#pragma once
#ifndef _GAME_ASSETS_H
#define _GAME_ASSETS_H

#include <stddef.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "assets_generated.h"
#include "dictionary.h"
#include "logging.h"
#include "sqlite3.h"

namespace G {

inline std::string_view FlatbufferStringview(const flatbuffers::String* s) {
  if (s == nullptr) return std::string_view("", 0);
  return std::string_view(s->data(), s->size());
}

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
    std::size_t width;
    std::size_t height;
  };

  struct Sprite {
    std::string_view name;
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

  struct Shader {
    std::string_view name;
    std::size_t size;
    uint8_t* contents;
  };

  DbAssets(std::string_view dbname, Allocator* allocator)
      : allocator_(allocator),
        images_(allocator),
        sprites_(allocator),
        spritesheets_(allocator),
        scripts_(allocator),
        sounds_(allocator),
        fonts_(allocator),
        shaders_(allocator),
        text_files_(allocator) {
    db_filename_.Append(dbname);
  }

  void Load();

  Image GetImage(std::string_view name) const {
    return images_.LookupOrDie(name);
  }
  Script GetScript(std::string_view name) const {
    return scripts_.LookupOrDie(name);
  }
  Spritesheet GetSpritesheet(std::string_view name) const {
    return spritesheets_.LookupOrDie(name);
  }
  Sprite GetSprite(std::string_view name) const {
    return sprites_.LookupOrDie(name);
  }
  Font GetFont(std::string_view name) const { return fonts_.LookupOrDie(name); }
  TextFile GetText(std::string_view name) const {
    return text_files_.LookupOrDie(name);
  }
  Sound GetSound(std::string_view name) const {
    return sounds_.LookupOrDie(name);
  }
  Shader GetShader(std::string_view name) const {
    return shaders_.LookupOrDie(name);
  }

 private:
  void LoadScript(std::string_view name, uint8_t* buffer, std::size_t size);

  FixedStringBuffer<256> db_filename_;

  sqlite3* db_;
  Allocator* allocator_;

  std::size_t name_size_;
  char* name_buffer_;

  std::size_t content_size_;
  uint8_t* content_buffer_;

  Dictionary<Image> images_;
  Dictionary<Sprite> sprites_;
  Dictionary<Spritesheet> spritesheets_;
  Dictionary<Script> scripts_;
  Dictionary<Sound> sounds_;
  Dictionary<Font> fonts_;
  Dictionary<Shader> shaders_;
  Dictionary<TextFile> text_files_;
};

class Assets {
 public:
  Assets(const AssetsPack* assets, std::size_t size)
      : assets_(assets), size_(size) {
    CHECK(assets_ != nullptr, "Failed to build assets from buffer");
  }

  const ImageAsset* GetImage(std::string_view name) const;
  const ScriptAsset* GetScript(std::string_view name) const;
  std::size_t scripts() const { return assets_->scripts()->size(); }
  const ScriptAsset* GetScriptByIndex(std::size_t idx) const {
    return assets_->scripts()->Get(idx);
  }
  const SpritesheetAsset* GetSpritesheet(std::string_view name) const;
  std::size_t spritesheets() const { return assets_->spritesheets()->size(); }
  const SpritesheetAsset* GetSpritesheetByIndex(std::size_t idx) const {
    return assets_->spritesheets()->Get(idx);
  }
  const SpriteAsset* GetSprite(std::string_view name) const;
  std::size_t sprites() const { return assets_->sprites()->size(); }
  const SpriteAsset* GetSpriteByIndex(std::size_t idx) const {
    return assets_->sprites()->Get(idx);
  }
  const FontAsset* GetFont(std::string_view name) const;
  std::size_t fonts() const { return assets_->fonts()->size(); }
  const FontAsset* GetFontByIndex(std::size_t idx) const {
    return assets_->fonts()->Get(idx);
  }
  const TextFileAsset* GetText(std::string_view name) const;
  std::size_t shaders() const { return assets_->shaders()->size(); }
  const ShaderAsset* GetShaderByIndex(std::size_t idx) const {
    return assets_->shaders()->Get(idx);
  }
  const SoundAsset* GetSound(std::string_view name) const;
  const ShaderAsset* GetShader(std::string_view) const;

  const AssetsPack* PackedAssets() const { return assets_; }
  std::size_t PackerAssetSize() const { return size_; }

 private:
  const AssetsPack* assets_;
  const std::size_t size_;
};

}  // namespace G

#endif  // _GAME_ASSETS_H
