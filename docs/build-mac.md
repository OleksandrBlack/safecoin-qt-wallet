# Build instruction for the safecoinwallet on macOS.

this guide has been made for macOS 10.14 and 10.15

## Setting up the build environment

Before compiling you will need to configure your build environmet.

### Xcode

The first step to setting up your mac build environment is installing [Xcode](https://developer.apple.com/xcode/).
After Xcode has been installed download and install the Xcode command line tools [here](https://developer.apple.com/download/more/?=command%20line%20tools). or enter this command in the terminal: 
```
xcode-select --install
```
You will get a popup asking you to install the Xcode command line tools simply click `install` to intsall them.

### Homebrew

Next up is installing home brew, this can be done with the following command:

```
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
```

after Homebrew has been installed you will need to download the following dependencies with this command:

```
brew install autoconf automake berkeley-db4 libtool boost miniupnpc openssl pkg-config protobuf libevent qt gcc@8 binutils coreutils coreutils wget
```

### Configuring your PATH
Now that we have installed all the needed dependencies to compile the safewallet you will need to configure your PATH.

To configure your path you will need to edit the following file `/etc/paths`. you will need to add qmake to your PATH. 
This can be done with the following command:

```
sudo nano /etc/paths
```
Then you will need to add the path to qmake. By default this is `/usr/local/opt/qt/bin` and `/usr/local/opt/qt5/bin`
After adding this to your path the file should look something like this: 

```
/usr/local/bin
/usr/bin
/bin
/usr/sbin
/sbin
/usr/local/opt/qt5/bin
/usr/local/opt/qt/bin
```
Now simply save and exit the file this can be done by pressing `^X` then press `Y` and then `return` 

After following these steps your build environment should be ready to compile safecoind and the safecoinwallet just restart your terminal for the changes to take effect.


## Compiling safecoind and safecoin-cli
safecoinwallet needs a Safecoin full node running safecoind and safecoin-cli. 

To compile safecoind and safecoin-cli simply open the terminal and enter the following commands:

```
git clone https://github.com/Fair-Exchange/safecoin --branch master --single-branch
cd safecoin
# if you have already have the zcash params installed you won't need to execute the next command
./zcutil/fetch-params.sh
# now build safecoind and safecoin-cli
./zcutil/build-mac.sh -j$(expr $(sysctl -n hw.ncpu) - 1)
# note that this can take some time
```
After the compile is done you can find `safecoind` and `safecoin-cli` in `/src` you will need these two files when we are done compiling the safewallet.



## Compiling safewallet from source
safecoinwallet is written in C++ 14, and can be compiled with g++/clang++/visual c++. It also depends on Qt5, which you can get from [here](https://www.qt.io/download). Note that if you are compiling from source, you won't get the embedded safecoind and safecoin-cli by default. We will add these later.


### Building safewallet
To compile the safewallet for macOS simply execute the follwing commands: 

```
git clone https://github.com/Fair-Exchange/safewallet.git
cd safewallet
qmake safe-qt-wallet.pro CONFIG+=debug
make -j$(expr $(sysctl -n hw.ncpu) - 1)
```

### Embedding safecoind and safecoin-cli

To embed the safecoind and safecoin-cli you will need to copy the `safecoind` and `safecoin-cli` these can be found in `~/safecoin/src`
After copying these files go to `~/safewallet` and right click on `safecoinwallet.app` now press on `Show Package Contents` and go to `/Contents/MacOS/` now paste the `safecoind` and `safecoin-cli`

### Optinal you can package the safewallet in a .dmg for release

Create a folder on your desktop and give it the name you want the image to be called.
Next copy the safecoinwallet.app to the folder.
Additionally you can add a short cut to the applications folder by going to your home directory and option dragging this to the folder on your desktop.

next open **Disk Utilty**
now go to `File -> New Image -> Image from Folder` select the folder on your desktop and click `choose` 
Enter the name you want the image to show up as this should be the same name as you named your folder.
For encryption choose `none` and for Image format choose `read-only`.
now click `save` 

### Support

For support or other questions, Join [Discord](https://discordapp.com/invite/vQgYGJz), or tweet at [@safecoins](https://twitter.com/safecoins) or [file an issue](https://github.com/Fair-Exchange/safewallet/issues).
