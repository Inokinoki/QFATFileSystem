# CMakeLists.txt for building with FAT32 support ONLY
# This example shows how to include only the FAT32 filesystem in your project
# to reduce binary size and compilation time

cmake_minimum_required(VERSION 3.11)
project(FAT32OnlyExample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)

# Include the header
include_directories(../..)

# Build a library with ONLY FAT32 support
add_library(QFATFS_FAT32_ONLY
    ../../qfatfilesystem_base.cpp
    ../../qfat32filesystem.cpp
    # Note: We do NOT include qfat12filesystem.cpp or qfat16filesystem.cpp
)

target_link_libraries(QFATFS_FAT32_ONLY PUBLIC Qt${QT_VERSION_MAJOR}::Core)

# Build the example
add_executable(example_fat32_only example_fat32_only.cpp)
target_link_libraries(example_fat32_only QFATFS_FAT32_ONLY Qt${QT_VERSION_MAJOR}::Core)

# Usage instructions:
# 1. Copy this file to CMakeLists.txt in this directory
# 2. mkdir build && cd build
# 3. cmake ..
# 4. make
# 5. ./example_fat32_only
#
# Result: Your binary will include ONLY FAT32 support, making it smaller
