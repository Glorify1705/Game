#include "cli_metadata.h"

namespace G {

namespace {

template <typename T, size_t N>
constexpr Slice<const T> Flags(const T (&arr)[N]) {
  return Slice<const T>(arr, N);
}

constexpr CliFlag kRunFlags[] = {
    {"clean", '\0', "", "Delete cached database and repack from scratch"},
    {"no-hotreload", '\0', "", "Disable the file watcher"},
    {"test", '\0', "", "Run in test mode"},
};

constexpr CliFlag kPackageFlags[] = {
    {"output", 'o', "dir", "Output directory (default: ./dist)"},
    {"name", '\0', "name", "Override binary name (default: from conf.json)"},
    {"engine-binary", '\0', "path",
     "Use a pre-built engine binary instead of self"},
    {"strip", '\0', "", "Strip debug symbols from the binary"},
    {"sfx", '\0', "", "Produce a self-extracting Windows .exe"},
    {"zip", '\0', "", "Produce a .zip archive"},
};

constexpr CliFlag kStubsFlags[] = {
    {"output", '\0', "path",
     "Output file path (default: definitions/game.lua)"},
};

constexpr CliFlag kConvertFlags[] = {
    {"output", 'o', "path", "Output file path (default: input with new ext)"},
    {"format", 'f', "fmt", "Output format (inferred from extension)"},
};

constexpr CliFlag kAtlasFlags[] = {
    {"output", 'o', "dir", "Output directory (default: current directory)"},
    {"name", '\0', "name", "Base name for output files (default: atlas)"},
    {"size", '\0', "WxH", "Maximum atlas size (default: 2048x2048)"},
    {"padding", '\0', "px", "Pixels between sprites (default: 1)"},
    {"extrude", '\0', "px", "Extrude sprite edges by N pixels"},
    {"recursive", '\0', "", "Include images from subdirectories"},
};

constexpr Slice<const CliFlag> kNoFlags;

// clang-format off
constexpr CliCommand kCommands[] = {
    {"init",
     "Create a new game project",
     "Creates a new game project with scaffold files.\n"
     "If no directory is given, uses the current directory.",
     "[dir]",
     kNoFlags,
     "Creates:\n"
     "  conf.json            Window and project configuration\n"
     "  main.lua             Entry point\n"
     "  game.lua             Starter game module with init/update/draw\n"
     "  .luarc.json          LuaLS workspace config\n"
     "  definitions/game.lua LuaLS type stubs"},

    {"run",
     "Run a game with hot-reloading",
     "Runs the game in the given directory (default: current directory)\n"
     "with hot-reloading enabled.\n"
     "\n"
     "The asset database is cached in ~/.cache/game/ and managed\n"
     "automatically. Arguments after '--' are forwarded to the game.",
     "[dir] [-- args]",
     Flags(kRunFlags),
     ""},

    {"clean",
     "Delete cached asset database",
     "Deletes the cached asset database for the given project directory\n"
     "(default: current directory). The cache is rebuilt automatically\n"
     "on the next 'game run'.",
     "[dir]",
     kNoFlags,
     ""},

    {"package",
     "Package a game for distribution",
     "Packages the game into a self-contained distributable.",
     "[directory] [options]",
     Flags(kPackageFlags),
     ""},

    {"stubs",
     "Generate LuaLS type stubs",
     "Generates LuaLS type stubs for IDE autocomplete.\n"
     "Does not require a running game window.",
     "[--output <path>]",
     Flags(kStubsFlags),
     ""},

    {"convert",
     "Convert assets between formats",
     "Converts assets to/from the engine's native formats.",
     "<input> [options]",
     Flags(kConvertFlags),
     "Supported conversions:\n"
     "  PNG, JPEG, BMP, GIF, TGA -> QOI  (image to engine format)\n"
     "  QOI -> PNG                       (engine format to portable)\n"
     "  WAV, OGG -> QOA                  (audio to engine format)\n"
     "  QOA -> WAV                       (engine format to portable)"},

    {"atlas",
     "Pack images into a texture atlas",
     "Packs loose images into a texture atlas with sprite metadata.\n"
     "Outputs a .qoi atlas image and a .sprites.json metadata file.\n"
     "\n"
     "Input formats: PNG, JPEG, BMP, GIF, TGA, QOI.",
     "<input-dir> [options]",
     Flags(kAtlasFlags),
     ""},

    {"version",
     "Print engine version",
     "Prints the engine version and exits.",
     "",
     kNoFlags,
     ""},

    {"help",
     "Show help for a command",
     "Shows help for a command.",
     "[command]",
     kNoFlags,
     ""},

    {"completions",
     "Generate shell completions or man page",
     "Outputs shell completions or a man page to stdout.\n"
     "Redirect the output to install it.",
     "{bash|zsh|man}",
     kNoFlags,
     ""},
};
// clang-format on

}  // namespace

Slice<const CliCommand> GetCommands() {
  return Slice<const CliCommand>(kCommands,
                                 sizeof(kCommands) / sizeof(kCommands[0]));
}

const CliCommand* FindCommand(std::string_view name) {
  auto commands = GetCommands();
  for (size_t i = 0; i < commands.size(); ++i) {
    if (commands[i].name == name) return &commands[i];
  }
  return nullptr;
}

}  // namespace G
