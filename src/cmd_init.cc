#include <cstdio>
#include <cstring>

#include "cli.h"
#include "stringlib.h"
#include "templates.h"

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace G {

namespace {

bool MakeDir(const char* path) {
#ifdef _WIN32
  return CreateDirectoryA(path, nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

bool FileExists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

bool WriteFile(const char* path, const char* contents) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return false;
  fputs(contents, f);
  fclose(f);
  return true;
}

bool WriteFileF(const char* path, const char* fmt, const char* arg1,
                const char* arg2) {
  FILE* f = fopen(path, "w");
  if (f == nullptr) return false;
  fprintf(f, fmt, arg1, arg2);
  fclose(f);
  return true;
}

}  // namespace

int CmdInit(int argc, const char* argv[]) {
  const char* dir = ".";
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] != '-') {
      dir = argv[i];
      break;
    }
  }

  // Extract project name from directory.
  std::string_view dir_sv(dir);
  std::string_view project_name = Basename(dir_sv);
  if (project_name.empty() || project_name == "." || project_name == "/") {
    // Use current working directory name.
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
      project_name = Basename(std::string_view(cwd));
    }
    if (project_name.empty()) project_name = "my-game";
  }

  // Create directory if needed.
  MakeDir(dir);

  // Check if project already exists.
  FixedStringBuffer<1024> conf_path(dir, "/conf.json");
  if (FileExists(conf_path.str())) {
    fprintf(stderr,
            "Error: project already exists in '%s' (conf.json found).\n", dir);
    return 1;
  }

  // Write scaffold files.
  // Need a null-terminated copy of project_name for printf.
  char name_buf[256];
  size_t name_len = project_name.size() < sizeof(name_buf) - 1
                        ? project_name.size()
                        : sizeof(name_buf) - 1;
  memcpy(name_buf, project_name.data(), name_len);
  name_buf[name_len] = '\0';

  if (!WriteFileF(conf_path.str(), templates::kConfJson, name_buf, name_buf)) {
    fprintf(stderr, "Error: could not write %s\n", conf_path.str());
    return 1;
  }

  FixedStringBuffer<1024> main_path(dir, "/main.lua");
  WriteFile(main_path.str(), templates::kMainLua);

  FixedStringBuffer<1024> game_path(dir, "/game.lua");
  WriteFile(game_path.str(), templates::kGameLua);

  FixedStringBuffer<1024> luarc_path(dir, "/.luarc.json");
  WriteFile(luarc_path.str(), templates::kLuarcJson);

  // Create definitions directory and generate stubs.
  FixedStringBuffer<1024> defs_dir(dir, "/definitions");
  MakeDir(defs_dir.str());

  FixedStringBuffer<1024> stubs_output(defs_dir.str(), "/game.lua");
  const char* stubs_argv[] = {"stubs", "--output", stubs_output.str()};
  CmdStubs(3, stubs_argv);

  printf("Created new game project in '%s'.\n", dir);
  printf("\n  cd %s && game run\n\n", dir);
  return 0;
}

}  // namespace G
