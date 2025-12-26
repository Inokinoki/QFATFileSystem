// Example: Using ONLY FAT16 filesystem
// This demonstrates how to include only FAT16 support in your project

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include "qfatfilesystem.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== FAT16 Filesystem Example ===";
    qDebug() << "This example uses ONLY FAT16 implementation";

    // Create a FAT16 filesystem instance
    QScopedPointer<QFAT16FileSystem> fs(QFAT16FileSystem::create("fat16_test.img"));

    if (fs.isNull()) {
        qDebug() << "ERROR: Failed to open FAT16 image";
        return 1;
    }

    qDebug() << "\n--- Listing Root Directory ---";
    QList<QFATFileInfo> files = fs->listRootDirectory();
    for (const QFATFileInfo &file : files) {
        qDebug() << (file.isDirectory ? "[DIR]" : "[FILE]")
                 << file.longName
                 << "Size:" << file.size << "bytes";
    }

    // Create a directory
    qDebug() << "\n--- Creating a directory ---";
    QFATError error;
    bool success = fs->createDirectory("/fat16_test_dir", error);

    if (success) {
        qDebug() << "Directory created successfully";
    } else {
        qDebug() << "Directory may already exist:" << fs->errorString();
    }

    // Write a file in the new directory
    qDebug() << "\n--- Writing a file in subdirectory ---";
    QByteArray data = "This file was created using FAT16!";
    success = fs->writeFile("/fat16_test_dir/readme.txt", data, error);

    if (success) {
        qDebug() << "File written successfully";

        // List directory contents
        qDebug() << "\n--- Listing subdirectory ---";
        QList<QFATFileInfo> dirFiles = fs->listDirectory("/fat16_test_dir");
        for (const QFATFileInfo &file : dirFiles) {
            qDebug() << "  " << (file.isDirectory ? "[DIR]" : "[FILE]")
                     << file.longName;
        }
    } else {
        qDebug() << "Write error:" << fs->errorString();
    }

    // Partial read example
    qDebug() << "\n--- Partial file read ---";
    QByteArray partial = fs->readFilePartial("/fat16_test_dir/readme.txt", 0, 10, error);
    if (error == QFATError::None) {
        qDebug() << "First 10 bytes:" << partial;
    }

    // Get filesystem info
    qDebug() << "\n--- Filesystem Information ---";
    quint32 totalSpace = fs->getTotalSpace(error);
    quint32 freeSpace = fs->getFreeSpace(error);

    qDebug() << "Total space:" << totalSpace / 1024 / 1024 << "MB";
    qDebug() << "Free space:" << freeSpace / 1024 / 1024 << "MB";

    qDebug() << "\n=== FAT16 Example Complete ===";

    return 0;
}
