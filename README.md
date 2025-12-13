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

The tests can be enabled by setting `BUILD_TESTING` to `ON` in the CMake configuration:

```bash
cmake .. -DBUILD_TESTING=ON
make
```

### Test Image Generation

Tests require FAT16 and FAT32 test images. The `generate_test_images.sh` script automatically creates these images and supports multiple environments:

#### Development Environment Dependencies

**Linux (Native):**
```bash
# Required for test image generation
sudo apt-get install dosfstools mtools

# mtools is recommended for non-privileged operation
# Without mtools, sudo privileges are required for loop mounting
```

**Linux (Docker):**
```bash
# Install in Dockerfile
RUN apt-get update && apt-get install -y \
    dosfstools \
    mtools
```

**macOS:**
```bash
# Built-in tools are used (hdiutil, newfs_msdos)
# No additional installation needed
```

#### Generating Test Images

The script automatically detects the environment and chooses the best method:

```bash
cd tests
./generate_test_images.sh
```

**Methods:**
- **Docker/Non-privileged**: Uses `mtools` (no loop mounting required)
- **Linux with sudo**: Uses loop device mounting
- **macOS**: Uses `hdiutil`

### Running Tests

Once test images are generated, run tests using `ctest`:

```bash
ctest --output-on-failure --verbose
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
