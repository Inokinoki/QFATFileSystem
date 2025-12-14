#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT32_IMAGE_PATH = "data/fat32.img";

class TestFAT32AdvancedOperations : public QObject
{
    Q_OBJECT
private slots:
    // Partial read tests
    void testPartialRead();
    void testPartialReadWithOffset();

    // Rename tests
    void testRenameFile();
    void testRenameDirectory();

    // Move tests
    void testMoveFile();

    // Recursive directory deletion tests
    void testDeleteEmptyDirectoryNonRecursive();
    void testDeleteNonEmptyDirectoryRecursive();

    // Filesystem info tests
    void testGetFreeSpace();
    void testGetTotalSpace();
};

void TestFAT32AdvancedOperations::testPartialRead()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_partial.img");
    QFile::setPermissions("test_fat32_partial.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_partial.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QFATError error;
    bool success = fs->writeFile("/partial32.txt", testData, error);
    QVERIFY(success);

    // Read partial data
    QByteArray partial = fs->readFilePartial("/partial32.txt", 0, 10, error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(partial, QByteArray("0123456789"));

    qDebug() << "Successfully read partial data from FAT32";

    QFile::remove("test_fat32_partial.img");
}

void TestFAT32AdvancedOperations::testPartialReadWithOffset()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_partial_offset.img");
    QFile::setPermissions("test_fat32_partial_offset.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_partial_offset.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QFATError error;
    fs->writeFile("/offset32.txt", testData, error);

    // Read from middle
    QByteArray partial = fs->readFilePartial("/offset32.txt", 10, 16, error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(partial, QByteArray("ABCDEFGHIJKLMNOP"));

    qDebug() << "Successfully read partial data with offset in FAT32";

    QFile::remove("test_fat32_partial_offset.img");
}

void TestFAT32AdvancedOperations::testRenameFile()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_rename.img");
    QFile::setPermissions("test_fat32_rename.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_rename.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "Content to rename in FAT32";
    QFATError error;
    fs->writeFile("/oldname32.txt", testData, error);
    QVERIFY(fs->exists("/oldname32.txt"));

    // Rename the file
    bool success = fs->renameFile("/oldname32.txt", "/newname32.txt", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/oldname32.txt"));
    QVERIFY(fs->exists("/newname32.txt"));

    // Verify content
    QByteArray readData = fs->readFile("/newname32.txt", error);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully renamed file in FAT32";

    QFile::remove("test_fat32_rename.img");
}

void TestFAT32AdvancedOperations::testRenameDirectory()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_rename_dir.img");
    QFile::setPermissions("test_fat32_rename_dir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_rename_dir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/olddir32", error);
    QVERIFY(fs->exists("/olddir32"));

    // Rename the directory
    bool success = fs->renameFile("/olddir32", "/newdir32", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/olddir32"));
    QVERIFY(fs->exists("/newdir32"));

    qDebug() << "Successfully renamed directory in FAT32";

    QFile::remove("test_fat32_rename_dir.img");
}

void TestFAT32AdvancedOperations::testMoveFile()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_move.img");
    QFile::setPermissions("test_fat32_move.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_move.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/source32", error);
    fs->createDirectory("/dest32", error);

    QByteArray testData = "Moving file in FAT32";
    fs->writeFile("/source32/file32.txt", testData, error);
    QVERIFY(fs->exists("/source32/file32.txt"));

    // Move the file
    bool success = fs->moveFile("/source32/file32.txt", "/dest32/file32.txt", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/source32/file32.txt"));
    QVERIFY(fs->exists("/dest32/file32.txt"));

    // Verify content
    QByteArray readData = fs->readFile("/dest32/file32.txt", error);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully moved file in FAT32";

    QFile::remove("test_fat32_move.img");
}

void TestFAT32AdvancedOperations::testDeleteEmptyDirectoryNonRecursive()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_del_empty.img");
    QFile::setPermissions("test_fat32_del_empty.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_del_empty.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/deltest", error);
    QVERIFY(fs->exists("/deltest"));

    // Delete empty directory
    bool success = fs->deleteDirectory("/deltest", false, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);
    QVERIFY(!fs->exists("/deltest"));

    qDebug() << "Successfully deleted empty directory in FAT32";

    QFile::remove("test_fat32_del_empty.img");
}

void TestFAT32AdvancedOperations::testDeleteNonEmptyDirectoryRecursive()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_del_recursive.img");
    QFile::setPermissions("test_fat32_del_recursive.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_del_recursive.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/parent32", error);
    fs->createDirectory("/parent32/child32", error);
    fs->writeFile("/parent32/file32.txt", "test", error);
    fs->writeFile("/parent32/child32/nested32.txt", "nested", error);

    // Delete with recursive flag
    bool success = fs->deleteDirectory("/parent32", true, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);
    QVERIFY(!fs->exists("/parent32"));

    qDebug() << "Successfully deleted directory tree recursively in FAT32";

    QFile::remove("test_fat32_del_recursive.img");
}

void TestFAT32AdvancedOperations::testGetFreeSpace()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_freespace.img");
    QFile::setPermissions("test_fat32_freespace.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_freespace.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    quint32 freeSpace = fs->getFreeSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(freeSpace > 0);

    qDebug() << "FAT32 free space:" << freeSpace << "bytes";

    // Write a file and check free space decreased
    QByteArray largeData(10000, 'X');
    fs->writeFile("/large32.bin", largeData, error);

    quint32 newFreeSpace = fs->getFreeSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(newFreeSpace < freeSpace);

    qDebug() << "FAT32 free space after write:" << newFreeSpace << "bytes";

    QFile::remove("test_fat32_freespace.img");
}

void TestFAT32AdvancedOperations::testGetTotalSpace()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_totalspace.img");
    QFile::setPermissions("test_fat32_totalspace.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_totalspace.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    quint32 totalSpace = fs->getTotalSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(totalSpace > 0);

    quint32 freeSpace = fs->getFreeSpace(error);
    QVERIFY(freeSpace <= totalSpace);

    qDebug() << "FAT32 total space:" << totalSpace << "bytes";
    qDebug() << "FAT32 free space:" << freeSpace << "bytes";
    qDebug() << "FAT32 used space:" << (totalSpace - freeSpace) << "bytes";

    QFile::remove("test_fat32_totalspace.img");
}

QTEST_MAIN(TestFAT32AdvancedOperations)
#include "test_fat32_advanced.moc"
