#!/usr/bin/env bash

set -euo pipefail

TARGET=${TARGET:-xtensa-softmmu}
VERSION=${VERSION:-dev}

echo DBG
./configure --help

# Building with -Werror only on Linux as that breaks some features detection in meson on macOS.
# Defining --bindir, --datadir, etc - to have the same directory tree on Linux and Windows
#   also adding --with-suffix="" to avoid doubled "qemu/qemu" path.
#
#   MinGW build ref: https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-qemu/PKGBUILD

./configure \
    --bindir=xtensa-softmmu \
    --datadir=share/qemu \
    --enable-gcrypt \
    --disable-sdl \
    --enable-slirp \
    --disable-opengl \
    --enable-vte \
    --enable-gtk \
    --enable-strip \
    --enable-pixman \
    --enable-stack-protector \
    --extra-cflags=-Werror \
    --prefix=${PWD}/install/qemu \
    --target-list=${TARGET} \
    --with-pkgversion="${VERSION}" \
    --with-suffix="" \
    --without-default-features \
|| { cat meson-logs/meson-log.txt && false; }
