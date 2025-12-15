#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT12_IMAGE_PATH = "data/fat12.img";

class TestFAT12DirectoryOperations : public QObject
{
    Q_OBJECT
private slots:
    void testCreateDirectory();
    void testCreateNestedDirectory();
    void testDeleteEmptyDirectory();
    void testDeleteNonEmptyDirectoryRecursive();
    void testRenameFile();
    void testRenameDirectory();
    void testMoveFile();
};

void TestFAT12DirectoryOperations::testCreateDirectory()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_mkdir.img");
    QFile::setPermissions("test_fat12_mkdir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_mkdir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    bool success = fs->createDirectory("/NEWDIR", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/NEWDIR"));

    QFATFileInfo info = fs->getFileInfo("/NEWDIR", error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(info.isDirectory);

    qDebug() << "Successfully created directory in FAT12";

    QFile::remove("test_fat12_mkdir.img");
}

void TestFAT12DirectoryOperations::testCreateNestedDirectory()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_nested.img");
    QFile::setPermissions("test_fat12_nested.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_nested.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/PARENT", error);
    QCOMPARE(error, QFATError::None);

    bool success = fs->createDirectory("/PARENT/CHILD", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/PARENT/CHILD"));

    qDebug() << "Successfully created nested directory in FAT12";

    QFile::remove("test_fat12_nested.img");
}

void TestFAT12DirectoryOperations::testDeleteEmptyDirectory()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_rmdir.img");
    QFile::setPermissions("test_fat12_rmdir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_rmdir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/DELME", error);
    QVERIFY(fs->exists("/DELME"));

    bool success = fs->deleteDirectory("/DELME", false, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/DELME"));

    qDebug() << "Successfully deleted empty directory in FAT12";

    QFile::remove("test_fat12_rmdir.img");
}

void TestFAT12DirectoryOperations::testDeleteNonEmptyDirectoryRecursive()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_recursive.img");
    QFile::setPermissions("test_fat12_recursive.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_recursive.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/TREE", error);
    fs->createDirectory("/TREE/SUB", error);
    fs->writeFile("/TREE/FILE.TXT", "test", error);
    fs->writeFile("/TREE/SUB/NESTED.TXT", "nested", error);

    bool success = fs->deleteDirectory("/TREE", true, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/TREE"));

    qDebug() << "Successfully deleted directory tree recursively in FAT12";

    QFile::remove("test_fat12_recursive.img");
}

void TestFAT12DirectoryOperations::testRenameFile()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_rename.img");
    QFile::setPermissions("test_fat12_rename.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_rename.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray testData = "Rename test";
    fs->writeFile("/OLD.TXT", testData, error);
    QVERIFY(fs->exists("/OLD.TXT"));

    bool success = fs->renameFile("/OLD.TXT", "/NEW.TXT", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/OLD.TXT"));
    QVERIFY(fs->exists("/NEW.TXT"));

    // Verify content
    QByteArray readData = fs->readFile("/NEW.TXT", error);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully renamed file in FAT12";

    QFile::remove("test_fat12_rename.img");
}

void TestFAT12DirectoryOperations::testRenameDirectory()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_renamedir.img");
    QFile::setPermissions("test_fat12_renamedir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_renamedir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/OLDDIR", error);
    QVERIFY(fs->exists("/OLDDIR"));

    bool success = fs->renameFile("/OLDDIR", "/NEWDIR", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/OLDDIR"));
    QVERIFY(fs->exists("/NEWDIR"));

    qDebug() << "Successfully renamed directory in FAT12";

    QFile::remove("test_fat12_renamedir.img");
}

void TestFAT12DirectoryOperations::testMoveFile()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_move.img");
    QFile::setPermissions("test_fat12_move.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_move.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->createDirectory("/SRC", error);
    fs->createDirectory("/DST", error);

    QByteArray testData = "Move test data";
    fs->writeFile("/SRC/FILE.TXT", testData, error);
    QVERIFY(fs->exists("/SRC/FILE.TXT"));

    bool success = fs->moveFile("/SRC/FILE.TXT", "/DST/FILE.TXT", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/SRC/FILE.TXT"));
    QVERIFY(fs->exists("/DST/FILE.TXT"));

    // Verify content
    QByteArray readData = fs->readFile("/DST/FILE.TXT", error);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully moved file in FAT12";

    QFile::remove("test_fat12_move.img");
}

QTEST_MAIN(TestFAT12DirectoryOperations)
#include "test_fat12_directories.moc"
