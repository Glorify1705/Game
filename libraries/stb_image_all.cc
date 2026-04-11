// Full stb_image implementation with all supported decoders enabled.
// The packer uses a separate PNG-only build (STBI_ONLY_PNG in packer.cc).
// The asset conversion tools need JPEG, BMP, GIF, and TGA as well.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
