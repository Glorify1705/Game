cmake_minimum_required(VERSION 3.8...3.25)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

project(lua 
    VERSION 5.15 
    DESCRIPTION "CMake Lua Bundle"
    LANGUAGES C)

set(SUPPORTS_GCC_FLAGS
    $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>
)
set(GCC_AND_CLANG_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter
    -ggdb
    -DNDEBUG
    -DGAME_WITH_ASSERTS)
set(MSVC_FLAGS /W4 /WX /DGAME_WITH_ASSERTS /O2 /DEBUG /NDEBUG)

set(COMPILER_FLAGS $<${SUPPORTS_GCC_FLAGS}:${GCC_AND_CLANG_FLAGS}>
                   $<$<CXX_COMPILER_ID:MSVC>:${MSVC_FLAGS}>)

set(SRCS   
    src/lapi.c src/lcode.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c 
    src/lgc.c src/llex.c src/lmem.c src/lobject.c src/lopcodes.c src/lparser.c src/lstate.c 
    src/lstring.c src/ltable.c src/ltm.c src/lundump.c src/lvm.c src/lzio.c src/lauxlib.c 
    src/lbaselib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c 
    src/lstrlib.c src/linit.c src/lauxlib.c     
    src/lauxlib.c src/lbaselib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c
	src/lstrlib.c src/loadlib.c src/linit.c)

add_library(lua STATIC "${SRCS}")

target_compile_options(lua PRIVATE ${COMPILER_FLAGS})
