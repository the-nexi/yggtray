# Yggtray

[Yggdrasil](https://yggdrasil-network.github.io/) tray and control panel.  It
allows to configure, run and control the Yggdrasil daemon.

It also provides first setup wizard, which adds user to "yggdrasil" group in order to interface with Yggdrasil daemon, and optionally creates ip6tables rules to protect local services from exposure to yggdrasil network.

This wiard can be launched by running `yggtray --setup` command.

## CLI arguments

* `--setup` - show setup wizard
* `--version` - show version


## Installation

### GNU Guix

To install the latest Yggtray version through [GNU Guix](https://guix.gnu.org/) package manager, run the following command:
```
guix install yggtray
```

### Manual

```
./make.sh
cd build/
sudo make install
```

to uninstall:
```
sudo make uninstall
```

## Building

```
git clone https://github.com/the-nexi/yggtray.git
cd yggtray
mkdir build
cd build
cmake ..
make -j$(nproc)
cd .. 
```

## AppImage

To build a portable AppImage:
```
./make.sh appimage
```

This will create `YggdrasilTray-<version>-x86_64.AppImage` in the build directory. The AppImage can be run on any Linux system without installation:
```
chmod +x build/YggdrasilTray-*-x86_64.AppImage
./build/YggdrasilTray-*-x86_64.AppImage
```

## Documentation
This project has Doxygen documentation available, doc directory will be created after compiling.

## License

This project is licensed under the GNU General Public License v3 (GNU GPL v3).  

Icons used in this application are sourced from the [Qogir-kde](https://github.com/vinceliuice/Qogir-kde) icon theme, which is also licensed under GNU GPL v3.  


