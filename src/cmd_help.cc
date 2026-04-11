#include <cstdio>
#include <string_view>

#include "cli.h"
#include "version.h"

namespace G {

namespace {

void PrintGeneralHelp(const char* prog) {
  printf(
      "game engine v%s\n"
      "\n"
      "Usage: %s <command> [options]\n"
      "\n"
      "Commands:\n"
      "  init [dir]           Create a new game project\n"
      "  run [dir] [-- args]  Run a game with hot-reloading\n"
      "  clean [dir]          Delete cached asset database\n"
      "  package [dir]        Package a game for distribution\n"
      "  stubs [--output]     Generate LuaLS type stubs\n"
      "  convert <file>       Convert assets between formats\n"
      "  atlas <dir>          Pack images into a texture atlas\n"
      "  version              Print engine version\n"
      "  help [command]       Show help for a command\n"
      "\n"
      "Run '%s help <command>' for details on a specific command.\n",
      GAME_VERSION_STR, prog, prog);
}

void PrintInitHelp(const char* prog) {
  printf(
      "Usage: %s init [directory]\n"
      "\n"
      "Creates a new game project with scaffold files.\n"
      "If no directory is given, uses the current directory.\n"
      "\n"
      "Creates:\n"
      "  conf.json            Window and project configuration\n"
      "  main.lua             Entry point\n"
      "  game.lua             Starter game module with init/update/draw\n"
      "  .luarc.json          LuaLS workspace config\n"
      "  definitions/game.lua LuaLS type stubs\n",
      prog);
}

void PrintCleanHelp(const char* prog) {
  printf(
      "Usage: %s clean [directory]\n"
      "\n"
      "Deletes the cached asset database for the given project directory\n"
      "(default: current directory). The cache is rebuilt automatically\n"
      "on the next 'game run'.\n",
      prog);
}

void PrintRunHelp(const char* prog) {
  printf(
      "Usage: %s run [directory] [-- game-args...]\n"
      "\n"
      "Runs the game in the given directory (default: current directory)\n"
      "with hot-reloading enabled.\n"
      "\n"
      "The asset database is cached in ~/.cache/game/ and managed\n"
      "automatically. Arguments after '--' are forwarded to the game.\n"
      "\n"
      "Options:\n"
      "  --clean         Delete cached database and repack from scratch\n"
      "  --no-hotreload  Disable the file watcher\n",
      prog);
}

void PrintPackageHelp(const char* prog) {
  printf(
      "Usage: %s package [directory] [options]\n"
      "\n"
      "Packages the game into a self-contained distributable.\n"
      "\n"
      "Options:\n"
      "  -o, --output <dir>  Output directory (default: ./dist)\n"
      "  --name <name>       Override binary name (default: from conf.json)\n"
      "  --strip             Strip debug symbols from the binary\n"
      "  --zip               Produce a .zip archive\n",
      prog);
}

void PrintConvertHelp(const char* prog) {
  printf(
      "Usage: %s convert <input> [options]\n"
      "\n"
      "Converts assets to/from the engine's native formats.\n"
      "\n"
      "Supported conversions:\n"
      "  PNG, JPEG, BMP, GIF, TGA -> QOI  (image to engine format)\n"
      "  QOI -> PNG                       (engine format to portable)\n"
      "  WAV, OGG -> QOA                  (audio to engine format)\n"
      "  QOA -> WAV                       (engine format to portable)\n"
      "\n"
      "Options:\n"
      "  -o, --output <path>  Output file path (default: input with new ext)\n"
      "  -f, --format <fmt>   Output format (inferred from extension)\n",
      prog);
}

void PrintAtlasHelp(const char* prog) {
  printf(
      "Usage: %s atlas <input-dir> [options]\n"
      "\n"
      "Packs loose images into a texture atlas with sprite metadata.\n"
      "Outputs a .qoi atlas image and a .sprites.json metadata file.\n"
      "\n"
      "Input formats: PNG, JPEG, BMP, GIF, TGA, QOI.\n"
      "\n"
      "Options:\n"
      "  -o, --output <dir>   Output directory (default: current directory)\n"
      "  --name <name>        Base name for output files (default: atlas)\n"
      "  --size <WxH>         Maximum atlas size (default: 2048x2048)\n"
      "  --padding <px>       Pixels between sprites (default: 1)\n"
      "  --extrude <px>       Extrude sprite edges by N pixels\n"
      "  --recursive          Include images from subdirectories\n",
      prog);
}

void PrintStubsHelp(const char* prog) {
  printf(
      "Usage: %s stubs [--output <path>]\n"
      "\n"
      "Generates LuaLS type stubs for IDE autocomplete.\n"
      "Does not require a running game window.\n"
      "\n"
      "Options:\n"
      "  --output <path>  Output file path (default: definitions/game.lua)\n",
      prog);
}

}  // namespace

int CmdHelp(const char* argv0, const char* subcommand) {
  if (subcommand == nullptr) {
    PrintGeneralHelp(argv0);
    return 0;
  }

  std::string_view cmd = subcommand;
  if (cmd == "init") {
    PrintInitHelp(argv0);
  } else if (cmd == "clean") {
    PrintCleanHelp(argv0);
  } else if (cmd == "run") {
    PrintRunHelp(argv0);
  } else if (cmd == "package") {
    PrintPackageHelp(argv0);
  } else if (cmd == "stubs") {
    PrintStubsHelp(argv0);
  } else if (cmd == "convert") {
    PrintConvertHelp(argv0);
  } else if (cmd == "atlas") {
    PrintAtlasHelp(argv0);
  } else if (cmd == "version") {
    printf("Usage: %s version\n\nPrints the engine version and exits.\n",
           argv0);
  } else if (cmd == "help") {
    printf("Usage: %s help [command]\n\nShows help for a command.\n", argv0);
  } else {
    fprintf(stderr, "Unknown command: %s\n\n", subcommand);
    PrintGeneralHelp(argv0);
    return 1;
  }
  return 0;
}

}  // namespace G
