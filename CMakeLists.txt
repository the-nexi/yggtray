cmake_minimum_required(VERSION 3.5)

project(YggdrasilTray)

set(CMAKE_CXX_STANDARD 14)

find_package(Qt5Widgets REQUIRED)
find_package(Qt5Network REQUIRED)

# Enable AUTOMOC to process Q_OBJECT macros
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(tray tray.cpp)

target_link_libraries(tray Qt5::Widgets Qt5::Network)

