// Example: Using ONLY FAT12 filesystem
// This demonstrates how to include only FAT12 support in your project

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include "qfatfilesystem.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== FAT12 Filesystem Example ===";
    qDebug() << "This example uses ONLY FAT12 implementation";

    // Create a FAT12 filesystem instance
    QScopedPointer<QFAT12FileSystem> fs(QFAT12FileSystem::create("fat12_test.img"));

    if (fs.isNull()) {
        qDebug() << "ERROR: Failed to open FAT12 image";
        return 1;
    }

    qDebug() << "\n--- Listing Root Directory ---";
    QList<QFATFileInfo> files = fs->listRootDirectory();
    for (const QFATFileInfo &file : files) {
        qDebug() << (file.isDirectory ? "[DIR]" : "[FILE]")
                 << file.longName
                 << "Size:" << file.size << "bytes";
    }

    // Read a file
    qDebug() << "\n--- Reading a file ---";
    QFATError error;
    QByteArray data = fs->readFile("/test.txt", error);

    if (error == QFATError::None) {
        qDebug() << "File content:" << data;
    } else {
        qDebug() << "Read error:" << fs->errorString();
    }

    // Write a new file
    qDebug() << "\n--- Writing a new file ---";
    QByteArray newData = "Hello from FAT12!";
    bool success = fs->writeFile("/hello_fat12.txt", newData, error);

    if (success) {
        qDebug() << "File written successfully";

        // Verify by reading back
        QByteArray verify = fs->readFile("/hello_fat12.txt", error);
        qDebug() << "Verified content:" << verify;
    } else {
        qDebug() << "Write error:" << fs->errorString();
    }

    // Get filesystem info
    qDebug() << "\n--- Filesystem Information ---";
    quint32 totalSpace = fs->getTotalSpace(error);
    quint32 freeSpace = fs->getFreeSpace(error);
    quint32 usedSpace = totalSpace - freeSpace;

    qDebug() << "Total space:" << totalSpace << "bytes (" << totalSpace / 1024 << "KB)";
    qDebug() << "Free space:" << freeSpace << "bytes (" << freeSpace / 1024 << "KB)";
    qDebug() << "Used space:" << usedSpace << "bytes (" << usedSpace / 1024 << "KB)";

    qDebug() << "\n=== FAT12 Example Complete ===";

    return 0;
}
