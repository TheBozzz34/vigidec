# vigide

A native IDE for the [VIG virtual machine](https://github.com/TheBozzz34/vig-meta),
written in C++20 with a Vulkan renderer and a [microui](https://github.com/rxi/microui)
immediate-mode UI.

## Requirements

- CMake 3.21+ and a C/C++20 compiler (GCC, Clang or MSVC)
- Vulkan headers + loader and a GLSL compiler (`glslc` or `glslangValidator`).
  The [LunarG Vulkan SDK](https://vulkan.lunarg.com/) provides all of these on
  Windows, Linux and macOS.
- Git (GLFW and microui are fetched at configure time)

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

## Layout

```
CMakeLists.txt            top-level build
cmake/Dependencies.cmake  Vulkan (system), GLFW + microui (FetchContent, pinned)
cmake/CompileShaders.cmake  GLSL -> SPIR-V -> embedded C arrays
shaders/                  UI vertex/fragment shaders
src/main.cpp, app.cpp     entry point and main loop
src/platform/window.*     GLFW window, input forwarded to microui
src/render/vk_context.*   instance, device, swapchain, frame sync
src/render/ui_renderer.*  microui command list -> Vulkan draws
src/ui/atlas.*            microui's built-in font/icon atlas
src/ui/ide.*              IDE panels (explorer, editor, VM, output)
```

## Status

The window, renderer and panel layout are in place. The editor is a
read-only viewer and the toolbar/VM panel are placeholders until the VM and
assembler are wired in.
