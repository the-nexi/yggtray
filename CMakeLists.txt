cmake_minimum_required(VERSION 3.5)

project(YggdrasilTray)

set(CMAKE_CXX_STANDARD 14)

find_package(Qt5Widgets REQUIRED)
find_package(Qt5Network REQUIRED)

# Enable AUTOMOC to process Q_OBJECT macros
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(yggtray tray.cpp)

target_link_libraries(yggtray Qt5::Widgets Qt5::Network)

# Add Doxygen support
find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/DoxygenDocs)

    add_custom_target(
        doc ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_IN}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen documentation"
        VERBATIM
    )
else ()
    message(WARNING "Doxygen not found. Documentation won't be generated.")
endif ()

