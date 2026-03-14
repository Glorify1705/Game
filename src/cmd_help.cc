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
      "  package [dir]        Package a game for distribution\n"
      "  stubs [--output]     Generate LuaLS type stubs\n"
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
  } else if (cmd == "run") {
    PrintRunHelp(argv0);
  } else if (cmd == "package") {
    PrintPackageHelp(argv0);
  } else if (cmd == "stubs") {
    PrintStubsHelp(argv0);
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
