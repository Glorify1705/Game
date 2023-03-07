cmake_minimum_required(VERSION 3.8...3.25)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

project(lua 
    VERSION 5.15 
    DESCRIPTION "CMake Lua Bundle"
    LANGUAGES C)

set(SRCS   
    src/lapi.c src/lcode.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c 
    src/lgc.c src/llex.c src/lmem.c src/lobject.c src/lopcodes.c src/lparser.c src/lstate.c 
    src/lstring.c src/ltable.c src/ltm.c src/lundump.c src/lvm.c src/lzio.c src/lauxlib.c 
    src/lbaselib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c 
    src/lstrlib.c src/linit.c src/lauxlib.c     
    src/lauxlib.c src/lbaselib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c
	src/lstrlib.c src/loadlib.c src/linit.c)

add_library(lua STATIC "${SRCS}")