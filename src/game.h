#pragma once
#ifndef _GAME_GAME_H
#define _GAME_GAME_H

#include "array.h"
#include "libraries/sqlite3.h"

namespace G {

struct GameOptions {
  // Path to the game project directory, or nullptr for packaged mode.
  const char* source_directory = nullptr;
  // Whether to watch source files and hot-reload on changes.
  bool hotreload = true;
  // Arguments forwarded to the game scripts (everything after '--').
  Slice<const char*> args;
};

// Runs the full engine with SDL, OpenGL, audio, etc.
// Takes ownership of the db handle and closes it on exit.
void RunGame(const GameOptions& opts, sqlite3* db);

}  // namespace G

#endif  // _GAME_GAME_H
