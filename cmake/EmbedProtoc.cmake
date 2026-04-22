# Generates a C header embedding protoc.lua as a string constant.
file(READ "${INPUT}" PROTOC_CONTENTS)
string(REPLACE "\\" "\\\\" PROTOC_CONTENTS "${PROTOC_CONTENTS}")
string(REPLACE "\"" "\\\"" PROTOC_CONTENTS "${PROTOC_CONTENTS}")
string(REPLACE "\n" "\\n\"\n\"" PROTOC_CONTENTS "${PROTOC_CONTENTS}")
file(WRITE "${OUTPUT}"
"// Auto-generated from libraries/lua-protobuf/protoc.lua. Do not edit.\n"
"#pragma once\n"
"static const char kProtocLua[] =\n"
"\"${PROTOC_CONTENTS}\";\n"
"static const unsigned int kProtocLuaLen = sizeof(kProtocLua) - 1;\n"
)
