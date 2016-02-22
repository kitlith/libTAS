#!/bin/sh

LIB_SOURCE_FILES="libTAS.c keyboard.c hook.c logging.c openal.c threads.c ../shared/tasflags.c"
C_WARNINGS="-std=c99 -Wall -Wextra -Wmissing-include-dirs -Wmissing-declarations -Wfloat-equal -Wundef -Wcast-align -Wredundant-decls -Winit-self -Wshadow  -shared -fpic"
C_OPTIMISATIONS="-g -O1 -lX11"

[ -d ../../bin ] || mkdir ../../bin
gcc $LIB_SOURCE_FILES $C_WARNINGS $C_OPTIMISATIONS -o ../../bin/libTAS.so
