safewallet desktop wallet for Safecoin ($SAFE) that runs on Linux, Windows and macOS.


![Screenshots](safewallet.png?raw=true)
# Installation

Head over to the releases page and grab the latest installers or binary. https://github.com/Fair-Exchange/safewallet/releases

## safecoind
safewallet needs a Safecoin full node running safecoind. If you already have a safecoind node running, safewallet will connect to it. 

If you don't have one, safewallet will start its embedded safecoind node. 

Additionally, if this is the first time you're running safewallet or a safecoind daemon, safewallet will download the zcash params (~1.7 GB) and configure `safecoin.conf` for you. 

Pass `--no-embedded` to disable the embedded safecoind and force safewallet to connect to an external node.

## Compiling from source
safewallet is written in C++ 14, and can be compiled with g++/clang++/visual c++. It also depends on Qt5, which you can get from [here](https://www.qt.io/download). Note that if you are compiling from source, you won't get the embedded safecoind by default. You can either run an external safecoind, or compile safecoind as well. 


### Building on Linux

#### Ubuntu 18.04:

```

sudo apt-get install qt5-default qt5-qmake libqt5websockets5-dev qtcreator
git clone https://github.com/Fair-Exchange/safewallet.git
cd safewallet
qmake safe-qt-wallet.pro CONFIG+=debug
make -j$(nproc)

./safewallet
```

#### Arch Linux:

```
sudo pacman -S qt5-base qt5-tools qtcreator qt5-websockets rust
git clone https://github.com/Fair-Exchange/safewallet.git
cd safewallet
./build.sh linguist
./build.sh release
./safewallet
```

### Building on Windows
You need Visual Studio 2017 (The free C++ Community Edition works just fine). 

From the VS Tools command prompt
```
git clone  https://github.com/Fair-Exchange/safewallet.git
cd safewallet
c:\Qt5\bin\qmake.exe safe-qt-wallet.pro -spec win32-msvc CONFIG+=debug
nmake

debug\safewallet.exe
```

To create the Visual Studio project files so you can compile and run from Visual Studio:
```
c:\Qt5\bin\qmake.exe safe-qt-wallet.pro -tp vc CONFIG+=debug
```

### Building on macOS

You need to install the Xcode app or the Xcode command line tools first, and then install Qt. 


```
git clone https://github.com/Fair-Exchange/safewallet.git
cd safewallet
qmake safe-qt-wallet.pro CONFIG+=debug
make

./safewallet.app/Contents/MacOS/safewallet
```

For a more indepth build guide please read our mac build guide [here](docs/build-mac.md)
### Emulating the embedded node

In binary releases, safewallet will use node binaries in the current directory to sync a node from scratch.
It does not attempt to download them, it bundles them. To simulate this from a developer setup, you can symlink
these four files in your Git repo:

```
    ln -s ../safecoin/src/safecoind
    ln -s ../safecoin/src/safecoin-cli
```

The above assumes safewallet and safecoin git repos are in the same directory. File names on Windows will need to be tweaked.

### Support

For support or other questions, Join [Discord](https://discordapp.com/invite/vQgYGJz), or tweet at [@safecoins](https://twitter.com/safecoins) or [file an issue](https://github.com/Fair-Exchange/safewallet/issues).
