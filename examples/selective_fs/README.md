# Selective Filesystem Examples

This directory contains examples demonstrating how to include only the filesystem types you need in your project, reducing binary size and compilation time.

## Overview

After the refactoring, each filesystem implementation (FAT12, FAT16, FAT32) is in its own independent file. This allows you to:

- **Include only what you need**: If you only work with FAT32, include only that
- **Reduce binary size**: Each filesystem adds ~40-50KB to your binary
- **Faster compilation**: Compile only the code you use
- **Cleaner dependencies**: No unused code in your project

## File Structure

```
selective_fs/
├── README.md                           # This file
├── example_fat12_only.cpp              # Example using only FAT12
├── example_fat16_only.cpp              # Example using only FAT16
├── example_fat32_only.cpp              # Example using only FAT32
├── CMakeLists_FAT12_only.txt           # Build script for FAT12 only
├── CMakeLists_FAT16_only.txt           # Build script for FAT16 only
├── CMakeLists_FAT32_only.txt           # Build script for FAT32 only
└── CMakeLists_custom.txt               # Build script for custom selection
```

## Quick Start

### Option 1: Use a Single Filesystem Type

If you only need FAT32 support:

```bash
cd examples/selective_fs
cp CMakeLists_FAT32_only.txt CMakeLists.txt
mkdir build && cd build
cmake ..
make
./example_fat32_only
```

Replace `FAT32` with `FAT12` or `FAT16` as needed.

### Option 2: Custom Selection

To select specific filesystem types:

```bash
cd examples/selective_fs
cp CMakeLists_custom.txt CMakeLists.txt

# Edit CMakeLists.txt and set:
# set(INCLUDE_FAT12 ON)   # or OFF
# set(INCLUDE_FAT16 OFF)  # or ON
# set(INCLUDE_FAT32 ON)   # or OFF

mkdir build && cd build
cmake ..
make
```

Or use command-line options:

```bash
cmake -DINCLUDE_FAT12=OFF -DINCLUDE_FAT16=OFF -DINCLUDE_FAT32=ON ..
```

## Integration in Your Project

### Method 1: Copy Source Files

Copy only the files you need to your project:

```
# Required for all builds:
qfatfilesystem.h
qfatfilesystem_base.cpp
internal_constants.h

# Add only the filesystem types you need:
qfat12filesystem.cpp  # For FAT12 support
qfat16filesystem.cpp  # For FAT16 support
qfat32filesystem.cpp  # For FAT32 support
```

### Method 2: CMake Subdirectory

If using the library as a subdirectory:

```cmake
# In your CMakeLists.txt

# Option A: Include all filesystems (default)
add_subdirectory(path/to/QFATFileSystem)
target_link_libraries(your_target QFATFS::QFATFS)

# Option B: Build custom library with only what you need
set(QFATFS_SOURCES
    path/to/QFATFileSystem/qfatfilesystem_base.cpp
    path/to/QFATFileSystem/qfat32filesystem.cpp  # Only FAT32
)
add_library(qfatfs_custom ${QFATFS_SOURCES})
target_include_directories(qfatfs_custom PUBLIC path/to/QFATFileSystem)
target_link_libraries(your_target qfatfs_custom Qt${QT_VERSION_MAJOR}::Core)
```

### Method 3: Build and Install Full Library

```bash
# Build the full library
mkdir build && cd build
cmake ..
make
sudo make install

# In your project, link only what you need
# (The linker will only include symbols you actually use)
find_package(QFATFS REQUIRED)
target_link_libraries(your_target QFATFS::QFATFS)
```

## Binary Size Comparison

Approximate object file sizes (compiled for x86_64):

| Component | Size | Purpose |
|-----------|------|---------|
| `qfatfilesystem_base.o` | ~20KB | Required for all builds |
| `qfat12filesystem.o` | ~43KB | FAT12 support |
| `qfat16filesystem.o` | ~49KB | FAT16 support |
| `qfat32filesystem.o` | ~48KB | FAT32 support |

**Examples:**
- FAT32 only: ~68KB (base + fat32)
- FAT16 only: ~69KB (base + fat16)
- FAT12 only: ~63KB (base + fat12)
- All three: ~160KB (base + fat12 + fat16 + fat32)

## Code Examples

### Using FAT12 Only

```cpp
#include "qfatfilesystem.h"

// Create FAT12 filesystem
QScopedPointer<QFAT12FileSystem> fs(
    QFAT12FileSystem::create("image.img")
);

// Use the filesystem
QFATError error;
QByteArray data = fs->readFile("/test.txt", error);
```

### Using FAT16 Only

```cpp
#include "qfatfilesystem.h"

// Create FAT16 filesystem
QScopedPointer<QFAT16FileSystem> fs(
    QFAT16FileSystem::create("image.img")
);

// Create directory and write file
QFATError error;
fs->createDirectory("/docs", error);
fs->writeFile("/docs/readme.txt", "Hello", error);
```

### Using FAT32 Only

```cpp
#include "qfatfilesystem.h"

// Create FAT32 filesystem
QScopedPointer<QFAT32FileSystem> fs(
    QFAT32FileSystem::create("image.img")
);

// Perform operations
QFATError error;
fs->writeFile("/large_file.bin", largeData, error);
fs->renameFile("/old.txt", "/new.txt", error);
```

## Testing Your Build

Each example includes basic tests:

1. **FAT12 Example**: Creates and reads files
2. **FAT16 Example**: Tests directory operations
3. **FAT32 Example**: Tests rename and move operations

Run the examples with test images:

```bash
# From the build directory
./example_fat12_only path/to/fat12.img
./example_fat16_only path/to/fat16.img
./example_fat32_only path/to/fat32.img
```

## Benefits Summary

### For Embedded Systems
- Include only FAT12 or FAT16 for smaller flash memory footprint
- Reduce binary size by up to 100KB

### For Desktop Applications
- If you only need FAT32, don't include FAT12/FAT16
- Cleaner dependency tree

### For Library Authors
- Users can choose exactly what they need
- No forced inclusion of unused code

## Notes

- All examples require the base class (`qfatfilesystem_base.cpp`)
- The header file (`qfatfilesystem.h`) contains all class definitions
- Qt Core is required for all builds
- Link-time optimization will remove unused code even if you include all sources

## Support

If you encounter issues or have questions about selective inclusion:

1. Check that you've included `qfatfilesystem_base.cpp`
2. Verify Qt dependencies are properly linked
3. Ensure `internal_constants.h` is in your include path
4. See the main project README for general usage

## License

Same as the main QFATFileSystem project.
