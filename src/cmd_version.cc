#include <cstdio>

#include "cli.h"
#include "version.h"

namespace G {

int CmdVersion(const char* argv0) {
  printf("%s v%s (built %s)\n", argv0, GAME_VERSION_STR, __DATE__);
  return 0;
}

}  // namespace G
