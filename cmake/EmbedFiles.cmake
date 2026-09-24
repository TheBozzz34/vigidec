# vigide_embed_files(<target> <VAR> <file> [<VAR> <file>...])
#
# Generates a C source per file defining `const unsigned char VAR[]` and
# `const size_t VAR_size`, and adds them to <target>. Declare the symbols
# yourself (see src/assets.h).

function(vigide_embed_files target)
    set(out_root "${CMAKE_CURRENT_BINARY_DIR}/generated/embed")
    set(args ${ARGN})
    list(LENGTH args n)
    math(EXPR odd "${n} % 2")
    if(odd)
        message(FATAL_ERROR "vigide_embed_files expects VAR/file pairs")
    endif()

    while(args)
        list(POP_FRONT args var file)
        get_filename_component(abs "${file}" ABSOLUTE)
        set(out "${out_root}/${var}.c")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_root}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${abs}" "-DOUTPUT=${out}" "-DVAR=${var}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedFile.cmake"
            DEPENDS "${abs}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedFile.cmake"
            COMMENT "Embedding ${file}"
            VERBATIM)
        target_sources(${target} PRIVATE "${out}")
    endwhile()
endfunction()
