#!/bin/bash

# Check if appimage parameter is provided
if [ "$1" = "appimage" ]; then
    rm -rf build
    mkdir build
    cd build
    cmake ..
    make appimage
else
    rm -rf build
    mkdir build
    cd build
    cmake ..
    make VERBOSE=1
fi
cd ..
