cmake_minimum_required(VERSION 3.8...3.26)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

project(GameTests 
    VERSION 0.1 
    DESCRIPTION "Tests for Game Code"
    LANGUAGES CXX)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(googletest)

enable_testing()

set(GAME_SRCS
    "${CMAKE_SOURCE_DIR}/src/allocators.cc"
    "${CMAKE_SOURCE_DIR}/src/strings.cc"
    "${CMAKE_SOURCE_DIR}/src/stats.cc"
    "${CMAKE_SOURCE_DIR}/src/logging.cc"
    )

add_executable(
  GameTests
  test.cc
  "${GAME_SRCS}"
)

set(SUPPORTS_GCC_FLAGS $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>)
set(GCC_AND_CLANG_FLAGS -Wall -Wextra -Werror -ggdb -O2 -DNDEBUG -DGAME_WITH_ASSERTS)
set(MSVC_FLAGS /W4 /WX /DGAME_WITH_ASSERTS /O2 /DEBUG /NDEBUG)

set(COMPILER_FLAGS 
    $<${SUPPORTS_GCC_FLAGS}:${GCC_AND_CLANG_FLAGS}> $<$<CXX_COMPILER_ID:MSVC>:${MSVC_FLAGS}>)

target_compile_options(GameTests PRIVATE ${COMPILER_FLAGS})

target_include_directories(GameTests
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}" 
                           "${CMAKE_SOURCE_DIR}/src"
                           "${CMAKE_SOURCE_DIR}/libraries/lua/src")

target_link_libraries(
  GameTests
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(GameTests)