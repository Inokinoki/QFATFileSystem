#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT16_IMAGE_PATH = "data/fat16.img";

class TestFAT16AdvancedOperations : public QObject
{
    Q_OBJECT
private slots:
    // Partial read tests
    void testPartialRead();
    void testPartialReadWithOffset();
    void testPartialReadBeyondFile();

    // Rename tests
    void testRenameFile();
    void testRenameFileAlreadyExists();
    void testRenameDirectory();

    // Move tests
    void testMoveFile();
    void testMoveDirectory();
    void testMoveToNonExistentDirectory();

    // Recursive directory deletion tests
    void testDeleteEmptyDirectoryNonRecursive();
    void testDeleteNonEmptyDirectoryRecursive();
    void testDeleteNonEmptyDirectoryNonRecursive();

    // Filesystem info tests
    void testGetFreeSpace();
    void testGetTotalSpace();
};

void TestFAT16AdvancedOperations::testPartialRead()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_partial.img");
    QFile::setPermissions("test_fat16_partial.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_partial.img");
    QVERIFY(!fs.isNull());

    // Write a test file
    QByteArray testData = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QFATError error;
    bool success = fs->writeFile("/partial.txt", testData, error);
    QVERIFY(success);

    // Read partial data from beginning
    QByteArray partial = fs->readFilePartial("/partial.txt", 0, 10, error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(partial, QByteArray("0123456789"));

    qDebug() << "Successfully read partial data from beginning";

    QFile::remove("test_fat16_partial.img");
}

