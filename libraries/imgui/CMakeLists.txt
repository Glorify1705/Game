# Dear ImGui v1.91.8 — vendored static library for the debug UI overlay.
# Built only when ENABLE_IMGUI is ON (default for dev builds).
add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_widgets.cpp
    imgui_tables.cpp
    backends/imgui_impl_sdl3.cpp
    backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/backends"
)

target_compile_features(imgui PRIVATE cxx_std_17)

# ImGui does not use RTTI or exceptions. Suppress warnings from vendored code.
target_compile_options(imgui PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -fno-exceptions;-fno-rtti;-Wno-error
    >
)

# The SDL3 backend needs SDL3 headers.
target_link_libraries(imgui PUBLIC SDL3::Headers)

# OpenGL backend needs GL headers (provided transitively by the engine, but
# declare it explicitly so imgui can compile standalone).
if(NOT WIN32)
    find_package(OpenGL QUIET)
    if(TARGET OpenGL::GL)
        target_link_libraries(imgui PRIVATE OpenGL::GL)
    endif()
endif()
