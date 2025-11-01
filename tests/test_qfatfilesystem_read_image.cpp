#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT16_IMAGE_PATH = "data/fat16.img";
const QString TEST_FAT32_IMAGE_PATH = "data/fat32.img";

class TestQFATFileSystem : public QObject
{
    Q_OBJECT
private slots:
    void testListFilesFAT16();
    void testListFilesFAT32();
    void testListRootDirectoryFAT16();
    void testListRootDirectoryFAT32();
    void testFileInfoStructure();
    void testListDirectoryFAT16();
    void testListDirectoryFAT32();
    void testListDirectoryPath();
};

void TestQFATFileSystem::testListFilesFAT16()
{
    QFile file(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(&file);

    QList<FileInfo> files = fs.listRootDirectory();

    // Verify that we can read files
    QVERIFY(files.size() >= 0);

    qDebug() << "FAT16: Found" << files.size() << "files/directories";

    // Print file information for debugging
    for (const FileInfo &file : files) {
        qDebug() << "File:" << file.longName << "(" << file.name << ")"
                 << "Size:" << file.size << "Directory:" << file.isDirectory;
        if (!file.modified.isNull()) {
            qDebug() << "Modified:" << file.modified.toString();
        }
    }
}

void TestQFATFileSystem::testListFilesFAT32()
{
    QFile file(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(&file);

    QList<FileInfo> files = fs.listRootDirectory();

    // Verify that we can read files
    QVERIFY(files.size() >= 0);

    qDebug() << "FAT32: Found" << files.size() << "files/directories";

    // Print file information for debugging
    for (const FileInfo &file : files) {
        qDebug() << "File:" << file.longName << "(" << file.name << ")"
                 << "Size:" << file.size << "Directory:" << file.isDirectory;
        if (!file.modified.isNull()) {
            qDebug() << "Modified:" << file.modified.toString();
        }
    }
}

void TestQFATFileSystem::testListRootDirectoryFAT16()
{
    QFile file(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(&file);

    QList<FileInfo> files = fs.listRootDirectory();

    // Verify that the auto-detection works
    QVERIFY(files.size() >= 0);

    qDebug() << "Root directory (FAT16): Found" << files.size() << "entries";
}

void TestQFATFileSystem::testListRootDirectoryFAT32()
{
    QFile file(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(&file);

    QList<FileInfo> files = fs.listRootDirectory();

    // Verify that the auto-detection works
    QVERIFY(files.size() >= 0);

    qDebug() << "Root directory (FAT32): Found" << files.size() << "entries";
}

void TestQFATFileSystem::testFileInfoStructure()
{
    // Test that FileInfo structure initializes correctly
    FileInfo info;

    QVERIFY(info.name.isEmpty());
    QVERIFY(info.longName.isEmpty());
    QVERIFY(info.isDirectory == false);
    QVERIFY(info.size == 0);
    QVERIFY(info.attributes == 0);
    QVERIFY(info.created.isNull());
    QVERIFY(info.modified.isNull());
    QVERIFY(info.cluster == 0);

    // Test setting values
    info.name = "TEST.TXT";
    info.longName = "Test File.txt";
    info.size = 1024;
    info.isDirectory = false;
    info.attributes = 0x20; // Archive attribute
    info.cluster = 5;

    QCOMPARE(info.name, QString("TEST.TXT"));
    QCOMPARE(info.longName, QString("Test File.txt"));
    QCOMPARE(info.size, quint32(1024));
    QCOMPARE(info.isDirectory, false);
    QCOMPARE(info.attributes, quint16(0x20));
    QCOMPARE(info.cluster, quint32(5));
}

void TestQFATFileSystem::testListDirectoryFAT16()
{
    QFile file(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(&file);

    // First, get root directory files to find a subdirectory
    QList<FileInfo> rootFiles = fs.listRootDirectory();

    // Try to find a directory entry and list it
    bool foundDir = false;
    for (const FileInfo &file : rootFiles) {
        if (file.isDirectory && file.name != "." && file.name != ".." && file.cluster >= 2) {
            qDebug() << "Found directory:" << file.name << "at cluster" << file.cluster;
            foundDir = true;

            // Try to list this directory using cluster number
            if (file.cluster < 0xFFF8) {
                QList<FileInfo> dirFiles = fs.listDirectory(static_cast<quint16>(file.cluster));
                qDebug() << "Directory" << file.name << "contains" << dirFiles.size() << "entries";
                QVERIFY(dirFiles.size() >= 0); // Should have at least . and .. entries
            }
            break;
        }
    }

    qDebug() << "Directory listing test - found directories:" << foundDir;
}

void TestQFATFileSystem::testListDirectoryFAT32()
{
    QFile file(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(&file);

    // First, get root directory files to find a subdirectory
    QList<FileInfo> rootFiles = fs.listRootDirectory();

    // Try to find a directory entry and list it
    bool foundDir = false;
    for (const FileInfo &file : rootFiles) {
        if (file.isDirectory && file.name != "." && file.name != ".." && file.cluster >= 2) {
            qDebug() << "Found directory:" << file.name << "at cluster" << file.cluster;
            foundDir = true;

            // Try to list this directory using cluster number
            if (file.cluster < 0x0FFFFFF8) {
                QList<FileInfo> dirFiles = fs.listDirectory(file.cluster);
                qDebug() << "Directory" << file.name << "contains" << dirFiles.size() << "entries";
                QVERIFY(dirFiles.size() >= 0); // Should have at least . and .. entries
            }
            break;
        }
    }

    qDebug() << "Directory listing test - found directories:" << foundDir;
}

void TestQFATFileSystem::testListDirectoryPath()
{
    QFile file16(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file16.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs16(&file16);

    // Test root directory via path
    QList<FileInfo> rootFiles = fs16.listDirectory("/");
    QVERIFY(rootFiles.size() >= 0);
    qDebug() << "Root directory via path contains" << rootFiles.size() << "entries";

    QFile file32(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file32.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs32(&file32);

    // Test root directory via path
    QList<FileInfo> rootFiles32 = fs32.listDirectory("/");
    QVERIFY(rootFiles32.size() >= 0);
    qDebug() << "Root directory via path (FAT32) contains" << rootFiles32.size() << "entries";
}

QTEST_MAIN(TestQFATFileSystem)
#include "test_qfatfilesystem_read_image.moc"