# QFATFileSystem

[![Code Quality](https://github.com/Inokinoki/QFATFileSystem/actions/workflows/code-quality.yml/badge.svg)](https://github.com/Inokinoki/QFATFileSystem/actions/workflows/code-quality.yml)
[![Linux CI](https://github.com/Inokinoki/QFATFileSystem/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/Inokinoki/QFATFileSystem/actions/workflows/ci-linux.yml)
[![macOS CI](https://github.com/Inokinoki/QFATFileSystem/actions/workflows/ci-macos.yml/badge.svg)](https://github.com/Inokinoki/QFATFileSystem/actions/workflows/ci-macos.yml)

A Qt-based library for reading and writing FAT16 and FAT32 filesystem images.

## Features

### Read Operations
- ✅ Read FAT16 and FAT32 filesystem images
- ✅ List root directory contents
- ✅ Navigate subdirectories
- ✅ Read file contents by path
- ✅ Parse file metadata (size, dates, attributes)
- ✅ Long filename (VFAT/LFN) support
- ✅ Check file/directory existence
- ✅ Get file information

### Write Operations
- ✅ Write files to FAT16 and FAT32 images
- ✅ Create directories
- ✅ Delete files
- ✅ Automatic cluster allocation and management
- ✅ Long filename (LFN) generation with checksum
- ✅ Short name generation (8.3 format)

### General Features
- ✅ Cross-platform (Linux, macOS, Windows)
- ✅ Qt5 and Qt6 support
- ✅ Error handling with detailed error codes
- ✅ Factory methods for easy instantiation

## Building

```bash
# Clone the repository
git clone https://github.com/Inokinok/QFATFileSystem.git
cd QFATFileSystem

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make
```

## Testing

### Prerequisites

**Linux:**
```bash
# Ubuntu/Debian
sudo apt-get install dosfstools mtools

# Fedora/RHEL
sudo dnf install dosfstools mtools

# Arch Linux
sudo pacman -S dosfstools mtools
```

**macOS:**
```bash
# Built-in tools (hdiutil, newfs_msdos) - no installation needed
```

### Building and Running Tests

Tests are automatically enabled and test images are generated during the build:

```bash
# Create build directory
mkdir build && cd build

# Configure with testing enabled (default)
cmake .. -DBUILD_TESTING=ON

# Build (this automatically generates test images)
make

# Run tests
ctest --output-on-failure --verbose
```

### How Test Image Generation Works

CMake automatically:
1. Checks for required tools (mtools on Linux, hdiutil on macOS)
2. Generates FAT16 and FAT32 test images during build
3. Copies images to the build directory

**Methods used:**
- **Linux**: `mtools` (no sudo required, no loop mounting)
- **macOS**: `hdiutil` (native built-in tools)

### Manual Test Image Generation

You can also manually generate test images:

```bash
cd tests
./generate_test_images.sh
```


## Usage Example

```cpp
#include "qfatfilesystem.h"
#include <QFile>

// Open a FAT16 image
QFile file("disk.img");
file.open(QIODevice::ReadOnly);
QFAT16FileSystem fs(&file);

// List root directory
QList<QFATFileInfo> files = fs.listRootDirectory();
for (const QFATFileInfo &info : files) {
    qDebug() << info.longName << info.size << "bytes";
}

// List subdirectory by path
QList<QFATFileInfo> subFiles = fs.listDirectory("/Documents");
```

## Contributing

Contributions are welcome! Please ensure:

1. All tests pass (`ctest` should succeed)
2. Code follows the existing style (`.clang-format` provided)
3. New features include appropriate tests

## License

See [LICENSE](LICENSE) file for details.

## Acknowledgments

This library is designed to be used in [QEFIEntryManager](https://github.com/Inokinok/QEFIEntryManager) project, to read EFI partitions and generate EFI entries.

The focus is for read-only access to FAT filesystems at very early stage. For other usages, please take your own risks.
