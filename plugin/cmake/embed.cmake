# Turns a file into a C++ header with an unsigned char array: cmake -DIN=... -DOUT=... -DNAME=... -P embed.cmake
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
string(REGEX REPLACE "((0x..,){40})" "\\1\n" hex "${hex}")
file(WRITE "${OUT}.tmp" "// Generated from ${IN}. Do not edit.\n#pragma once\nstatic const unsigned char ${NAME}[] = {\n${hex}\n};\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
