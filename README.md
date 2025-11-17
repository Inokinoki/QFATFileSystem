# QFATFileSystem

[![Build and Test](https://github.com/Inokinok/QFATFileSystem/actions/workflows/test.yml/badge.svg)](https://github.com/Inokinok/QFATFileSystem/actions/workflows/test.yml)
[![Extended Test Matrix](https://github.com/Inokinok/QFATFileSystem/actions/workflows/test-matrix.yml/badge.svg)](https://github.com/Inokinok/QFATFileSystem/actions/workflows/test-matrix.yml)

A Qt-based library for reading FAT16 and FAT32 filesystem images.

## Features

- ✅ Read FAT16 filesystem images
- ✅ Read FAT32 filesystem images
- ✅ List root directory contents
- ✅ Navigate subdirectories
- ✅ Parse file metadata (size, dates, attributes)
- ✅ Long filename (VFAT/LFN) support
- ✅ Cross-platform (Linux, macOS, Windows)
- TODO: Write to filesystem images

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

### Additional Requirements for Testing

- **Linux**: `dosfstools` package (for `mkfs.fat`)
- **macOS**: Built-in `hdiutil` and `newfs_msdos` (no additional installation needed)

# Run tests

The tests can be run using the `ctest` command:

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
