---
status: implemented
tags: [assets, cli, tools, texture-packing]
---

# Asset Conversion Tools

**Status: Under consideration.**

## Problem

Working with third-party assets (Kenney, OpenGameArt, random GitHub repos)
requires manual work that the engine should automate:

1. **Kenney ships individual PNGs.** You get 200 loose sprite files. To use
   them in the engine you must manually pack them into an atlas, then write a
   `.sprites.json` by hand with all the coordinates. This is tedious enough
   to discourage using free assets at all.

2. **No standalone converters.** The packer handles PNG→QOI and WAV/OGG→QOA
   implicitly, but there's no way to pre-convert assets yourself. For
   production you want assets pre-converted to QOI/QOA so the packer just
   copies bytes (faster iteration, faster packaging). For formats the packer
   doesn't handle (JPEG, MP3, FLAC) you're stuck with external tools.

## Design goals

1. **One command for each workflow.** `game atlas` packs sprites.
   `game convert` converts between formats. Flags and arguments only — no
   configuration files.

2. **Works outside a game project.** These are standalone tools. They don't
   need `conf.json`, a SQLite database, or PhysFS. They read files from the
   filesystem and write files to the filesystem.

3. **Composable with the existing pipeline.** The output of `game atlas` is a
   QOI + `.sprites.json` that drops straight into a game's `assets/`
   directory. `game convert` outputs QOI/QOA files ready for the packer.
   The packer continues to accept PNG/WAV/OGG for convenience during early
   experimentation — pre-converting is optional but faster.

4. **Support common third-party formats.** JPEG, BMP, GIF, and TGA for images
   (all supported by stb_image). MP3 and FLAC for audio (via dr_mp3 and
   dr_flac, both single-header). Accept what people actually have.

## Commands

### `game atlas`

Pack loose images into a texture atlas with sprite metadata.

```
game atlas <input-dir> [options]

Options:
  -o, --output <dir>     Output directory (default: current directory)
  --name <name>          Base name for output files (default: "atlas")
  --size <WxH>           Maximum atlas size (default: 2048x2048)
  --padding <px>         Pixels between sprites (default: 1)
  --recursive            Include images from subdirectories
  --extrude <px>         Extrude sprite edges by N pixels (prevents bleeding)
```

**Input:** a directory of images (PNG, JPEG, BMP, GIF, TGA, QOI).

**Output:** two files:
- `<name>.qoi` — the packed atlas image in the engine's native format.
- `<name>.sprites.json` — sprite metadata in the engine's existing format.

**Algorithm:** `stb_rect_pack` (already vendored). Uses the Skyline
bottom-left algorithm, which produces tight packings without the wasted
space of simpler shelf-based approaches. The library is already compiled
(`libraries/stb_rect_pack.cc`) and ready to link.

If sprites don't fit in a single atlas, emit multiple atlases
(`atlas_0.qoi`, `atlas_1.qoi`, etc.) with separate `.sprites.json` files.

**Sprite naming:** the sprite name in the JSON comes from the filename
without the extension. `player_idle_01.png` becomes sprite `player_idle_01`.
For recursive mode, subdirectories become prefixes: `enemies/bat.png`
becomes `enemies/bat`.

**Example:**

```sh
# Pack all PNGs in kenney_platformer/ into a 1024x1024 atlas.
game atlas kenney_platformer/ --size 1024x1024 --name platformer -o assets/

# Creates:
#   assets/platformer.qoi
#   assets/platformer.sprites.json
```

The `.sprites.json` matches the engine's existing format:

```json
{
  "atlas": "platformer.qoi",
  "width": 1024,
  "height": 1024,
  "sprites": [
    {"name": "player_idle", "x": 0, "y": 0, "width": 64, "height": 64},
    {"name": "player_run_01", "x": 64, "y": 0, "width": 64, "height": 64},
    ...
  ]
}
```

### `game convert`

Convert assets to/from the engine's native formats.

```
game convert <input> [options]

Options:
  -o, --output <path>    Output file path (default: input with new extension)
  -f, --format <fmt>     Output format (inferred from extension if not given)
```

**Supported conversions:**

| Input | Output | Notes |
|-------|--------|-------|
| PNG, JPEG, BMP, GIF, TGA | QOI | Image to engine format |
| QOI | PNG | Engine format back to portable format |
| WAV, OGG, MP3, FLAC | QOA | Audio to engine format |
| QOA | WAV | Engine format back to portable format |

The primary direction is *into* the engine (→ QOI/QOA). The reverse
conversions (QOI → PNG, QOA → WAV) exist for extracting assets back to
formats that external editors understand.

