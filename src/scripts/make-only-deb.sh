#!/bin/bash
# Copyright (c) 2019-2020 The Hush developers
# Copyright 2020 Safecoin Developers
# Released under the GPLv3

echo "Let There Be Debian Packages"
set -e
#set -x

ARCH=$(uname -i)
APP_VERSION=$(cat src/version.h | cut -d\" -f2)
if [ -z $APP_VERSION ]; then echo "APP_VERSION is not set"; exit 1; fi

# This assumes we already have a staticly compiled SD

echo "Building Debian package for $APP_VERSION-$ARCH..."
debdir=deb-SafecoinWallet-v$APP_VERSION-$ARCH
if [ -e $debdir ]; then
    mv $debdir $debdir-backup.$(perl -e 'print time')
fi
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat src/scripts/control | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

# might have been done already, but just in case
strip safecoinwallet
cp safecoinwallet                   $debdir/usr/local/bin/

# TODO: We need an updated pixmap!
#mkdir -p $debdir/usr/share/pixmaps/
#cp res/safecoinwallet.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp src/scripts/desktopentry    $debdir/usr/share/applications/safecoinwallet.desktop

dpkg-deb --build $debdir >/dev/null
sha256sum  $debdir.deb

exit 0