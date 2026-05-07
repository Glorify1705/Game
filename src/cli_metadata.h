#pragma once
#ifndef _GAME_CLI_METADATA_H
#define _GAME_CLI_METADATA_H

#include <string_view>

#include "array.h"

namespace G {

// Describes a single CLI flag (e.g. --strip, -o/--output <dir>).
struct CliFlag {
  std::string_view long_name;  // e.g. "output" (without --)
  char short_name;             // e.g. 'o', or '\0' if none
  std::string_view arg_name;   // e.g. "dir" for value flags, "" for booleans
  std::string_view description;
};

// Describes a CLI subcommand.
struct CliCommand {
  std::string_view name;
  std::string_view summary;      // one-line for command listing
  std::string_view description;  // paragraph for detailed help
  std::string_view
      usage_suffix;  // part after "game <cmd>", e.g. "[dir] [-- args]"
  Slice<const CliFlag> flags;
  std::string_view extra_help;  // optional freeform text, "" if none
};

// Returns the static array of all CLI commands.
Slice<const CliCommand> GetCommands();

// Finds a command by name. Returns nullptr if not found.
const CliCommand* FindCommand(std::string_view name);

}  // namespace G

#endif  // _GAME_CLI_METADATA_H