void TestFAT16AdvancedOperations::testPartialReadWithOffset()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_partial_offset.img");
    QFile::setPermissions("test_fat16_partial_offset.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_partial_offset.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QFATError error;
    fs->writeFile("/offset.txt", testData, error);

    // Read from middle
    QByteArray partial = fs->readFilePartial("/offset.txt", 10, 16, error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(partial, QByteArray("ABCDEFGHIJKLMNOP"));

    qDebug() << "Successfully read partial data with offset";

    QFile::remove("test_fat16_partial_offset.img");
}

void TestFAT16AdvancedOperations::testPartialReadBeyondFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_partial_beyond.img");
    QFile::setPermissions("test_fat16_partial_beyond.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_partial_beyond.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "Short";
    QFATError error;
    fs->writeFile("/short.txt", testData, error);

    // Try to read beyond file size - should return available data
    QByteArray partial = fs->readFilePartial("/short.txt", 0, 100, error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(partial.left(5), testData);

    qDebug() << "Correctly handled read beyond file size";

    QFile::remove("test_fat16_partial_beyond.img");
}

void TestFAT16AdvancedOperations::testRenameFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_rename.img");
    QFile::setPermissions("test_fat16_rename.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_rename.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "Content to rename";
    QFATError error;
    fs->writeFile("/oldname.txt", testData, error);
    QVERIFY(fs->exists("/oldname.txt"));

    // Rename the file
    bool success = fs->renameFile("/oldname.txt", "/newname.txt", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    // Verify old name doesn't exist and new name does
    QVERIFY(!fs->exists("/oldname.txt"));
    QVERIFY(fs->exists("/newname.txt"));

    // Verify content is preserved
    QByteArray readData = fs->readFile("/newname.txt", error);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully renamed file";

    QFile::remove("test_fat16_rename.img");
}

void TestFAT16AdvancedOperations::testRenameFileAlreadyExists()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_rename_exists.img");
    QFile::setPermissions("test_fat16_rename_exists.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_rename_exists.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->writeFile("/file1.txt", "File 1", error);
    fs->writeFile("/file2.txt", "File 2", error);

    // Try to rename to existing file - should fail
    bool success = fs->renameFile("/file1.txt", "/file2.txt", error);
    QVERIFY(!success);
    QCOMPARE(error, QFATError::InvalidPath);

    qDebug() << "Correctly prevented renaming to existing file";

    QFile::remove("test_fat16_rename_exists.img");
}

void TestFAT16AdvancedOperations::testRenameDirectory()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_rename_dir.img");
    QFile::setPermissions("test_fat16_rename_dir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_rename_dir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/olddir", error);
    QVERIFY(fs->exists("/olddir"));

    // Rename the directory
    bool success = fs->renameFile("/olddir", "/newdir", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/olddir"));
    QVERIFY(fs->exists("/newdir"));

    qDebug() << "Successfully renamed directory";

    QFile::remove("test_fat16_rename_dir.img");
}

void TestFAT16AdvancedOperations::testMoveFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_move.img");
    QFile::setPermissions("test_fat16_move.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_move.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/source", error);
    fs->createDirectory("/dest", error);

    QByteArray testData = "Moving file";
    fs->writeFile("/source/file.txt", testData, error);
    QVERIFY(fs->exists("/source/file.txt"));

    // Move the file
    bool success = fs->moveFile("/source/file.txt", "/dest/file.txt", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    // Verify moved
    QVERIFY(!fs->exists("/source/file.txt"));
    QVERIFY(fs->exists("/dest/file.txt"));

    // Verify content
    QByteArray readData = fs->readFile("/dest/file.txt", error);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully moved file";

    QFile::remove("test_fat16_move.img");
}

void TestFAT16AdvancedOperations::testMoveDirectory()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_move_dir.img");
    QFile::setPermissions("test_fat16_move_dir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_move_dir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/source", error);
    fs->createDirectory("/dest", error);
    fs->createDirectory("/source/moveme", error);

    // Move the directory
    bool success = fs->moveFile("/source/moveme", "/dest/moveme", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/source/moveme"));
    QVERIFY(fs->exists("/dest/moveme"));

    qDebug() << "Successfully moved directory";

    QFile::remove("test_fat16_move_dir.img");
}

void TestFAT16AdvancedOperations::testMoveToNonExistentDirectory()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_move_invalid.img");
    QFile::setPermissions("test_fat16_move_invalid.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_move_invalid.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->writeFile("/file.txt", "Test", error);

    // Try to move to non-existent directory
    bool success = fs->moveFile("/file.txt", "/nonexistent/file.txt", error);
    QVERIFY(!success);
    QCOMPARE(error, QFATError::DirectoryNotFound);

    qDebug() << "Correctly prevented move to non-existent directory";

    QFile::remove("test_fat16_move_invalid.img");
}

void TestFAT16AdvancedOperations::testDeleteEmptyDirectoryNonRecursive()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_del_empty.img");
    QFile::setPermissions("test_fat16_del_empty.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_del_empty.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/emptydir", error);
    QVERIFY(fs->exists("/emptydir"));

    // Delete empty directory without recursive flag
    bool success = fs->deleteDirectory("/emptydir", false, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);
    QVERIFY(!fs->exists("/emptydir"));

    qDebug() << "Successfully deleted empty directory";

    QFile::remove("test_fat16_del_empty.img");
}

void TestFAT16AdvancedOperations::testDeleteNonEmptyDirectoryRecursive()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_del_recursive.img");
    QFile::setPermissions("test_fat16_del_recursive.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_del_recursive.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/parent", error);
    fs->createDirectory("/parent/child", error);
    fs->writeFile("/parent/file.txt", "test", error);
    fs->writeFile("/parent/child/nested.txt", "nested", error);

    // Delete with recursive flag
    bool success = fs->deleteDirectory("/parent", true, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);
    QVERIFY(!fs->exists("/parent"));

    qDebug() << "Successfully deleted directory tree recursively";

    QFile::remove("test_fat16_del_recursive.img");
}

void TestFAT16AdvancedOperations::testDeleteNonEmptyDirectoryNonRecursive()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_del_nonempty.img");
    QFile::setPermissions("test_fat16_del_nonempty.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_del_nonempty.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/nonempty", error);
    fs->writeFile("/nonempty/file.txt", "test", error);

    // Try to delete without recursive - should fail
    bool success = fs->deleteDirectory("/nonempty", false, error);
    QVERIFY(!success);
    QCOMPARE(error, QFATError::InvalidPath);
    QVERIFY(fs->exists("/nonempty"));

    qDebug() << "Correctly prevented deletion of non-empty directory";

    QFile::remove("test_fat16_del_nonempty.img");
}

void TestFAT16AdvancedOperations::testGetFreeSpace()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_freespace.img");
    QFile::setPermissions("test_fat16_freespace.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_freespace.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    quint32 freeSpace = fs->getFreeSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(freeSpace > 0);

    qDebug() << "Free space:" << freeSpace << "bytes";

    // Write a file and check free space decreased
    QByteArray largeData(10000, 'X');
    fs->writeFile("/large.bin", largeData, error);

    quint32 newFreeSpace = fs->getFreeSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(newFreeSpace < freeSpace);

    qDebug() << "Free space after write:" << newFreeSpace << "bytes";

    QFile::remove("test_fat16_freespace.img");
}

void TestFAT16AdvancedOperations::testGetTotalSpace()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_totalspace.img");
    QFile::setPermissions("test_fat16_totalspace.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_totalspace.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    quint32 totalSpace = fs->getTotalSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(totalSpace > 0);

    quint32 freeSpace = fs->getFreeSpace(error);
    QVERIFY(freeSpace <= totalSpace);

    qDebug() << "Total space:" << totalSpace << "bytes";
    qDebug() << "Free space:" << freeSpace << "bytes";
    qDebug() << "Used space:" << (totalSpace - freeSpace) << "bytes";

    QFile::remove("test_fat16_totalspace.img");
}

QTEST_MAIN(TestFAT16AdvancedOperations)
#include "test_fat16_advanced.moc"
