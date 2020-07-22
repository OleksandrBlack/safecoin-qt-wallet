#!/bin/bash
if [ -z $QT_STATIC ]; then 
    echo "QT_STATIC is not set. Please set it to the base directory of a statically compiled Qt"; 
    exit 1; 
fi

if [ -z $APP_VERSION ]; then echo "APP_VERSION is not set"; exit 1; fi
if [ -z $PREV_VERSION ]; then echo "PREV_VERSION is not set"; exit 1; fi

if [ -z $SAFECOIN_DIR ]; then
    echo "SAFECOIN_DIR is not set. Please set it to the base directory of a Safecoin project with built Safecoin binaries."
    exit 1;
fi

if [ ! -f $SAFECOIN_DIR/safecoind ]; then
    echo "Couldn't find safecoind in $SAFECOIN_DIR/. Please build safecoind."
    exit 1;
fi

if [ ! -f $SAFECOIN_DIR/safecoin-cli ]; then
    echo "Couldn't find safecoin-cli in $SAFECOIN_DIR/. Please build safecoind."
    exit 1;
fi


echo -n "Version files.........."
# Replace the version number in the .pro file so it gets picked up everywhere
sed -i "s/${PREV_VERSION}/${APP_VERSION}/g" safe-qt-wallet.pro > /dev/null

# Also update it in the README.md
sed -i "s/${PREV_VERSION}/${APP_VERSION}/g" README.md > /dev/null
echo "[OK]"

echo -n "Cleaning1..............."
rm -rf bin/*
rm -rf artifacts/*
rm -rf release/safewallet-v$APP_VERSION
make distclean >/dev/null 2>&1
echo "[OK]"

echo ""
echo "-----------------------------------------------------"
echo "[Windows]"
echo "-----------------------------------------------------"
echo ""

if [ -z $MXE_PATH ]; then 
    echo "MXE_PATH is not set. Set it to ~/github/mxe/usr/bin if you want to build Windows"
    echo "Not building Windows"
    exit 0; 
fi

if [ ! -f $SAFECOIN_DIR/safecoind.exe ]; then
    echo "Couldn't find safecoind.exe in $SAFECOIN_DIR/. Please build safecoind.exe"
    exit 1;
fi


if [ ! -f $SAFECOIN_DIR/safecoin-cli.exe ]; then
    echo "Couldn't find safecoin-cli.exe in $SAFECOIN_DIR/. Please build safecoind.exe"
    exit 1;
fi

export PATH=$MXE_PATH:$PATH

echo -n "Building..............."
./win-build.sh release > /dev/null
echo "[OK]"


echo -n "Packaging.............."
mkdir release/safewallet-v$APP_VERSION  
cp release/safewallet.exe			release/safewallet-v$APP_VERSION 
cp $SAFECOIN_DIR/safecoind.exe			release/safewallet-v$APP_VERSION > /dev/null
cp $SAFECOIN_DIR/safecoin-cli.exe		release/safewallet-v$APP_VERSION > /dev/null
cp README.md							release/safewallet-v$APP_VERSION 
cp LICENSE								release/safewallet-v$APP_VERSION 
cd release && zip -r Windows-binaries-safewallet-v$APP_VERSION.zip safewallet-v$APP_VERSION/ > /dev/null
cd ..

mkdir artifacts >/dev/null 2>&1
cp release/Windows-binaries-safewallet-v$APP_VERSION.zip ./artifacts/
echo "[OK]"

if [ -f artifacts/Windows-binaries-safewallet-v$APP_VERSION.zip ] ; then
    echo -n "Package contents......."
    if unzip -l "artifacts/Windows-binaries-safewallet-v$APP_VERSION.zip" | wc -l | grep -q "11"; then 
        echo "[OK]"
    else
        echo "[ERROR]"
        exit 1
    fi
else
    echo "[ERROR]"
    exit 1
fi

echo -n "Cleaning2..............."
rm -rf bin/*
make distclean >/dev/null 2>&1
echo "[OK]"

echo ""
echo "-----------------------------------------------------"
echo "[Building on" `lsb_release -r`"]"
echo "-----------------------------------------------------"
echo ""

echo -n "Configuring............"
#TODO
#QT_STATIC=$QT_STATIC bash src/scripts/dotranslations.sh >/dev/null
$QT_STATIC/bin/qmake safe-qt-wallet.pro -spec linux-clang CONFIG+=release > /dev/null
echo "[OK]"


echo -n "Building..............."
rm -rf bin/safe-qt-wallet* > /dev/null
rm -rf bin/safewallet* > /dev/null
make clean > /dev/null
make -j$(nproc) > /dev/null
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
mkdir bin/safewallet-v$APP_VERSION > /dev/null
strip safewallet

cp safewallet                  bin/safewallet-v$APP_VERSION > /dev/null
cp $SAFECOIN_DIR/safecoind    bin/safewallet-v$APP_VERSION > /dev/null
cp $SAFECOIN_DIR/safecoin-cli bin/safewallet-v$APP_VERSION > /dev/null
cp README.md                      bin/safewallet-v$APP_VERSION > /dev/null
cp LICENSE                        bin/safewallet-v$APP_VERSION > /dev/null

cd bin && tar czf linux-safewallet-v$APP_VERSION.tar.gz safewallet-v$APP_VERSION/ > /dev/null
cd .. 

mkdir artifacts >/dev/null 2>&1
cp bin/linux-safewallet-v$APP_VERSION.tar.gz ./artifacts/linux-binaries-safewallet-v$APP_VERSION.tar.gz
echo "[OK]"


if [ -f artifacts/linux-binaries-safewallet-v$APP_VERSION.tar.gz ] ; then
    echo -n "Package contents......."
    # Test if the package is built OK
    if tar tf "artifacts/linux-binaries-safewallet-v$APP_VERSION.tar.gz" | wc -l | grep -q "6"; then 
        echo "[OK]"
    else
        echo "[ERROR]"
        exit 1
    fi    
else
    echo "[ERROR]"
    exit 1
fi

echo ""
echo "-----------------------------------------------------"
echo "[Building deb...........]"
echo "-----------------------------------------------------"
echo ""

debdir=bin/deb/safewallet-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat src/scripts/control | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp safewallet					$debdir/usr/local/bin/
cp $SAFECOIN_DIR/safecoind			$debdir/usr/local/bin/safecoind
cp $SAFECOIN_DIR/safecoin-cli		$debdir/usr/local/bin/safecoin-cli

mkdir -p $debdir/usr/share/pixmaps/
cp res/safewallet.xpm			$debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp src/scripts/desktopentry			$debdir/usr/share/applications/safewallet.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb						artifacts/linux-deb-safewallet-v$APP_VERSION.deb
echo "[OK]"

echo ""
echo "-----------------------------------------------------"
echo "DONE! Checksums:"
echo "-----------------------------------------------------"
echo ""
cd artifacts
sha256sum *
echo "Save to checksums.txt"
sha256sum * > checksums.txt
echo "[OK]"
