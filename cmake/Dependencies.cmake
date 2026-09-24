# Third-party dependencies.
#
# Vulkan comes from the system (Vulkan SDK or distro packages). GLFW,
# FreeType, nativefiledialog-extended and microui are fetched at configure
# time and pinned to exact revisions.

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

# --- FreeType --------------------------------------------------------------
# Only the core rasteriser is needed: fonts are embedded TTFs, so skip the
# optional compression/PNG/shaping dependencies for a self-contained build.
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON CACHE BOOL "" FORCE)

FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
    SYSTEM)

# --- nativefiledialog-extended ---------------------------------------------
# Native Open/Save dialogs. On Linux this uses GTK 3 by default; set
# VIGIDE_NFD_PORTAL=ON to use xdg-desktop-portal instead (needs only
# libdbus-1 at build time, but a portal service at run time).
option(VIGIDE_NFD_PORTAL "Linux: use xdg-desktop-portal instead of GTK for file dialogs" OFF)
set(NFD_PORTAL       ${VIGIDE_NFD_PORTAL} CACHE BOOL "" FORCE)
set(NFD_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL      OFF CACHE BOOL "" FORCE)

# nfd's only submodule is all of wayland-protocols (from freedesktop's
# GitLab), used for a single XML file. Skip it and supply that file from
# third_party/ instead; nfd is added below once the file is in place.
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.4.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  do-not-add-subdirectory)

# --- microui ---------------------------------------------------------------
# microui ships no build system; we only need the sources (plus the demo's
# font/icon atlas), so populate it and define the target ourselves.
FetchContent_Declare(microui
    GIT_REPOSITORY https://github.com/rxi/microui.git
    GIT_TAG        0850aba860959c3e75fb3e97120ca92957f9d057
    SOURCE_SUBDIR  do-not-add-subdirectory)

FetchContent_MakeAvailable(glfw freetype nfd microui)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/../third_party/wayland-protocols/xdg-foreign-unstable-v1.xml"
     DESTINATION "${nfd_SOURCE_DIR}/3ps/wayland-protocols/unstable/xdg-foreign")
add_subdirectory("${nfd_SOURCE_DIR}" "${nfd_BINARY_DIR}" SYSTEM)

add_library(microui STATIC ${microui_SOURCE_DIR}/src/microui.c)
target_include_directories(microui SYSTEM PUBLIC ${microui_SOURCE_DIR}/src)
set_target_properties(microui PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
if(MSVC)
    target_compile_definitions(microui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Location of the stock microui atlas (font glyphs + icons).
set(VIGIDE_MICROUI_ATLAS_DIR "${microui_SOURCE_DIR}/demo")