Format is inferred from the output file extension, or specified with `-f`.
If no output path is given, the input filename is used with the appropriate
extension swapped.

**Examples:**

```sh
# Convert a JPEG background to QOI for the engine.
game convert background.jpg

# Convert a sound effect to QOA.
game convert jump.mp3 -o assets/jump.qoa

# Extract a QOI back to PNG for editing in GIMP.
game convert player.qoi -f png

# Batch convert all WAVs in a directory.
for f in sounds/*.wav; do game convert "$f" -o assets/; done
```

**Batch mode** is explicit shell scripting, not a built-in feature. The
commands are fast enough that `for` loops or `xargs` work fine.

### Typical workflows

**Quick experimentation:** drop PNGs and WAVs straight into `assets/`.
The packer converts them on the fly. No extra steps.

**Production / faster iteration:** pre-convert with `game convert` and
`game atlas`. The packer just copies the QOI/QOA bytes — no decoding,
no encoding, no threading overhead.

**Importing a Kenney asset pack:**

```sh
# One command to go from 200 loose PNGs to a packed atlas.
game atlas ~/Downloads/kenney_platformer/PNG/ --name platformer -o assets/

# Audio assets.
for f in ~/Downloads/kenney_platformer/Audio/*.ogg; do
  game convert "$f" -o assets/
done
```

## Implementation

### New source files

- `src/cmd_atlas.cc` — the `game atlas` command.
- `src/cmd_convert.cc` — the `game convert` command.

Both are standalone (no Lua, no SDL, no game loop). They link against the
same image/audio libraries already in the project.

### Dependencies (already vendored)

- **stb_image** — decode PNG, JPEG, BMP, GIF, TGA. Currently only PNG is
  enabled (`STBI_ONLY_PNG` in packer.cc). The conversion tools need all
  formats.
- **stb_image_write** — encode PNG (already used for screenshots).
- **stb_rect_pack** — rectangle packing (already compiled).
- **QOI** — encode/decode (already in `src/image.cc`).
- **QOA** — encode/decode (already in `src/qoa.cc`).
- **stb_vorbis** — decode OGG (already vendored).
- **dr_wav** — decode WAV (already vendored).

### New dependencies (to vendor)

- **dr_mp3** — single-header MP3 decoder. Public domain. Same author and
  API pattern as dr_wav.
- **dr_flac** — single-header FLAC decoder. Public domain. Same author.

### stb_image configuration

The packer defines `STBI_ONLY_PNG` to minimize code size. The conversion
tools need all formats. Create a separate translation unit
(`src/stb_image_all.cc`) with the implementation and all formats enabled.
The packer keeps its PNG-only version. Binary size increases ~30 KB for
the additional decoders.

### Atlas packing

Uses `stb_rect_pack` (Skyline bottom-left):

```
stbrp_init_target(&ctx, atlas_width, atlas_height, nodes, node_count)
stbrp_pack_rects(&ctx, rects, rect_count)
```

One call packs all sprites. Rects that don't fit get `was_packed = 0` and
go into the next atlas.

### Output

The atlas image is encoded as QOI using the existing `QoiEncode` from
`src/image.cc`. The `.sprites.json` is written directly as formatted text
(simple enough that yyjson isn't needed for generation).

### Memory

Both commands use a single arena allocator. Image pixels are loaded into
the arena, the atlas is composited in-place, and everything is freed when
the command exits.

## Rejected alternatives

**TexturePacker/Aseprite integration.** External tool integration adds
complexity for marginal benefit. The built-in tools handle the common case.

**Built-in batch mode (`game convert *.wav`).** Shell globbing already works.
Keep the tool simple: one input, one output.

**Configuration files for atlas settings.** Atlas packing is a one-off
operation with obvious defaults. Flags are sufficient.

**Custom packing algorithm.** `stb_rect_pack` is already vendored, proven,
and efficient. No reason to write our own.

## Future extensions

Out of scope for the initial implementation:

- **`game atlas --watch`** — re-pack when source images change.
- **`game convert --resample <rate>`** — audio resampling during conversion.
- **`game atlas --trim`** — trim transparent borders. Requires storing trim
  offsets in the JSON so the engine can compensate at render time.
- **`game atlas --animation <prefix>`** — detect numbered sequences
  (`run_01.png`, `run_02.png`) and emit animation metadata alongside sprite
  rects. Depends on the animation system design.
- **Tile map support** — `game tilemap` converting Tiled `.tmx` files.
