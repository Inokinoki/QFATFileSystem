# Selective Filesystem Support - Quick Index

## Quick Links

| What You Want | Where to Look |
|--------------|---------------|
| **"How do I include only FAT32?"** | [README.md](README.md#quick-start) |
| **"What are the size savings?"** | [BENEFITS.md](BENEFITS.md#measured-results) |
| **"Show me a FAT12 example"** | [example_fat12_only.cpp](example_fat12_only.cpp) |
| **"Show me a FAT16 example"** | [example_fat16_only.cpp](example_fat16_only.cpp) |
| **"Show me a FAT32 example"** | [example_fat32_only.cpp](example_fat32_only.cpp) |
| **"How do I build FAT32 only?"** | [CMakeLists_FAT32_only.txt](CMakeLists_FAT32_only.txt) |
| **"I want custom selection"** | [CMakeLists_custom.txt](CMakeLists_custom.txt) |

## One-Minute Quick Start

### I only need FAT32:

```bash
# Copy the FAT32-only example
cp CMakeLists_FAT32_only.txt CMakeLists.txt

# Build it
mkdir build && cd build
cmake ..
make

# Run the example
./example_fat32_only /path/to/fat32.img
```

### I want to choose which filesystems to include:

```bash
# Copy the custom example
cp CMakeLists_custom.txt CMakeLists.txt

# Edit it to enable/disable filesystems
# set(INCLUDE_FAT12 ON)   # Enable/disable
# set(INCLUDE_FAT16 OFF)  # Enable/disable
# set(INCLUDE_FAT32 ON)   # Enable/disable

# Build
mkdir build && cd build
cmake ..
make
```

## What Each File Does

### Examples (Code)
- **example_fat12_only.cpp** - Working code example using only FAT12
- **example_fat16_only.cpp** - Working code example using only FAT16
- **example_fat32_only.cpp** - Working code example using only FAT32

### Build Scripts (CMake)
- **CMakeLists_FAT12_only.txt** - Builds library with FAT12 support only
- **CMakeLists_FAT16_only.txt** - Builds library with FAT16 support only
- **CMakeLists_FAT32_only.txt** - Builds library with FAT32 support only
- **CMakeLists_custom.txt** - Lets you pick which to include via flags

### Documentation
- **README.md** - Full documentation with all usage patterns
- **BENEFITS.md** - Measured performance and size benefits
- **INDEX.md** - This file - quick navigation

### Utilities
- **test_build.sh** - Tests all build configurations automatically
- **size_comparison.sh** - Shows size differences between configurations

## Common Use Cases

### Embedded System (Limited Flash)
➜ Use single filesystem: **295-307 KB** instead of 736 KB
➜ See: [CMakeLists_FAT12_only.txt](CMakeLists_FAT12_only.txt)

### Modern USB Drive Tool (FAT32 only)
➜ Use FAT32 only: **304 KB** instead of 736 KB (59% smaller)
➜ See: [CMakeLists_FAT32_only.txt](CMakeLists_FAT32_only.txt)

### Multi-Purpose Tool (All formats)
➜ Include all: **736 KB** (no change, but better organized)
➜ Use main project CMakeLists.txt

### Library Integration
➜ Copy only needed .cpp files to your project
➜ See: [README.md#integration-in-your-project](README.md#integration-in-your-project)

## Size Comparison at a Glance

| Configuration | Size | vs Full |
|--------------|------|---------|
| All filesystems | 736 KB | Baseline |
| FAT12 only | 295 KB | ✅ -60% |
| FAT16 only | 307 KB | ✅ -58% |
| FAT32 only | 304 KB | ✅ -59% |

Details: [BENEFITS.md](BENEFITS.md)

## Code Snippet Examples

### FAT12 Only
```cpp
#include "qfatfilesystem.h"
QScopedPointer<QFAT12FileSystem> fs(QFAT12FileSystem::create("disk.img"));
QByteArray data = fs->readFile("/file.txt", error);
```

### FAT16 Only
```cpp
#include "qfatfilesystem.h"
QScopedPointer<QFAT16FileSystem> fs(QFAT16FileSystem::create("disk.img"));
fs->createDirectory("/docs", error);
```

### FAT32 Only
```cpp
#include "qfatfilesystem.h"
QScopedPointer<QFAT32FileSystem> fs(QFAT32FileSystem::create("disk.img"));
fs->writeFile("/data.bin", largeData, error);
```

## Need More Help?

- Detailed usage: [README.md](README.md)
- Performance data: [BENEFITS.md](BENEFITS.md)
- Code examples: [example_*.cpp](.)
- Build configs: [CMakeLists_*.txt](.)

## Backward Compatibility

✅ No breaking changes - existing code still works
✅ Can still include all filesystems if needed
✅ Same API, just more flexible build options
