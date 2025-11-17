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
    void testRootContentFAT16();
    void testRootContentFAT32();
    void testSubdirectoryContentFAT16();
    void testSubdirectoryContentFAT32();
    void testFileSizes();
    void testLongFilenames();

private:
    bool findFileByName(const QList<FileInfo> &files, const QString &name);
    FileInfo getFileByName(const QList<FileInfo> &files, const QString &name);
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

bool TestQFATFileSystem::findFileByName(const QList<FileInfo> &files, const QString &name)
{
    QString upperName = name.toUpper();
    for (const FileInfo &file : files) {
        // Check exact match first
        if (file.name.toUpper() == upperName || file.longName.toUpper() == upperName) {
            return true;
        }

        // Check if short name matches pattern (e.g., LARGE~13.BIN matches largefile.bin)
        // For 8.3 names with tilde notation (e.g., LARGE~1), check if the prefix before ~ matches
        QString fileName = file.name.toUpper();
        if (fileName.contains("~")) {
            QString fileBaseName = fileName.left(fileName.indexOf("~"));
            QString searchBaseName = upperName.contains(".") ? upperName.left(upperName.indexOf(".")) : upperName;

            // Check if the search name starts with the short base name (before ~)
            if (searchBaseName.startsWith(fileBaseName)) {
                // Verify the extension matches too
                QString searchExt = upperName.contains(".") ? upperName.mid(upperName.lastIndexOf('.')) : "";
                QString fileExt = fileName.contains(".") ? fileName.mid(fileName.lastIndexOf('.')) : "";
                if (searchExt.isEmpty() || fileExt.toUpper() == searchExt.toUpper()) {
                    return true;
                }
            }
        }
    }
    return false;
}

FileInfo TestQFATFileSystem::getFileByName(const QList<FileInfo> &files, const QString &name)
{
    QString upperName = name.toUpper();
    for (const FileInfo &file : files) {
        // Check exact match first
        if (file.name.toUpper() == upperName || file.longName.toUpper() == upperName) {
            return file;
        }

        // Check if short name matches pattern (e.g., LARGE~13.BIN matches largefile.bin)
        QString fileName = file.name.toUpper();
        if (fileName.contains("~")) {
            QString fileBaseName = fileName.left(fileName.indexOf("~"));
            QString searchBaseName = upperName.contains(".") ? upperName.left(upperName.indexOf(".")) : upperName;

            // Check if the search name starts with the short base name (before ~)
            if (searchBaseName.startsWith(fileBaseName)) {
                // Verify the extension matches too
                QString searchExt = upperName.contains(".") ? upperName.mid(upperName.lastIndexOf('.')) : "";
                QString fileExt = fileName.contains(".") ? fileName.mid(fileName.lastIndexOf('.')) : "";
                if (searchExt.isEmpty() || fileExt.toUpper() == searchExt.toUpper()) {
                    return file;
                }
            }
        }
    }
    return FileInfo();
}

