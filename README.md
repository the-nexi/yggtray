# Yggtray

[Yggdrasil](https://yggdrasil-network.github.io/) tray and control panel.  It
allows to configure, run and control the Yggdrasil daemon.

It also provides first setup wizard, which adds user to "yggdrasil" group in order to interface with Yggdrasil daemon, and optionally creates ip6tables rules to protect local services from exposure to yggdrasil network.

This wiard can be launched by running `yggtray --setup` command.

## CLI arguments

* `--setup` - show setup wizard
* `--version` - show version


## Installation

```
./make.sh
cd build/
sudo make install

#to uninstall:
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
## Documentation
This project has Doxygen documentation available, doc directory will be created after compiling.
