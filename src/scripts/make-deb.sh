#!/bin/bash
# Copyright (c) 2019-2020 The Hush developers
# Copyright 2020 Safecoin Developers
# Released under the GPLv3

DEBLOG=deb.log.$$

if [ -z $QT_STATIC ]; then
    echo "QT_STATIC is not set. Please set it to the base directory of a statically compiled Qt";
    exit 1;
fi

APP_VERSION=$(cat src/version.h | cut -d\" -f2)
if [ -z $APP_VERSION ]; then echo "APP_VERSION is not set"; exit 1; fi
#if [ -z $PREV_VERSION ]; then echo "PREV_VERSION is not set"; exit 1; fi

if [ -z $SAFECOIN_DIR ]; then
    echo "SAFECOIN_DIR is not set. Please set it to the base directory of safecoin.git"
    exit 1;
fi

if [ ! -f $SAFECOIN_DIR/artifacts/safecoind ]; then
    echo "Couldn't find safecoind in $SAFECOIN_DIR/artifacts/. Please build safecoind."
    exit 1;
fi

if [ ! -f $SAFECOIN_DIR/artifacts/safecoin-cli ]; then
    echo "Couldn't find safecoin-cli in $SAFECOIN_DIR/artifacts/. Please build safecoind."
    exit 1;
fi

echo -n "Cleaning..............."
rm -rf bin/*
rm -rf artifacts/*
make distclean >/dev/null 2>&1
echo "[OK]"

echo ""
echo "[Building $APP_VERSION on" `lsb_release -r`" logging to $DEBLOG ]"

echo -n "Translations............"
QT_STATIC=$QT_STATIC bash src/scripts/dotranslations.sh >/dev/null
echo -n "Configuring............"
$QT_STATIC/bin/qmake safe-qt-wallet.pro -spec linux-clang CONFIG+=release > /dev/null
echo "[OK]"


echo -n "Building..............."
rm -rf bin/safewallet* > /dev/null
make clean > /dev/null
./build.sh release > /dev/null
echo "[OK]"


# Test for Qt
echo -n "Static link............"
if [[ $(ldd safewallet | grep -i "Qt") ]]; then
    echo "FOUND QT; ABORT";
    exit 1
fi
echo "[OK]"


echo -n "Packaging.............."
APP=SafeWallet-v$APP_VERSION
DIR=bin/$APP
mkdir $DIR > /dev/null
strip safewallet

cp safewallet                  $DIR > /dev/null
cp $SAFECOIN_DIR/artifacts/safecoind    $DIR > /dev/null
cp $SAFECOIN_DIR/artifacts/safecoin-cli $DIR > /dev/null
cp $SAFECOIN_DIR/artifacts/safecoin-tx $DIR > /dev/null
cp README.md                      $DIR > /dev/null
cp LICENSE                        $DIR > /dev/null

cd bin && tar czf $APP.tar.gz $DIR/ > /dev/null
cd ..

mkdir artifacts >/dev/null 2>&1
cp $DIR.tar.gz ./artifacts/$APP-linux.tar.gz
echo "[OK]"


if [ -f artifacts/$APP-linux.tar.gz ] ; then
    echo -n "Package contents......."
    # Test if the package is built OK
    if tar tf "artifacts/$APP-linux.tar.gz" | wc -l | grep -q "9"; then
        echo "[OK]"
    else
        echo "[ERROR] Wrong number of files does not match 9"
        exit 1
    fi
else
    echo "[ERROR]"
    exit 1
fi

echo -n "Building deb..........."
debdir=bin/deb/safewallet-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat src/scripts/control | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp safewallet                   $debdir/usr/local/bin/
# TODO: how does this interact with safecoind deb ?
cp $SAFECOIN_DIR/artifacts/safecoind $debdir/usr/local/bin/safecoind

mkdir -p $debdir/usr/share/pixmaps/
cp res/safewallet.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp src/scripts/desktopentry    $debdir/usr/share/applications/safewallet.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb                 artifacts/$DIR.deb
echo "[OK]"

exit 0