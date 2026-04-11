#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

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

// Decode an image file (any supported format) into RGBA pixels.
uint8_t* DecodeImage(const uint8_t* file_data, size_t file_size,
                     std::string_view ext, int* out_w, int* out_h,
                     Allocator* allocator) {
  if (ext == "qoi") {
    QoiDesc desc;
    auto* pixels =
        static_cast<uint8_t*>(QoiDecode(file_data, static_cast<int>(file_size),
                                        &desc, /*channels=*/4, allocator));
    if (pixels == nullptr) return nullptr;
    *out_w = static_cast<int>(desc.width);
    *out_h = static_cast<int>(desc.height);
    return pixels;
  }
  int ch;
  return stbi_load_from_memory(file_data, static_cast<int>(file_size), out_w,
                               out_h, &ch, /*desired_channels=*/4);
}

// State passed through the directory iteration callback.
struct CollectState {
  const char* dir;
  const char* prefix;
  bool recursive;
  DynArray<SpriteInput>* sprites;
  Allocator* allocator;
};

// Forward declaration for recursive calls.
void CollectImages(const char* dir, const char* prefix, bool recursive,
                   DynArray<SpriteInput>* sprites, Allocator* allocator);

void CollectCallback(const DirEntry& entry, void* userdata) {
  auto* state = static_cast<CollectState*>(userdata);
  if (entry.type == DirEntryType::kDirectory) {
    if (state->recursive) {
      FixedStringBuffer<1024> sub_dir(state->dir, "/", entry.name);
      FixedStringBuffer<256> sub_prefix(state->allocator);
      if (state->prefix[0]) sub_prefix.Append(state->prefix, "/");
      sub_prefix.Append(entry.name);
      CollectImages(sub_dir.str(), sub_prefix.str(), state->recursive,
                    state->sprites, state->allocator);
    }
    return;
  }
  std::string_view ext = Extension(entry.name);
  if (!IsSupportedImage(ext)) return;

  FixedStringBuffer<1024> full_path(state->dir, "/", entry.name);
  uint8_t* file_data = nullptr;
  auto result = ReadEntireFile(full_path.str(), &file_data, state->allocator);
  if (result.is_error()) return;
  size_t file_size = result.value();

  int w, h;
  uint8_t* pixels =
      DecodeImage(file_data, file_size, ext, &w, &h, state->allocator);
  if (pixels == nullptr) {
    fprintf(stderr, "Warning: could not decode '%s', skipping.\n",
            full_path.str());
    return;
  }

  std::string_view name_part = WithoutExt(entry.name);
  FixedStringBuffer<256> sprite_name(state->allocator);
  if (state->prefix[0]) sprite_name.Append(state->prefix, "/");
  sprite_name.Append(name_part);

  SpriteInput si;
  si.name = StrDupZ(state->allocator, sprite_name.piece());
  si.pixels = pixels;
  si.width = w;
  si.height = h;
  state->sprites->Push(si);
}

