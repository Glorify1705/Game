#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "cli.h"
#include "defer.h"
#include "image.h"
#include "libraries/stb_image.h"
#include "libraries/stb_rect_pack.h"
#include "logging.h"
#include "platform.h"
#include "qoa.h"
#include "stringlib.h"
#include "units.h"

namespace G {
namespace {

// Read an entire file into an arena-allocated buffer.
uint8_t* ReadEntireFile(const char* path, size_t* out_size,
                        Allocator* allocator) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return nullptr;
  DEFER([f] { fclose(f); });
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  if (len <= 0) return nullptr;
  fseek(f, 0, SEEK_SET);
  auto* buf = static_cast<uint8_t*>(allocator->Alloc(len, 1));
  if (fread(buf, 1, len, f) != static_cast<size_t>(len)) return nullptr;
  *out_size = static_cast<size_t>(len);
  return buf;
}

bool WriteEntireFile(const char* path, const void* data, size_t size) {
  FILE* f = fopen(path, "wb");
  if (f == nullptr) {
    fprintf(stderr, "Error: could not open '%s' for writing: %s\n", path,
            strerror(errno));
    return false;
  }
  DEFER([f] { fclose(f); });
  if (fwrite(data, 1, size, f) != size) {
    fprintf(stderr, "Error: could not write '%s'\n", path);
    return false;
  }
  return true;
}

// Returns true if the extension is a supported image format.
bool IsSupportedImage(std::string_view ext) {
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
         ext == "gif" || ext == "tga" || ext == "qoi";
}

struct SpriteInput {
  // Sprite name derived from filename (null-terminated).
  const char* name;
  // Decoded pixel data (RGBA).
  uint8_t* pixels;
  int width;
  int height;
};

// Duplicate a string into the arena with a null terminator.
const char* StrDupZ(Allocator* allocator, std::string_view s) {
  char* buf = static_cast<char*>(allocator->Alloc(s.size() + 1, 1));
  memcpy(buf, s.data(), s.size());
  buf[s.size()] = '\0';
  return buf;
}

// Collect image files from a directory. Optionally recurse into subdirectories.
// Sprite names use the relative path from the base dir (without extension).
void CollectImages(const char* dir, const char* prefix, bool recursive,
                   DynArray<SpriteInput>* sprites, Allocator* allocator) {
#ifdef _WIN32
  FixedStringBuffer<1024> pattern(dir, "\\*");
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern.str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  DEFER([h] { FindClose(h); });
  do {
    if (fd.cFileName[0] == '.') continue;
    FixedStringBuffer<1024> full_path(dir, "\\", fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (recursive) {
        FixedStringBuffer<256> sub_prefix(allocator);
        if (prefix[0]) sub_prefix.Append(prefix, "/");
        sub_prefix.Append(fd.cFileName);
        CollectImages(full_path.str(), sub_prefix.str(), recursive, sprites,
                      allocator);
      }
      continue;
    }
    std::string_view ext = Extension(fd.cFileName);
    if (!IsSupportedImage(ext)) continue;
    std::string_view name_part = WithoutExt(fd.cFileName);
    FixedStringBuffer<256> sprite_name(allocator);
    if (prefix[0]) sprite_name.Append(prefix, "/");
    sprite_name.Append(name_part);
    // Read and decode the image.
    size_t file_size = 0;
    uint8_t* file_data = ReadEntireFile(full_path.str(), &file_size, allocator);
    if (file_data == nullptr) continue;
    int w, h, ch;
    uint8_t* pixels;
    if (ext == "qoi") {
      QoiDesc desc;
      pixels = static_cast<uint8_t*>(QoiDecode(
          file_data, static_cast<int>(file_size), &desc, 4, allocator));
      w = static_cast<int>(desc.width);
      h = static_cast<int>(desc.height);
    } else {
      pixels = stbi_load_from_memory(file_data, static_cast<int>(file_size), &w,
                                     &h, &ch, /*desired_channels=*/4);
    }
    if (pixels == nullptr) {
      fprintf(stderr, "Warning: could not decode '%s', skipping.\n",
              full_path.str());
      continue;
    }
    SpriteInput si;
    si.name = StrDupZ(allocator, sprite_name.piece());
    si.pixels = pixels;
    si.width = w;
    si.height = h;
    sprites->Push(si);
  } while (FindNextFileA(h, &fd));
#else
  DIR* d = opendir(dir);
  if (d == nullptr) return;
  DEFER([d] { closedir(d); });
  struct dirent* entry;
  while ((entry = readdir(d)) != nullptr) {
    if (entry->d_name[0] == '.') continue;
    FixedStringBuffer<1024> full_path(dir, "/", entry->d_name);
    struct stat st;
    if (stat(full_path.str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      if (recursive) {
        FixedStringBuffer<256> sub_prefix(allocator);
        if (prefix[0]) sub_prefix.Append(prefix, "/");
        sub_prefix.Append(entry->d_name);
        CollectImages(full_path.str(), sub_prefix.str(), recursive, sprites,
                      allocator);
      }
      continue;
    }
    std::string_view ext = Extension(entry->d_name);
    if (!IsSupportedImage(ext)) continue;
    std::string_view name_part = WithoutExt(entry->d_name);
    FixedStringBuffer<256> sprite_name(allocator);
    if (prefix[0]) sprite_name.Append(prefix, "/");
    sprite_name.Append(name_part);
    size_t file_size = 0;
    uint8_t* file_data = ReadEntireFile(full_path.str(), &file_size, allocator);
    if (file_data == nullptr) continue;
    int w, h, ch;
    uint8_t* pixels;
    if (ext == "qoi") {
      QoiDesc desc;
      pixels = static_cast<uint8_t*>(
          QoiDecode(file_data, static_cast<int>(file_size), &desc,
                    /*channels=*/4, allocator));
      w = static_cast<int>(desc.width);
      h = static_cast<int>(desc.height);
    } else {
      pixels = stbi_load_from_memory(file_data, static_cast<int>(file_size), &w,
                                     &h, &ch, /*desired_channels=*/4);
    }
    if (pixels == nullptr) {
      fprintf(stderr, "Warning: could not decode '%s', skipping.\n",
              full_path.str());
      continue;
    }
    SpriteInput si;
    si.name = StrDupZ(allocator, sprite_name.piece());
    si.pixels = pixels;
    si.width = w;
    si.height = h;
    sprites->Push(si);
  }
#endif
}

// Blit a sprite's RGBA pixels into the atlas at (dst_x, dst_y).
void BlitSprite(uint8_t* atlas, int atlas_w, const SpriteInput& sprite,
                int dst_x, int dst_y) {
  for (int y = 0; y < sprite.height; ++y) {
    const uint8_t* src = sprite.pixels + y * sprite.width * 4;
    uint8_t* dst = atlas + ((dst_y + y) * atlas_w + dst_x) * 4;
    memcpy(dst, src, sprite.width * 4);
  }
}

// Write the .sprites.json metadata file.
bool WriteSpriteJson(const char* path, const char* atlas_filename, int atlas_w,
                     int atlas_h, const DynArray<SpriteInput>& sprites,
                     const stbrp_rect* rects, size_t count) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) {
    fprintf(stderr, "Error: could not open '%s' for writing: %s\n", path,
            strerror(errno));
    return false;
  }
  DEFER([f] { fclose(f); });
  fprintf(f, "{\n");
  fprintf(f, "  \"atlas\": \"%s\",\n", atlas_filename);
  fprintf(f, "  \"width\": %d,\n", atlas_w);
  fprintf(f, "  \"height\": %d,\n", atlas_h);
  fprintf(f, "  \"sprites\": [\n");
  bool first = true;
  for (size_t i = 0; i < count; ++i) {
    if (!rects[i].was_packed) continue;
    if (!first) fprintf(f, ",\n");
    first = false;
    fprintf(f,
            "    {\"name\": \"%s\", \"x\": %d, \"y\": %d, "
            "\"width\": %d, \"height\": %d}",
            sprites[i].name, rects[i].x, rects[i].y, sprites[i].width,
            sprites[i].height);
  }
  fprintf(f, "\n  ]\n");
  fprintf(f, "}\n");
  return true;
}

