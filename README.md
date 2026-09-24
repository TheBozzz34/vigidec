# vigide

A native IDE for the [VIG virtual machine](https://github.com/TheBozzz34/vig-meta),
written in C++20 with a Vulkan renderer, a [microui](https://github.com/rxi/microui)
immediate-mode UI and [FreeType](https://freetype.org/) text rendering.

## Requirements

- CMake 3.21+ and a C/C++20 compiler (GCC, Clang or MSVC)
- Vulkan headers + loader and a GLSL compiler (`glslc` or `glslangValidator`).
  The [LunarG Vulkan SDK](https://vulkan.lunarg.com/) provides all of these on
  Windows, Linux and macOS.
- Git (GLFW, FreeType and microui are fetched at configure time)

On Debian/Ubuntu:

```sh
sudo apt install cmake ninja-build libvulkan-dev glslc vulkan-validationlayers \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev
```

## Build and run

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/vigide
```

Use the `release` preset for an optimised build. Debug builds enable the
Khronos validation layer when it is installed.

Run the editor unit tests (no GPU needed) with `ctest --preset debug`.

## Editor

| Keys | Action |
| --- | --- |
| Arrows, Home/End, PgUp/PgDn | Move (Home toggles between indentation and column 1) |
| Ctrl+Left/Right, Ctrl+Home/End | Word / document movement |
| Shift + any movement, mouse drag | Extend selection |
| Double / triple click, gutter click | Select word / line |
| Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V | Select all, copy, cut, paste (whole line when nothing is selected) |
| Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z | Undo, redo |
| Tab / Shift+Tab | Indent / unindent (selected lines) |
| Ctrl+D | Duplicate line or selection |
| Ctrl+Backspace / Ctrl+Delete | Delete word |
| Ctrl+S | Save |

On macOS, Cmd replaces Ctrl. Syntax highlighting covers VIG assembly
(`.vigas`) and C (`.c`/`.h`, for vigcc).

## Layout

```
CMakeLists.txt            top-level build
cmake/Dependencies.cmake  Vulkan (system); GLFW, FreeType, microui (FetchContent, pinned)
cmake/CompileShaders.cmake  GLSL -> SPIR-V -> embedded C arrays
cmake/EmbedFiles.cmake    binary assets (fonts) -> embedded C arrays
assets/fonts/             JetBrains Mono (code) and Inter (UI), SIL OFL 1.1
shaders/                  UI vertex/fragment shaders
src/main.cpp, app.cpp     entry point and main loop
src/platform/             GLFW window; input for microui and the editor
src/render/vk_context.*   instance, device, swapchain, frame sync
src/render/font.*         FreeType fonts, glyph atlas (grows on demand)
src/render/ui_renderer.*  microui command list -> Vulkan draws
src/editor/               text buffer, syntax highlighting, editor widget
src/ui/ide.*              IDE panels (explorer, editor, VM, output)
tests/                    editor core unit tests
```

## Status

The window, renderer, fonts, panel layout and code editor are in place.
The build/run toolbar actions and the VM panel are placeholders until the VM
and assembler are wired in.
