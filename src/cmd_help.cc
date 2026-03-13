#include <cstdio>
#include <string_view>

#include "cli.h"
#include "version.h"

namespace G {

namespace {

void PrintGeneralHelp() {
  printf(
      "game engine v%s\n"
      "\n"
      "Usage: game <command> [options]\n"
      "\n"
      "Commands:\n"
      "  init [dir]           Create a new game project\n"
      "  run [dir] [-- args]  Run a game with hot-reloading\n"
      "  package [dir]        Package a game for distribution\n"
      "  stubs [--output]     Generate LuaLS type stubs\n"
      "  version              Print engine version\n"
      "  help [command]       Show help for a command\n"
      "\n"
      "Run 'game help <command>' for details on a specific command.\n",
      GAME_VERSION_STR);
}

void PrintInitHelp() {
  printf(
      "Usage: game init [directory]\n"
      "\n"
      "Creates a new game project with scaffold files.\n"
      "If no directory is given, uses the current directory.\n"
      "\n"
      "Creates:\n"
      "  conf.json            Window and project configuration\n"
      "  main.lua             Entry point\n"
      "  game.lua             Starter game module with init/update/draw\n"
      "  .luarc.json          LuaLS workspace config\n"
      "  definitions/game.lua LuaLS type stubs\n");
}

void PrintRunHelp() {
  printf(
      "Usage: game run [directory] [-- game-args...]\n"
      "\n"
      "Runs the game in the given directory (default: current directory)\n"
      "with hot-reloading enabled.\n"
      "\n"
      "The asset database is cached in ~/.cache/game/ and managed\n"
      "automatically. Arguments after '--' are forwarded to the game.\n"
      "\n"
      "Options:\n"
      "  --clean         Delete cached database and repack from scratch\n"
      "  --no-hotreload  Disable the file watcher\n");
}

void PrintPackageHelp() {
  printf(
      "Usage: game package [directory] [options]\n"
      "\n"
      "Packages the game into a self-contained distributable.\n"
      "\n"
      "Options:\n"
      "  -o, --output <dir>  Output directory (default: ./dist)\n"
      "  --name <name>       Override binary name (default: from conf.json)\n"
      "  --strip             Strip debug symbols from the binary\n"
      "  --zip               Produce a .zip archive\n");
}

void PrintStubsHelp() {
  printf(
      "Usage: game stubs [--output <path>]\n"
      "\n"
      "Generates LuaLS type stubs for IDE autocomplete.\n"
      "Does not require a running game window.\n"
      "\n"
      "Options:\n"
      "  --output <path>  Output file path (default: definitions/game.lua)\n");
}

}  // namespace

int CmdHelp(const char* subcommand) {
  if (subcommand == nullptr) {
    PrintGeneralHelp();
  } else {
    std::string_view cmd = subcommand;
    if (cmd == "init") {
      PrintInitHelp();
    } else if (cmd == "run") {
      PrintRunHelp();
    } else if (cmd == "package") {
      PrintPackageHelp();
    } else if (cmd == "stubs") {
      PrintStubsHelp();
    } else if (cmd == "version") {
      printf("Usage: game version\n\nPrints the engine version and exits.\n");
    } else if (cmd == "help") {
      printf("Usage: game help [command]\n\nShows help for a command.\n");
    } else {
      fprintf(stderr, "Unknown command: %s\n\n", subcommand);
      PrintGeneralHelp();
      return 1;
    }
  }
  return 0;
}

}  // namespace G
