#pragma once
#ifndef _GAME_GAME_H
#define _GAME_GAME_H

#include "array.h"
#include "libraries/sqlite3.h"

namespace G {

struct GameOptions {
  // Path to the game project directory, or nullptr for packaged mode.
  const char* source_directory = nullptr;
  // Native path of the blob source mounted read-only for asset loading: the
  // loose blob directory in dev mode, assets.zip in packaged mode.
  const char* blob_source = nullptr;
  // Whether to watch source files and hot-reload on changes.
  bool hotreload = true;
  // Run the game's _Game:test_inputs() coroutine instead of taking real input.
  // The engine exits with code 0 if the coroutine returns normally, 1 on
  // assertion failure or Lua error.
  bool test_mode = false;
  // Arguments forwarded to the game scripts (everything after '--').
  Slice<const char*> args;
  // All command-line arguments (for logging after SDL logger is set up).
  Slice<const char*> all_args;
};

// Runs the full engine with SDL, OpenGL, audio, etc.
// Takes ownership of the db handle and closes it on exit. Returns the test
// exit code in test mode, otherwise 0.
int RunGame(const GameOptions& opts, sqlite3* db);

}  // namespace G

#endif  // _GAME_GAME_H