bool ParseSize(const char* str, int* out_w, int* out_h) {
  // Accept "WxH" or "WXH".
  const char* x = strchr(str, 'x');
  if (x == nullptr) x = strchr(str, 'X');
  if (x == nullptr) return false;
  *out_w = atoi(str);
  *out_h = atoi(x + 1);
  return *out_w > 0 && *out_h > 0;
}

}  // namespace

int CmdAtlas(Slice<const char*> args, Allocator* allocator) {
  const char* input_dir = nullptr;
  const char* output_dir = ".";
  const char* name = "atlas";
  int atlas_w = 2048;
  int atlas_h = 2048;
  int padding = 1;
  bool recursive = false;
  int extrude = 0;

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if ((arg == "-o" || arg == "--output") && i + 1 < args.size()) {
      output_dir = args[++i];
    } else if (arg == "--name" && i + 1 < args.size()) {
      name = args[++i];
    } else if (arg == "--size" && i + 1 < args.size()) {
      if (!ParseSize(args[++i], &atlas_w, &atlas_h)) {
        fprintf(stderr, "Error: invalid size '%s'. Use WxH (e.g. 1024x1024).\n",
                args[i]);
        return 1;
      }
    } else if (arg == "--padding" && i + 1 < args.size()) {
      padding = atoi(args[++i]);
    } else if (arg == "--extrude" && i + 1 < args.size()) {
      extrude = atoi(args[++i]);
    } else if (arg == "--recursive") {
      recursive = true;
    } else if (arg[0] != '-') {
      input_dir = args[i];
    }
  }

  if (input_dir == nullptr) {
    fprintf(stderr, "Error: no input directory specified.\n");
    fprintf(stderr, "Usage: game atlas <input-dir> [options]\n");
    return 1;
  }

  if (!DirectoryExists(input_dir)) {
    fprintf(stderr, "Error: directory not found: '%s'\n", input_dir);
    return 1;
  }

  // Create output directory if it doesn't exist.
  if (!DirectoryExists(output_dir)) {
    if (MakeDirs(output_dir).is_error()) {
      fprintf(stderr, "Error: could not create output directory '%s'\n",
              output_dir);
      return 1;
    }
  }

  ArenaAllocator arena(allocator, Megabytes(256));

  // Collect all images.
  DynArray<SpriteInput> sprites(&arena);
  CollectImages(input_dir, "", recursive, &sprites, &arena);

  if (sprites.empty()) {
    fprintf(stderr, "Error: no images found in '%s'.\n", input_dir);
    return 1;
  }

  printf("Found %zu sprites.\n", sprites.size());

  // Total padding per sprite (padding + extrude on each side).
  int pad = padding + extrude;

  // Pack sprites into atlas pages.
  int atlas_index = 0;
  size_t remaining = sprites.size();
  auto* rects = arena.NewArray<stbrp_rect>(sprites.size());
  for (size_t i = 0; i < sprites.size(); ++i) {
    rects[i].id = static_cast<int>(i);
    rects[i].w = static_cast<stbrp_coord>(sprites[i].width + pad * 2);
    rects[i].h = static_cast<stbrp_coord>(sprites[i].height + pad * 2);
  }

  // Track which sprites haven't been packed yet.
  auto* unpacked = arena.NewArray<size_t>(sprites.size());
  for (size_t i = 0; i < sprites.size(); ++i) unpacked[i] = i;

  while (remaining > 0) {
    // Allocate packing context.
    int node_count = atlas_w;
    auto* nodes = arena.NewArray<stbrp_node>(node_count);
    stbrp_context ctx;
    stbrp_init_target(&ctx, atlas_w, atlas_h, nodes, node_count);

    // Build rect array for unpacked sprites.
    auto* page_rects = arena.NewArray<stbrp_rect>(remaining);
    for (size_t i = 0; i < remaining; ++i) {
      page_rects[i] = rects[unpacked[i]];
    }

    stbrp_pack_rects(&ctx, page_rects, static_cast<int>(remaining));

    // Copy results back and count packed/unpacked.
    size_t packed_count = 0;
    size_t new_remaining = 0;
    auto* new_unpacked = arena.NewArray<size_t>(remaining);
    for (size_t i = 0; i < remaining; ++i) {
      size_t idx = unpacked[i];
      rects[idx] = page_rects[i];
      if (page_rects[i].was_packed) {
        packed_count++;
      } else {
        new_unpacked[new_remaining++] = idx;
      }
    }

    if (packed_count == 0) {
      fprintf(stderr,
              "Error: sprite '%s' (%dx%d) does not fit in %dx%d atlas.\n",
              sprites[new_unpacked[0]].name, sprites[new_unpacked[0]].width,
              sprites[new_unpacked[0]].height, atlas_w, atlas_h);
      return 1;
    }

    // Composite the atlas image (RGBA).
    size_t atlas_pixels = static_cast<size_t>(atlas_w) * atlas_h * 4;
    auto* atlas_buf = arena.NewArray<uint8_t>(atlas_pixels);
    memset(atlas_buf, 0, atlas_pixels);

    for (size_t i = 0; i < sprites.size(); ++i) {
      if (!rects[i].was_packed) continue;
      // Check if this sprite was packed in this page.
      bool in_this_page = false;
      for (size_t j = 0; j < remaining; ++j) {
        if (unpacked[j] == i) {
          in_this_page = true;
          break;
        }
      }
      if (!in_this_page) continue;
      BlitSprite(atlas_buf, atlas_w, sprites[i], rects[i].x + pad,
                 rects[i].y + pad);
    }

    // Encode atlas as QOI.
    QoiDesc qoi_desc;
    qoi_desc.width = atlas_w;
    qoi_desc.height = atlas_h;
    qoi_desc.channels = 4;
    qoi_desc.colorspace = QoiColorspace::kLinear;
    int qoi_len;
    auto* qoi_data = QoiEncode(atlas_buf, &qoi_desc, &qoi_len, &arena);
    if (qoi_data == nullptr) {
      fprintf(stderr, "Error: failed to encode atlas QOI.\n");
      return 1;
    }

    // Build output filenames.
    FixedStringBuffer<1024> qoi_path;
    FixedStringBuffer<1024> json_path;
    FixedStringBuffer<256> atlas_filename;
    if (new_remaining == 0 && atlas_index == 0) {
      // Single atlas: no index suffix.
      qoi_path.Append(output_dir, "/", name, ".qoi");
      json_path.Append(output_dir, "/", name, ".sprites.json");
      atlas_filename.Append(name, ".qoi");
    } else {
      qoi_path.AppendF("%s/%s_%d.qoi", output_dir, name, atlas_index);
      json_path.AppendF("%s/%s_%d.sprites.json", output_dir, name, atlas_index);
      atlas_filename.AppendF("%s_%d.qoi", name, atlas_index);
    }

    // Write QOI file.
    if (!WriteEntireFile(qoi_path.str(), qoi_data, qoi_len)) return 1;

    // Write sprites.json. Adjust rects to exclude padding.
    // Temporarily adjust rect positions to point to the sprite, not the padded
    // area.
    for (size_t i = 0; i < sprites.size(); ++i) {
      rects[i].x += pad;
      rects[i].y += pad;
    }
    if (!WriteSpriteJson(json_path.str(), atlas_filename.str(), atlas_w,
                         atlas_h, sprites, rects, sprites.size())) {
      return 1;
    }
    // Undo adjustment for any remaining packing iterations.
    for (size_t i = 0; i < sprites.size(); ++i) {
      rects[i].x -= pad;
      rects[i].y -= pad;
    }

    printf("  %s (%d sprites)\n", qoi_path.str(),
           static_cast<int>(packed_count));
    printf("  %s\n", json_path.str());

    unpacked = new_unpacked;
    remaining = new_remaining;
    atlas_index++;
  }

  return 0;
}

}  // namespace G
