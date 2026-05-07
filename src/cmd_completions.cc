#include <cstdio>
#include <string_view>

#include "cli.h"
#include "cli_metadata.h"
#include "version.h"

namespace G {

namespace {

void GenerateBash() {
  auto commands = GetCommands();

  printf("# bash completion for game\n");
  printf("# eval \"$(game completions bash)\"\n");
  printf("_game() {\n");
  printf("  local cur prev words cword\n");
  printf("  _init_completion || return\n");
  printf("\n");
  printf("  if [[ $cword -eq 1 ]]; then\n");
  printf("    COMPREPLY=($(compgen -W '");
  for (size_t i = 0; i < commands.size(); ++i) {
    if (i > 0) printf(" ");
    printf("%.*s", (int)commands[i].name.size(), commands[i].name.data());
  }
  printf("' -- \"$cur\"))\n");
  printf("    return\n");
  printf("  fi\n");
  printf("\n");
  printf("  case \"${words[1]}\" in\n");
  for (size_t i = 0; i < commands.size(); ++i) {
    const auto& cmd = commands[i];
    if (cmd.flags.empty()) continue;
    printf("    %.*s)\n", (int)cmd.name.size(), cmd.name.data());
    printf("      COMPREPLY=($(compgen -W '");
    for (size_t j = 0; j < cmd.flags.size(); ++j) {
      const auto& flag = cmd.flags[j];
      if (j > 0) printf(" ");
      printf("--%.*s", (int)flag.long_name.size(), flag.long_name.data());
      if (flag.short_name != '\0') {
        printf(" -%c", flag.short_name);
      }
    }
    printf("' -- \"$cur\"))\n");
    printf("      ;;\n");
  }
  printf("    help)\n");
  printf("      COMPREPLY=($(compgen -W '");
  for (size_t i = 0; i < commands.size(); ++i) {
    if (i > 0) printf(" ");
    printf("%.*s", (int)commands[i].name.size(), commands[i].name.data());
  }
  printf("' -- \"$cur\"))\n");
  printf("      ;;\n");
  printf("    completions)\n");
  printf("      COMPREPLY=($(compgen -W 'bash zsh man' -- \"$cur\"))\n");
  printf("      ;;\n");
  printf("  esac\n");
  printf("}\n");
  printf("complete -F _game game\n");
}

void GenerateZsh() {
  auto commands = GetCommands();

  printf("#compdef game\n");
  printf("\n");
  printf("_game() {\n");
  printf("  local -a subcommands\n");
  printf("  subcommands=(\n");
  for (size_t i = 0; i < commands.size(); ++i) {
    const auto& cmd = commands[i];
    printf("    '%.*s:%.*s'\n", (int)cmd.name.size(), cmd.name.data(),
           (int)cmd.summary.size(), cmd.summary.data());
  }
  printf("  )\n");
  printf("\n");
  printf("  _arguments -C \\\n");
  printf("    '1:command:->command' \\\n");
  printf("    '*::arg:->args'\n");
  printf("\n");
  printf("  case $state in\n");
  printf("    command)\n");
  printf("      _describe 'command' subcommands\n");
  printf("      ;;\n");
  printf("    args)\n");
  printf("      case $words[1] in\n");

  for (size_t i = 0; i < commands.size(); ++i) {
    const auto& cmd = commands[i];
    if (cmd.flags.empty() && cmd.name != "help" && cmd.name != "completions") {
      continue;
    }

    printf("        %.*s)\n", (int)cmd.name.size(), cmd.name.data());
    printf("          _arguments \\\n");

    for (size_t j = 0; j < cmd.flags.size(); ++j) {
      const auto& flag = cmd.flags[j];
      if (flag.short_name != '\0') {
        printf("            {-%c,--%.*s}'[%.*s]", flag.short_name,
               (int)flag.long_name.size(), flag.long_name.data(),
               (int)flag.description.size(), flag.description.data());
        if (!flag.arg_name.empty()) {
          printf(":%.*s:", (int)flag.arg_name.size(), flag.arg_name.data());
        }
      } else {
        printf("            '--%.*s[%.*s]", (int)flag.long_name.size(),
               flag.long_name.data(), (int)flag.description.size(),
               flag.description.data());
        if (!flag.arg_name.empty()) {
          printf(":%.*s:", (int)flag.arg_name.size(), flag.arg_name.data());
        }
      }
      printf("' \\\n");
    }

    // Directory completion for commands that take a directory positional.
    if (cmd.name == "init" || cmd.name == "run" || cmd.name == "clean" ||
        cmd.name == "package" || cmd.name == "atlas") {
      printf("            '1:directory:_files -/' \\\n");
    } else if (cmd.name == "convert") {
      printf("            '1:input file:_files' \\\n");
    } else if (cmd.name == "help") {
      printf("            '1:command:(");
      for (size_t j = 0; j < commands.size(); ++j) {
        if (j > 0) printf(" ");
        printf("%.*s", (int)commands[j].name.size(), commands[j].name.data());
      }
      printf(")' \\\n");
    } else if (cmd.name == "completions") {
      printf("            '1:format:(bash zsh man)' \\\n");
    }

    printf("          ;;\n");
  }

  printf("      esac\n");
  printf("      ;;\n");
  printf("  esac\n");
  printf("}\n");
  printf("\n");
  printf("_game\n");
}

void GenerateMan() {
  auto commands = GetCommands();

  printf(".TH GAME 1\n");
  printf(".SH NAME\n");
  printf("game \\- 2D game engine CLI\n");
  printf(".SH SYNOPSIS\n");
  printf(".B game\n");
  printf(".I command\n");
  printf("[options]\n");
  printf(".SH DESCRIPTION\n");
  printf(".B game\n");
  printf(
      "is a CLI tool for creating, running, and packaging 2D games.\n"
      "Games are scripted in Lua 5.1 (with optional Fennel).\n"
      "The engine handles graphics (SDL2 + OpenGL), audio, physics,\n"
      "collision detection, input, and asset management.\n");

  printf(".SH COMMANDS\n");
  for (size_t i = 0; i < commands.size(); ++i) {
    const auto& cmd = commands[i];
    printf(".SS game %.*s", (int)cmd.name.size(), cmd.name.data());
    if (!cmd.usage_suffix.empty()) {
      printf(" %.*s", (int)cmd.usage_suffix.size(), cmd.usage_suffix.data());
    }
    printf("\n");
    printf("%.*s\n", (int)cmd.description.size(), cmd.description.data());

    if (!cmd.flags.empty()) {
      for (size_t j = 0; j < cmd.flags.size(); ++j) {
        const auto& flag = cmd.flags[j];
        printf(".TP\n");
        if (flag.short_name != '\0') {
          printf(".BR \\-%c \", \" \\-\\-%.*s", flag.short_name,
                 (int)flag.long_name.size(), flag.long_name.data());
        } else {
          printf(".B \\-\\-%.*s", (int)flag.long_name.size(),
                 flag.long_name.data());
        }
        if (!flag.arg_name.empty()) {
          printf(" \\fI%.*s\\fR", (int)flag.arg_name.size(),
                 flag.arg_name.data());
        }
        printf("\n");
        printf("%.*s\n", (int)flag.description.size(), flag.description.data());
      }
    }

    if (!cmd.extra_help.empty()) {
      printf(".PP\n");
      printf(".nf\n");
      printf("%.*s\n", (int)cmd.extra_help.size(), cmd.extra_help.data());
      printf(".fi\n");
    }
  }

  printf(".SH GLOBAL OPTIONS\n");
  printf(".TP\n");
  printf(".B \\-\\-help\n");
  printf("Show usage information.\n");
  printf(".TP\n");
  printf(".B \\-\\-version\n");
  printf("Print engine version.\n");

  printf(".SH FILES\n");
  printf(".TP\n");
  printf(".I conf.json\n");
  printf("Project configuration file (window size, app name, etc.).\n");
  printf(".TP\n");
  printf(".I ~/.cache/game/\n");
  printf("Cached asset databases for development builds.\n");

  printf(".SH VERSION\n");
  printf("Engine version %s\n", GAME_VERSION_STR);
}

}  // namespace

int CmdCompletions(Slice<const char*> args, Allocator* /*allocator*/) {
  if (args.size() < 2) {
    fprintf(stderr, "Usage: game completions {bash|zsh|man}\n");
    return 1;
  }

  std::string_view format = args[1];
  if (format == "bash") {
    GenerateBash();
  } else if (format == "zsh") {
    GenerateZsh();
  } else if (format == "man") {
    GenerateMan();
  } else {
    fprintf(stderr, "Unknown format '%.*s'. Use bash, zsh, or man.\n",
            (int)format.size(), format.data());
    return 1;
  }
  return 0;
}

}  // namespace G
