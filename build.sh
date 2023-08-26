#!/bin/sh

set -ue

zig build-obj -fsingle-threaded -lc -OReleaseFast -fstrip loadbsp.zig

gcc -Wall -Wextra -Wpedantic -std=c11 -Wno-unused-parameter \
    $(pkg-config --cflags --libs glfw3 glew cglm) -lm \
    loadbsp.o main.c -Ofast -flto

rm *.o
