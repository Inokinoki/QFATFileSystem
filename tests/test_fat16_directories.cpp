#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT16_IMAGE_PATH = "data/fat16.img";

class TestFAT16DirectoryOperations : public QObject
{
    Q_OBJECT
private slots:
    // Directory creation tests
    void testCreateDirectory();
    void testCreateNestedDirectory();
    void testCreateDirectoryInvalidParent();

    // Directory deletion tests
    void testDeleteEmptyDirectory();
};

void TestFAT16DirectoryOperations::testCreateDirectory()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_mkdir.img");
    QFile::setPermissions("test_fat16_mkdir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_mkdir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    bool success = fs->createDirectory("/newdir", error);

    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/newdir"));

    QFATFileInfo info = fs->getFileInfo("/newdir", error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(info.isDirectory);

    qDebug() << "Successfully created directory in FAT16";

    QFile::remove("test_fat16_mkdir.img");
}

void TestFAT16DirectoryOperations::testCreateNestedDirectory()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_nested.img");
    QFile::setPermissions("test_fat16_nested.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_nested.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    bool success = fs->createDirectory("/parent", error);
    QVERIFY(success);

    success = fs->createDirectory("/parent/child", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/parent/child"));

    QFATFileInfo info = fs->getFileInfo("/parent/child", error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(info.isDirectory);

    qDebug() << "Successfully created nested directory";

    QFile::remove("test_fat16_nested.img");
}

void TestFAT16DirectoryOperations::testCreateDirectoryInvalidParent()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_invalid_parent.img");
    QFile::setPermissions("test_fat16_invalid_parent.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_invalid_parent.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    bool success = fs->createDirectory("/nonexistent/child", error);

    QVERIFY(!success);
    QCOMPARE(error, QFATError::DirectoryNotFound);

    qDebug() << "Correctly handled invalid parent directory";

    QFile::remove("test_fat16_invalid_parent.img");
}

void TestFAT16DirectoryOperations::testDeleteEmptyDirectory()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_rmdir.img");
    QFile::setPermissions("test_fat16_rmdir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_rmdir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    QString dirName = "/xqz9876";
    bool success = fs->createDirectory(dirName, error);
    QVERIFY(success);
    QVERIFY(fs->exists(dirName));

    success = fs->deleteFile(dirName, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists(dirName));

    qDebug() << "Successfully deleted empty directory in FAT16";

    QFile::remove("test_fat16_rmdir.img");
}

QTEST_MAIN(TestFAT16DirectoryOperations)
#include "test_fat16_directories.moc"
