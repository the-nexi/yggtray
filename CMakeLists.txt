cmake_minimum_required(VERSION 3.10)

project(YggdrasilTray)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INSTALL_PREFIX "/usr")

# Define the version
set(YGGTRAY_VERSION "0.1.5")
add_definitions(-DYGGTRAY_VERSION="${YGGTRAY_VERSION}")

find_package(Qt5Widgets REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5 COMPONENTS Core LinguistTools REQUIRED)

# Enable AUTOMOC to process Q_OBJECT macros
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Add resources
qt5_add_resources(RESOURCES resources.qrc)

# Add executable
add_executable(yggtray tray.cpp ${RESOURCES})
target_link_libraries(yggtray Qt5::Widgets Qt5::Network)

# Copy icon file to build directory
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/yggtray_running.png
              ${CMAKE_CURRENT_BINARY_DIR}/yggtray_running.png
              COPYONLY)

# Define installation directories
set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin")
set(INSTALL_DOC_DIR "${CMAKE_INSTALL_PREFIX}/share/doc/${PROJECT_NAME}")
set(INSTALL_MAN_DIR "${CMAKE_INSTALL_PREFIX}/share/man/man1")

# Add installation rules
install(TARGETS yggtray DESTINATION ${INSTALL_BIN_DIR})

# Optional: Install a man page
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/yggtray.1 DESTINATION ${INSTALL_MAN_DIR})

# Add uninstall target
if(NOT TARGET uninstall)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -E remove -f "${INSTALL_BIN_DIR}/yggtray"
                                   "${INSTALL_MAN_DIR}/yggtray.1"
                                   "${CMAKE_INSTALL_PREFIX}/share/applications/yggtray.desktop"
                                   "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps/yggtray_running.png"
        COMMENT "Uninstalling YggdrasilTray"
    )
endif()

# Add a target to generate a package
include(CPack)

# CPack configuration
set(CPACK_PACKAGE_NAME "yggtray")
set(CPACK_PACKAGE_VERSION ${YGGTRAY_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "System tray application for Yggdrasil")
set(CPACK_PACKAGE_VENDOR "Nexi (Oleg Chagaev)")
set(CPACK_PACKAGE_CONTACT "olg5702olg@protonmail.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Nexi (Oleg Chagaev)")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")

# Create desktop file content
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/yggtray.desktop
"[Desktop Entry]
Type=Application
Name=YggdrasilTray
Comment=System tray application for Yggdrasil
Exec=yggtray
Icon=yggtray_running
Categories=Network;System;
Terminal=false")

# Install desktop file and icon
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/yggtray_running.png
    DESTINATION share/icons/hicolor/256x256/apps
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/yggtray.desktop
    DESTINATION share/applications
)

# AppImage target
add_custom_target(appimage
    DEPENDS yggtray
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/AppDir
    COMMAND ${CMAKE_COMMAND} -E env DESTDIR=${CMAKE_BINARY_DIR}/AppDir ${CMAKE_COMMAND} --install .
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/AppDir/usr/share/applications/yggtray.desktop ${CMAKE_BINARY_DIR}/AppDir/yggtray.desktop
    COMMAND ${CMAKE_COMMAND} -E create_symlink usr/bin/yggtray ${CMAKE_BINARY_DIR}/AppDir/AppRun
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/yggtray_running.png ${CMAKE_BINARY_DIR}/AppDir/yggtray_running.png
    COMMAND wget -c -nv "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    COMMAND chmod +x appimagetool-x86_64.AppImage
    COMMAND VERSION=${YGGTRAY_VERSION} ./appimagetool-x86_64.AppImage AppDir
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Creating AppImage..."
)

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

