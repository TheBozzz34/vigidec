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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(CompileShaders)

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
    src/render/vk_context.cpp
    src/render/ui_renderer.cpp
    src/ui/atlas.c
    src/ui/ide.cpp)

target_include_directories(vigide PRIVATE src)
set_source_files_properties(src/ui/atlas.c PROPERTIES
    INCLUDE_DIRECTORIES "${VIGIDE_MICROUI_ATLAS_DIR}")
target_compile_features(vigide PRIVATE cxx_std_20 c_std_11)
set_target_properties(vigide PROPERTIES
    CXX_EXTENSIONS OFF
    C_EXTENSIONS OFF)

target_link_libraries(vigide PRIVATE
    vigide_shaders
    microui
    glfw
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

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vigide)

install(TARGETS vigide RUNTIME DESTINATION bin)
