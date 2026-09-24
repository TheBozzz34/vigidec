cmake_minimum_required(VERSION 3.21)

project(vigide
    VERSION 0.1.0
    DESCRIPTION "IDE for the VIG virtual machine"
    LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(VIGIDE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(VIGIDE_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(CompileShaders)
include(EmbedFiles)

# ---------------------------------------------------------------------------
# Shaders (GLSL -> SPIR-V, embedded into the executable)
# ---------------------------------------------------------------------------
vigide_compile_shaders(vigide_shaders
    shaders/ui.vert
    shaders/ui.frag)

# ---------------------------------------------------------------------------
# IDE executable
# ---------------------------------------------------------------------------
add_executable(vigide
    src/main.cpp
    src/app.cpp
    src/platform/window.cpp
    src/platform/file_dialogs.cpp
    src/render/vk_context.cpp
    src/render/ui_renderer.cpp
    src/render/font.cpp
    src/editor/text_buffer.cpp
    src/editor/highlight.cpp
    src/editor/text_editor.cpp
    src/editor/search.cpp
    src/ui/microui_icons.c
    src/ui/widgets.cpp
    src/ui/find_bar.cpp
    src/ui/ide.cpp)

vigide_embed_files(vigide
    vigide_font_mono assets/fonts/JetBrainsMono-Regular.ttf
    vigide_font_ui   assets/fonts/Inter-Regular.ttf)

target_include_directories(vigide PRIVATE src)
set_source_files_properties(src/ui/microui_icons.c PROPERTIES
    INCLUDE_DIRECTORIES "${VIGIDE_MICROUI_ATLAS_DIR}")
target_compile_features(vigide PRIVATE cxx_std_20 c_std_11)
set_target_properties(vigide PROPERTIES
    CXX_EXTENSIONS OFF
    C_EXTENSIONS OFF)

target_link_libraries(vigide PRIVATE
    vigide_shaders
    microui
    glfw
    freetype
    nfd
    Vulkan::Vulkan)

target_compile_definitions(vigide PRIVATE
    GLFW_INCLUDE_NONE
    VIGIDE_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug>:VIGIDE_VALIDATION=1>)

if(MSVC)
    target_compile_options(vigide PRIVATE /W4 /permissive-
        $<$<BOOL:${VIGIDE_WARNINGS_AS_ERRORS}>:/WX>)
    target_compile_definitions(vigide PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(vigide PRIVATE -Wall -Wextra -Wpedantic
        -Wno-missing-field-initializers  # Vulkan `Info info{sType}` idiom
        $<$<BOOL:${VIGIDE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# ---------------------------------------------------------------------------
# Tests (editor core only; no window or GPU required)
# ---------------------------------------------------------------------------
if(VIGIDE_BUILD_TESTS)
    enable_testing()
    add_executable(vigide_editor_tests
        tests/editor_tests.cpp
        src/editor/text_buffer.cpp
        src/editor/highlight.cpp
        src/editor/search.cpp)
    target_include_directories(vigide_editor_tests PRIVATE src)
    target_compile_features(vigide_editor_tests PRIVATE cxx_std_20)
    add_test(NAME editor COMMAND vigide_editor_tests)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vigide)

install(TARGETS vigide RUNTIME DESTINATION bin)
