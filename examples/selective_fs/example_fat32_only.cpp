// Example: Using ONLY FAT32 filesystem
// This demonstrates how to include only FAT32 support in your project

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include "qfatfilesystem.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== FAT32 Filesystem Example ===";
    qDebug() << "This example uses ONLY FAT32 implementation";

    // Create a FAT32 filesystem instance
    QScopedPointer<QFAT32FileSystem> fs(QFAT32FileSystem::create("fat32_test.img"));

    if (fs.isNull()) {
        qDebug() << "ERROR: Failed to open FAT32 image";
        return 1;
    }

    qDebug() << "\n--- Listing Root Directory ---";
    QList<QFATFileInfo> files = fs->listRootDirectory();
    for (const QFATFileInfo &file : files) {
        qDebug() << (file.isDirectory ? "[DIR]" : "[FILE]")
                 << file.longName
                 << "Size:" << file.size << "bytes";
    }

    // Test file operations
    qDebug() << "\n--- File Operations Test ---";
    QFATError error;

    // Write a file
    QByteArray largeData;
    for (int i = 0; i < 1000; i++) {
        largeData.append(QString("Line %1: FAT32 can handle larger files!\n").arg(i).toUtf8());
    }

    bool success = fs->writeFile("/large_file.txt", largeData, error);
    if (success) {
        qDebug() << "Large file written successfully (" << largeData.size() << "bytes)";

        // Get file info
        QFATFileInfo info = fs->getFileInfo("/large_file.txt", error);
        if (error == QFATError::None) {
            qDebug() << "File info:";
            qDebug() << "  Name:" << info.longName;
            qDebug() << "  Size:" << info.size << "bytes";
            qDebug() << "  Created:" << info.created;
            qDebug() << "  Modified:" << info.modified;
        }
    }

    // Test rename
    qDebug() << "\n--- Rename Operation ---";
    success = fs->renameFile("/large_file.txt", "/renamed_file.txt", error);
    if (success) {
        qDebug() << "File renamed successfully";
        qDebug() << "File exists at new name:" << fs->exists("/renamed_file.txt");
        qDebug() << "File exists at old name:" << fs->exists("/large_file.txt");
    }

    // Test directory operations
    qDebug() << "\n--- Directory Operations ---";
    success = fs->createDirectory("/fat32_docs", error);
    if (success || error == QFATError::InvalidPath) {
        qDebug() << "Directory created (or already exists)";

        // Create nested directory
        success = fs->createDirectory("/fat32_docs/subdir", error);
        if (success) {
            qDebug() << "Nested directory created";

            // Move file into directory
            success = fs->moveFile("/renamed_file.txt", "/fat32_docs/moved_file.txt", error);
            if (success) {
                qDebug() << "File moved successfully";
            }
        }
    }

    // Get filesystem info
    qDebug() << "\n--- Filesystem Information ---";
    quint32 totalSpace = fs->getTotalSpace(error);
    quint32 freeSpace = fs->getFreeSpace(error);

    qDebug() << "Total space:" << totalSpace / 1024 / 1024 << "MB";
    qDebug() << "Free space:" << freeSpace / 1024 / 1024 << "MB";
    qDebug() << "Usage:" << ((totalSpace - freeSpace) * 100 / totalSpace) << "%";

    qDebug() << "\n=== FAT32 Example Complete ===";

    return 0;
}
