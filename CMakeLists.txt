cmake_minimum_required(VERSION 3.11)

set(QFATFS_VERSION_MAJOR 0)
set(QFATFS_VERSION_MINOR 0)
set(QFATFS_VERSION_PATCH 1)
set(QFATFS_VERSION ${QFATFS_VERSION_MAJOR}.${QFATFS_VERSION_MINOR}.${QFATFS_VERSION_PATCH})

project(QFATFS LANGUAGES CXX VERSION ${QFATFS_VERSION})

# make cache variables for install destinations
include(GNUInstallDirs)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)

add_library(QFATFS
    qfatfilesystem.cpp
)
add_library(QFATFS::QFATFS ALIAS QFATFS)
set_target_properties(QFATFS PROPERTIES PUBLIC_HEADER qfatfilesystem.h)
target_link_libraries(QFATFS PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(QFATFS PRIVATE QFATFS_LIBRARY)

install(
    TARGETS QFATFS
    EXPORT QFATFSTargets
    PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include
)

install(
    EXPORT QFATFSTargets
    FILE QFATFSTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QFATFS
)

if(BUILD_TESTING)
    # Enable testing for the project
    enable_testing()

    add_subdirectory(tests)
endif()

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/QFATFSConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/QFATFSConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QFATFS
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/QFATFSConfigVersion.cmake
    VERSION ${QFATFS_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/QFATFSConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/QFATFSConfigVersion.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/QFATFS
)