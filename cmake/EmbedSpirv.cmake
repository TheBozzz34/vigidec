# Script mode helper: turns a SPIR-V binary into a C header.
#   cmake -DSPV=<in.spv> -DHEADER=<out.h> -DVAR=<identifier> -P EmbedSpirv.cmake

file(READ "${SPV}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR rem "${hex_len} % 8")
if(NOT rem EQUAL 0 OR hex_len EQUAL 0)
    message(FATAL_ERROR "${SPV} is not a valid SPIR-V binary")
endif()

# SPIR-V words are little-endian on disk; emit them as uint32_t literals.
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1," words "${hex}")
string(REGEX REPLACE "((0x........,){8})" "\\1\n    " words "${words}")

file(WRITE "${HEADER}"
"// Generated from ${SPV} - do not edit.\n#pragma once\n#include <stdint.h>\n\n"
"static const uint32_t ${VAR}[] = {\n    ${words}\n};\n")
