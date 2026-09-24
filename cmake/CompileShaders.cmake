# vigide_compile_shaders(<target> <glsl files...>)
#
# Compiles each GLSL shader to SPIR-V and emits a header containing the
# words as a C array, so shaders ship inside the executable. The result is
# an INTERFACE library that adds the generated include directory; include
# shaders as e.g. `#include "shaders/ui.vert.h"`, which defines
# `ui_vert_spv[]` (uint32_t) for `ui.vert`.

find_program(VIGIDE_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
if(NOT VIGIDE_GLSLC AND TARGET Vulkan::glslc)
    get_target_property(VIGIDE_GLSLC Vulkan::glslc IMPORTED_LOCATION)
endif()
if(NOT VIGIDE_GLSLC)
    find_program(VIGIDE_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
endif()
if(NOT VIGIDE_GLSLC AND NOT VIGIDE_GLSLANG)
    message(FATAL_ERROR "No GLSL compiler found: install glslc or glslangValidator (both ship with the Vulkan SDK).")
endif()

function(vigide_compile_shaders target)
    set(out_root "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(headers)

    foreach(src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)
        string(MAKE_C_IDENTIFIER "${name}_spv" var)
        set(header "${out_root}/shaders/${name}.h")

        if(VIGIDE_GLSLC)
            set(spv "${out_root}/shaders/${name}.spv")
            add_custom_command(
                OUTPUT "${header}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_root}/shaders"
                COMMAND "${VIGIDE_GLSLC}" --target-env=vulkan1.0 -O
                        -o "${spv}" "${abs}"
                COMMAND "${CMAKE_COMMAND}" "-DSPV=${spv}" "-DHEADER=${header}" "-DVAR=${var}"
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedSpirv.cmake"
                DEPENDS "${abs}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedSpirv.cmake"
                COMMENT "Compiling shader ${name}"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT "${header}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_root}/shaders"
                COMMAND "${VIGIDE_GLSLANG}" -V --target-env vulkan1.0 --vn "${var}"
                        -o "${header}" "${abs}"
                DEPENDS "${abs}"
                COMMENT "Compiling shader ${name}"
                VERBATIM)
        endif()
        list(APPEND headers "${header}")
    endforeach()

    add_custom_target(${target}_compile DEPENDS ${headers})
    add_library(${target} INTERFACE)
    add_dependencies(${target} ${target}_compile)
    target_include_directories(${target} INTERFACE "${out_root}")
endfunction()