// Collect image files from a directory. Optionally recurse into subdirectories.
void CollectImages(const char* dir, const char* prefix, bool recursive,
                   DynArray<SpriteInput>* sprites, Allocator* allocator) {
  CollectState state;
  state.dir = dir;
  state.prefix = prefix;
  state.recursive = recursive;
  state.sprites = sprites;
  state.allocator = allocator;
  // Ignore errors — empty or missing directories just yield no sprites.
  (void)IterateDirectory(dir, CollectCallback, &state);
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
                     int atlas_h, Slice<const SpriteInput> sprites,
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

// Parse a "WxH" size string.
bool ParseSize(const char* str, int* out_w, int* out_h) {
  const char* x = strchr(str, 'x');
  if (x == nullptr) x = strchr(str, 'X');
  if (x == nullptr) return false;
  *out_w = atoi(str);
  *out_h = atoi(x + 1);
  return *out_w > 0 && *out_h > 0;
}

// Parsed command-line arguments for `game atlas`.
struct AtlasArgs {
  const char* input_dir = nullptr;
  const char* output_dir = ".";
  const char* name = "atlas";
  int atlas_w = 2048;
  int atlas_h = 2048;
  int padding = 1;
  int extrude = 0;
  bool recursive = false;
};

// Parse atlas command-line arguments. Returns 0 on success.
int ParseAtlasArgs(Slice<const char*> args, AtlasArgs* out) {
  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if ((arg == "-o" || arg == "--output") && i + 1 < args.size()) {
      out->output_dir = args[++i];
    } else if (arg == "--name" && i + 1 < args.size()) {
      out->name = args[++i];
    } else if (arg == "--size" && i + 1 < args.size()) {
      if (!ParseSize(args[++i], &out->atlas_w, &out->atlas_h)) {
        fprintf(stderr, "Error: invalid size '%s'. Use WxH (e.g. 1024x1024).\n",
                args[i]);
        return 1;
      }
    } else if (arg == "--padding" && i + 1 < args.size()) {
      out->padding = atoi(args[++i]);
    } else if (arg == "--extrude" && i + 1 < args.size()) {
      out->extrude = atoi(args[++i]);
    } else if (arg == "--recursive") {
      out->recursive = true;
    } else if (arg[0] != '-') {
      out->input_dir = args[i];
    }
  }

  if (out->input_dir == nullptr) {
    fprintf(stderr, "Error: no input directory specified.\n");
    fprintf(stderr, "Usage: game atlas <input-dir> [options]\n");
    return 1;
  }

  if (!DirectoryExists(out->input_dir)) {
    fprintf(stderr, "Error: directory not found: '%s'\n", out->input_dir);
    return 1;
  }

  return 0;
}

// Encode and write a single atlas page as QOI. Returns false on error.
bool WriteAtlasQoi(const char* path, const uint8_t* atlas_buf, int atlas_w,
                   int atlas_h, Allocator* allocator) {
  QoiDesc qoi_desc;
  qoi_desc.width = atlas_w;
  qoi_desc.height = atlas_h;
  qoi_desc.channels = 4;
  qoi_desc.colorspace = QoiColorspace::kLinear;
  int qoi_len;
  auto* qoi_data = QoiEncode(atlas_buf, &qoi_desc, &qoi_len, allocator);
  if (qoi_data == nullptr) {
    fprintf(stderr, "Error: failed to encode atlas QOI.\n");
    return false;
  }
  return !WriteEntireFile(path, qoi_data, qoi_len).is_error();
}

// Composite sprites into an atlas buffer and write QOI + JSON files.
int WriteAtlasPage(const AtlasArgs& opts, int atlas_index, bool is_last,
                   Slice<const SpriteInput> sprites, stbrp_rect* rects,
                   const size_t* unpacked, size_t remaining, int pad,
                   Allocator* allocator) {
  int atlas_w = opts.atlas_w;
  int atlas_h = opts.atlas_h;

  // Composite the atlas image (RGBA).
  size_t atlas_pixels = static_cast<size_t>(atlas_w) * atlas_h * 4;
  auto* atlas_buf = allocator->NewArray<uint8_t>(atlas_pixels);
  memset(atlas_buf, 0, atlas_pixels);

  for (size_t j = 0; j < remaining; ++j) {
    size_t i = unpacked[j];
    if (!rects[i].was_packed) continue;
    BlitSprite(atlas_buf, atlas_w, sprites[i], rects[i].x + pad,
               rects[i].y + pad);
  }

  // Build output filenames.
  FixedStringBuffer<1024> qoi_path;
  FixedStringBuffer<1024> json_path;
  FixedStringBuffer<256> atlas_filename;
  if (is_last && atlas_index == 0) {
    qoi_path.Append(opts.output_dir, "/", opts.name, ".qoi");
    json_path.Append(opts.output_dir, "/", opts.name, ".sprites.json");
    atlas_filename.Append(opts.name, ".qoi");
  } else {
    qoi_path.AppendF("%s/%s_%d.qoi", opts.output_dir, opts.name, atlas_index);
    json_path.AppendF("%s/%s_%d.sprites.json", opts.output_dir, opts.name,
                      atlas_index);
    atlas_filename.AppendF("%s_%d.qoi", opts.name, atlas_index);
  }

  if (!WriteAtlasQoi(qoi_path.str(), atlas_buf, atlas_w, atlas_h, allocator)) {
    return 1;
  }

  // Adjust rects to exclude padding for JSON output.
  for (size_t i = 0; i < sprites.size(); ++i) {
    rects[i].x += pad;
    rects[i].y += pad;
  }
  if (!WriteSpriteJson(json_path.str(), atlas_filename.str(), atlas_w, atlas_h,
                       sprites, rects, sprites.size())) {
    return 1;
  }
  // Undo adjustment for any remaining packing iterations.
  for (size_t i = 0; i < sprites.size(); ++i) {
    rects[i].x -= pad;
    rects[i].y -= pad;
  }

  printf("  %s\n", qoi_path.str());
  printf("  %s\n", json_path.str());
  return 0;
}

}  // namespace

