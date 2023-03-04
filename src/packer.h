#pragma once
#ifndef _GAME_PACKER_H
#define _GAME_PACKER_H

#include <vector>

namespace G {

void PackerMain(const char* output_file, const std::vector<const char*> paths);

}  // namespace G

#endif  // _GAME_PACKER_H