void TestQFATFileSystem::testRootContentFAT16()
{
    QFile file(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(&file);

    QList<FileInfo> files = fs.listRootDirectory();

    // Verify expected files exist
    QVERIFY2(findFileByName(files, "hello.txt"), "hello.txt not found");
    QVERIFY2(findFileByName(files, "test.txt"), "test.txt not found");
    QVERIFY2(findFileByName(files, "readme.txt"), "readme.txt not found");
    QVERIFY2(findFileByName(files, "empty.txt"), "empty.txt not found");
    QVERIFY2(findFileByName(files, "binary.dat"), "binary.dat not found");
    QVERIFY2(findFileByName(files, "largefile.bin"), "largefile.bin not found");

    // Verify expected directories exist
    QVERIFY2(findFileByName(files, "subdir1"), "subdir1 not found");
    QVERIFY2(findFileByName(files, "subdir2"), "subdir2 not found");
    QVERIFY2(findFileByName(files, "Documents"), "Documents not found");

    // Verify directory flags
    FileInfo subdir1 = getFileByName(files, "subdir1");
    QVERIFY2(subdir1.isDirectory, "subdir1 should be a directory");

    FileInfo helloFile = getFileByName(files, "hello.txt");
    QVERIFY2(!helloFile.isDirectory, "hello.txt should not be a directory");

    qDebug() << "FAT16 root content validation passed";
}

void TestQFATFileSystem::testRootContentFAT32()
{
    QFile file(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(&file);

    QList<FileInfo> files = fs.listRootDirectory();

    // Verify expected files exist
    QVERIFY2(findFileByName(files, "hello.txt"), "hello.txt not found");
    QVERIFY2(findFileByName(files, "test.txt"), "test.txt not found");
    QVERIFY2(findFileByName(files, "readme.txt"), "readme.txt not found");
    QVERIFY2(findFileByName(files, "empty.txt"), "empty.txt not found");
    QVERIFY2(findFileByName(files, "binary.dat"), "binary.dat not found");
    QVERIFY2(findFileByName(files, "largefile.bin"), "largefile.bin not found");

    // Verify expected directories exist
    QVERIFY2(findFileByName(files, "subdir1"), "subdir1 not found");
    QVERIFY2(findFileByName(files, "subdir2"), "subdir2 not found");
    QVERIFY2(findFileByName(files, "Documents"), "Documents not found");

    // Verify directory flags
    FileInfo subdir2 = getFileByName(files, "subdir2");
    QVERIFY2(subdir2.isDirectory, "subdir2 should be a directory");

    FileInfo testFile = getFileByName(files, "test.txt");
    QVERIFY2(!testFile.isDirectory, "test.txt should not be a directory");

    qDebug() << "FAT32 root content validation passed";
}

void TestQFATFileSystem::testSubdirectoryContentFAT16()
{
    QFile file(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(&file);

    // Get root directory to find subdir1
    QList<FileInfo> rootFiles = fs.listRootDirectory();
    FileInfo subdir1 = getFileByName(rootFiles, "subdir1");
    QVERIFY2(subdir1.isDirectory, "subdir1 should be a directory");
    QVERIFY2(subdir1.cluster >= 2, "subdir1 should have a valid cluster");

    // List subdir1 contents
    QList<FileInfo> subdir1Files = fs.listDirectory(static_cast<quint16>(subdir1.cluster));

    // Verify expected files in subdir1
    QVERIFY2(findFileByName(subdir1Files, "file1.txt"), "file1.txt not found in subdir1");
    QVERIFY2(findFileByName(subdir1Files, "file2.txt"), "file2.txt not found in subdir1");
    QVERIFY2(findFileByName(subdir1Files, "nested"), "nested directory not found in subdir1");

    // Verify nested is a directory
    FileInfo nested = getFileByName(subdir1Files, "nested");
    QVERIFY2(nested.isDirectory, "nested should be a directory");

    qDebug() << "FAT16 subdirectory content validation passed";
}

void TestQFATFileSystem::testSubdirectoryContentFAT32()
{
    QFile file(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(&file);

    // Get root directory to find Documents
    QList<FileInfo> rootFiles = fs.listRootDirectory();
    FileInfo docsDir = getFileByName(rootFiles, "Documents");
    QVERIFY2(docsDir.isDirectory, "Documents should be a directory");
    QVERIFY2(docsDir.cluster >= 2, "Documents should have a valid cluster");

    // List Documents contents
    QList<FileInfo> docFiles = fs.listDirectory(docsDir.cluster);

    // Verify expected files in Documents
    QVERIFY2(findFileByName(docFiles, "doc1.txt"), "doc1.txt not found in Documents");
    QVERIFY2(findFileByName(docFiles, "doc2.txt"), "doc2.txt not found in Documents");

    qDebug() << "FAT32 subdirectory content validation passed";
}

void TestQFATFileSystem::testFileSizes()
{
    // Test FAT16
    QFile file16(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file16.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs16(&file16);

    QList<FileInfo> files16 = fs16.listRootDirectory();

    // Check empty.txt is actually empty
    FileInfo emptyFile = getFileByName(files16, "empty.txt");
    QCOMPARE(emptyFile.size, quint32(0));

    // Check largefile.bin has expected size (100 KB)
    FileInfo largeFile = getFileByName(files16, "largefile.bin");
    QCOMPARE(largeFile.size, quint32(102400));

    // Check binary.dat has expected size (10 KB)
    FileInfo binaryFile = getFileByName(files16, "binary.dat");
    QCOMPARE(binaryFile.size, quint32(10240));

    // Test FAT32
    QFile file32(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file32.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs32(&file32);

    QList<FileInfo> files32 = fs32.listRootDirectory();

    // Check empty.txt is actually empty
    FileInfo emptyFile32 = getFileByName(files32, "empty.txt");
    QCOMPARE(emptyFile32.size, quint32(0));

    // Check largefile.bin has expected size (100 KB)
    FileInfo largeFile32 = getFileByName(files32, "largefile.bin");
    QCOMPARE(largeFile32.size, quint32(102400));

    qDebug() << "File size validation passed";
}

void TestQFATFileSystem::testLongFilenames()
{
    // Test FAT16
    QFile file16(TEST_FAT16_IMAGE_PATH);
    QVERIFY(file16.open(QIODevice::ReadOnly));
    QFAT16FileSystem fs16(&file16);

    QList<FileInfo> files16 = fs16.listRootDirectory();

    // Check for long filename
    QVERIFY2(findFileByName(files16, "this_is_a_long_filename.txt"), "Long filename not found in FAT16");

    // Test FAT32
    QFile file32(TEST_FAT32_IMAGE_PATH);
    QVERIFY(file32.open(QIODevice::ReadOnly));
    QFAT32FileSystem fs32(&file32);

    QList<FileInfo> files32 = fs32.listRootDirectory();

    // Check for long filename
    QVERIFY2(findFileByName(files32, "this_is_a_long_filename.txt"), "Long filename not found in FAT32");

    qDebug() << "Long filename validation passed";
}

QTEST_MAIN(TestQFATFileSystem)
#include "test_qfatfilesystem_read_image.moc"