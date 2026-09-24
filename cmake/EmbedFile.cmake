# Script mode helper: turns any file into a C source defining a byte array.
#   cmake -DINPUT=<file> -DOUTPUT=<out.c> -DVAR=<identifier> -P EmbedFile.cmake

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")

string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n    " bytes "${bytes}")

file(WRITE "${OUTPUT}"
"/* Generated from ${INPUT} - do not edit. */\n#include <stddef.h>\n\n"
"const unsigned char ${VAR}[] = {\n    ${bytes}\n};\n"
"const size_t ${VAR}_size = ${size};\n")