int CmdAtlas(Slice<const char*> args, Allocator* allocator) {
  AtlasArgs opts;
  int parse_result = ParseAtlasArgs(args, &opts);
  if (parse_result != 0) return parse_result;

  // Create output directory if it doesn't exist.
  if (!DirectoryExists(opts.output_dir)) {
    if (MakeDirs(opts.output_dir).is_error()) {
      fprintf(stderr, "Error: could not create output directory '%s'\n",
              opts.output_dir);
      return 1;
    }
  }

  ArenaAllocator arena(allocator, Megabytes(256));

  // Collect all images.
  DynArray<SpriteInput> sprites(&arena);
  CollectImages(opts.input_dir, "", opts.recursive, &sprites, &arena);

  if (sprites.empty()) {
    fprintf(stderr, "Error: no images found in '%s'.\n", opts.input_dir);
    return 1;
  }

  printf("Found %zu sprites.\n", sprites.size());

  int pad = opts.padding + opts.extrude;

  // Set up packing rects.
  auto* rects = arena.NewArray<stbrp_rect>(sprites.size());
  for (size_t i = 0; i < sprites.size(); ++i) {
    rects[i].id = static_cast<int>(i);
    rects[i].w = static_cast<stbrp_coord>(sprites[i].width + pad * 2);
    rects[i].h = static_cast<stbrp_coord>(sprites[i].height + pad * 2);
  }

  // Track which sprites haven't been packed yet.
  auto* unpacked = arena.NewArray<size_t>(sprites.size());
  for (size_t i = 0; i < sprites.size(); ++i) unpacked[i] = i;
  size_t remaining = sprites.size();
  int atlas_index = 0;

  while (remaining > 0) {
    int node_count = opts.atlas_w;
    auto* nodes = arena.NewArray<stbrp_node>(node_count);
    stbrp_context ctx;
    stbrp_init_target(&ctx, opts.atlas_w, opts.atlas_h, nodes, node_count);

    auto* page_rects = arena.NewArray<stbrp_rect>(remaining);
    for (size_t i = 0; i < remaining; ++i) {
      page_rects[i] = rects[unpacked[i]];
    }

    stbrp_pack_rects(&ctx, page_rects, static_cast<int>(remaining));

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
              sprites[new_unpacked[0]].height, opts.atlas_w, opts.atlas_h);
      return 1;
    }

    Slice<const SpriteInput> sprite_slice(sprites.cdata(), sprites.size());
    int result =
        WriteAtlasPage(opts, atlas_index, new_remaining == 0, sprite_slice,
                       rects, unpacked, remaining, pad, &arena);
    if (result != 0) return result;

    printf("  (%d sprites)\n", static_cast<int>(packed_count));

    unpacked = new_unpacked;
    remaining = new_remaining;
    atlas_index++;
  }

  return 0;
}

}  // namespace G
