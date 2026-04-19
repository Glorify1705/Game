add_library(text_editor STATIC TextEditor.cpp)

target_include_directories(text_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(text_editor PRIVATE cxx_std_17)

target_compile_options(text_editor PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -fno-exceptions;-fno-rtti;-Wno-error
    >
)

target_link_libraries(text_editor PUBLIC imgui)
