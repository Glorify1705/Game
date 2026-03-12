#include <cstdio>

#include "cli.h"
#include "version.h"

namespace G {

int CmdVersion() {
  printf("game engine v%s (built %s)\n", GAME_VERSION_STR, __DATE__);
  return 0;
}

}  // namespace G
