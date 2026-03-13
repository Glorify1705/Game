#pragma once
#ifndef _GAME_CLI_H
#define _GAME_CLI_H

#include "array.h"

namespace G {

// Scaffold a new game project: game init [dir]
int CmdInit(Slice<const char*> args);

// Run a game project in development mode: game run [dir] [--no-hotreload]
// [--clean]
int CmdRun(Slice<const char*> args);

// Run a packaged game from assets.sqlite3 adjacent to the binary.
int CmdRunPackaged(Slice<const char*> args);

// Bundle binary + assets for distribution: game package [dir] [-o dir]
// [--strip]
int CmdPackage(Slice<const char*> args);

// Generate LuaLS type stubs without starting the engine: game stubs [--output
// path]
int CmdStubs(Slice<const char*> args);

// Print engine version.
int CmdVersion();

// Print usage information, optionally for a specific subcommand.
int CmdHelp(const char* subcommand);

// Check if assets.sqlite3 exists adjacent to the running binary.
bool PackagedGameExists(const char* argv0);

}  // namespace G

#endif  // _GAME_CLI_H
