#pragma once
#ifndef _GAME_GAME_H
#define _GAME_GAME_H

#include "array.h"
#include "libraries/sqlite3.h"

namespace G {

struct GameOptions {
  const char* source_directory = nullptr;
  bool hotreload = true;
  Slice<const char*> args;  // game arguments (after --)
};

// Runs the full engine with SDL, OpenGL, audio, etc.
// Takes ownership of the db handle and closes it on exit.
void RunGame(const GameOptions& opts, sqlite3* db);

}  // namespace G

#endif  // _GAME_GAME_H
