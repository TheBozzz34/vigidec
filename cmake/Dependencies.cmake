# Third-party dependencies.
#
# Vulkan comes from the system (Vulkan SDK or distro packages). GLFW and
# microui are fetched at configure time and pinned to exact revisions.

include(FetchContent)

find_package(Vulkan REQUIRED)

# --- GLFW ------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
    SYSTEM)

# --- microui ---------------------------------------------------------------
# microui ships no build system; we only need the sources (plus the demo's
# font/icon atlas), so populate it and define the target ourselves.
FetchContent_Declare(microui
    GIT_REPOSITORY https://github.com/rxi/microui.git
    GIT_TAG        0850aba860959c3e75fb3e97120ca92957f9d057
    SOURCE_SUBDIR  do-not-add-subdirectory)

FetchContent_MakeAvailable(glfw microui)

add_library(microui STATIC ${microui_SOURCE_DIR}/src/microui.c)
target_include_directories(microui SYSTEM PUBLIC ${microui_SOURCE_DIR}/src)
set_target_properties(microui PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
if(MSVC)
    target_compile_definitions(microui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Location of the stock microui atlas (font glyphs + icons).
set(VIGIDE_MICROUI_ATLAS_DIR "${microui_SOURCE_DIR}/demo")
