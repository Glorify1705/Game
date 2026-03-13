#pragma once
#ifndef _GAME_CLI_H
#define _GAME_CLI_H

namespace G {

// Scaffold a new game project: game init [dir]
int CmdInit(int argc, const char* argv[]);

// Run a game project in development mode: game run [dir] [--no-hotreload]
// [--clean]
int CmdRun(int argc, const char* argv[]);

// Run a packaged game from assets.sqlite3 adjacent to the binary.
int CmdRunPackaged(int argc, const char* argv[]);

// Bundle binary + assets for distribution: game package [dir] [-o dir]
// [--strip]
int CmdPackage(int argc, const char* argv[]);

// Generate LuaLS type stubs without starting the engine: game stubs [--output
// path]
int CmdStubs(int argc, const char* argv[]);

// Print engine version.
int CmdVersion();

// Print usage information, optionally for a specific subcommand.
int CmdHelp(const char* subcommand);

// Check if assets.sqlite3 exists adjacent to the running binary.
bool PackagedGameExists(const char* argv0);

}  // namespace G

#endif  // _GAME_CLI_H
