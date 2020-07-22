#!/bin/bash
# Copyright 2019-2020 The Hush Developers
# Copyright 2020 Safecoin Developers
# Released under the GPLv3
# This script will cross-compile windoze binaries, hopefully!

set -e
UNAME=$(uname)

if [ "$UNAME" == "Linux" ] ; then
    JOBS=$(nproc)
elif [ "$UNAME" == "FreeBSD" ] ; then
    JOBS=$(nproc)
elif [ "$UNAME" == "Darwin" ] ; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=1
fi

VERSION=$(cat src/version.h |cut -d\" -f2)
echo "Compiling SafeWallet $VERSION with $JOBS threads..."
CONF=safe-qt-wallet.pro

qbuild () {
   x86_64-w64-mingw32.static-qmake-qt5 $CONF CONFIG+=debug
   make -j$JOBS
}

qbuild_release () {
   # This binary must be in your PATH!
   x86_64-w64-mingw32.static-qmake-qt5 $CONF CONFIG+=release
   make -j$JOBS
}

if [ "$1" == "clean" ]; then
   make clean
elif [ "$1" == "linguist" ]; then
    lupdate $CONF
    lrelease $CONF
elif [ "$1" == "cleanbuild" ]; then
   make clean
   qbuild
elif [ "$1" == "release" ]; then
   qbuild_release
else
   qbuild
fi