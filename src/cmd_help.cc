#include <cstdio>
#include <string_view>

#include "cli.h"
#include "cli_metadata.h"
#include "stringlib.h"
#include "version.h"

namespace G {

namespace {

void PrintGeneralHelp(const char* prog) {
  printf("game engine v%s\n\nUsage: %s <command> [options]\n\nCommands:\n",
         GAME_VERSION_STR, prog);
  auto commands = GetCommands();

  // Compute column width from longest label.
  int max_width = 0;
  for (size_t i = 0; i < commands.size(); ++i) {
    int w = (int)commands[i].name.size();
    if (!commands[i].usage_suffix.empty()) {
      w += 1 + (int)commands[i].usage_suffix.size();
    }
    if (w > max_width) max_width = w;
  }
  int col = max_width + 3;  // padding between label and summary

  for (size_t i = 0; i < commands.size(); ++i) {
    const auto& cmd = commands[i];
    SmallBuffer label;
    label.Append(cmd.name);
    if (!cmd.usage_suffix.empty()) {
      label.Append(" ", cmd.usage_suffix);
    }
    printf("  %-*s%.*s\n", col, label.str(), (int)cmd.summary.size(),
           cmd.summary.data());
  }
  printf("\nRun '%s help <command>' for details on a specific command.\n",
         prog);
}

void PrintCommandHelp(const char* prog, const CliCommand* cmd) {
  printf("Usage: %s %.*s", prog, (int)cmd->name.size(), cmd->name.data());
  if (!cmd->usage_suffix.empty()) {
    printf(" %.*s", (int)cmd->usage_suffix.size(), cmd->usage_suffix.data());
  }
  printf("\n\n");
  printf("%.*s\n", (int)cmd->description.size(), cmd->description.data());

  if (!cmd->flags.empty()) {
    printf("\nOptions:\n");
    for (size_t i = 0; i < cmd->flags.size(); ++i) {
      const auto& flag = cmd->flags[i];
      SmallBuffer col;
      if (flag.short_name != '\0') {
        col.AppendF("-%c, --%.*s", flag.short_name, (int)flag.long_name.size(),
                    flag.long_name.data());
      } else {
        col.AppendF("    --%.*s", (int)flag.long_name.size(),
                    flag.long_name.data());
      }
      if (!flag.arg_name.empty()) {
        col.AppendF(" <%.*s>", (int)flag.arg_name.size(), flag.arg_name.data());
      }
      printf("  %-24s%.*s\n", col.str(), (int)flag.description.size(),
             flag.description.data());
    }
  }

  if (!cmd->extra_help.empty()) {
    printf("\n%.*s\n", (int)cmd->extra_help.size(), cmd->extra_help.data());
  }
}

}  // namespace

int CmdHelp(const char* argv0, const char* subcommand) {
  if (subcommand == nullptr) {
    PrintGeneralHelp(argv0);
    return 0;
  }

  if (auto* cmd = FindCommand(subcommand); cmd != nullptr) {
    PrintCommandHelp(argv0, cmd);
    return 0;
  }

  fprintf(stderr, "Unknown command: %s\n\n", subcommand);
  PrintGeneralHelp(argv0);
  return 1;
}

}  // namespace G
