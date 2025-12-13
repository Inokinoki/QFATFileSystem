#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT32_IMAGE_PATH = "data/fat32.img";

class TestFAT32DirectoryOperations : public QObject
{
    Q_OBJECT
private slots:
    // Directory creation tests
    void testCreateDirectory();

    // Directory deletion tests
    void testDeleteEmptyDirectory();
};

void TestFAT32DirectoryOperations::testCreateDirectory()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_mkdir.img");
    QFile::setPermissions("test_fat32_mkdir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_mkdir.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    bool success = fs->createDirectory("/newdir32", error);

    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/newdir32"));

    QFATFileInfo info = fs->getFileInfo("/newdir32", error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(info.isDirectory);

    qDebug() << "Successfully created directory in FAT32";

    QFile::remove("test_fat32_mkdir.img");
}

void TestFAT32DirectoryOperations::testDeleteEmptyDirectory()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_rmdir.img");
    QFile::setPermissions("test_fat32_rmdir.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_rmdir.img");
    // Already existing directory from the basic image
    QVERIFY(!fs.isNull());

    QFATError error;
    // Use a unique directory name that won't conflict with existing entries
    QString dirName = "/tmpdir32";
    bool success = fs->createDirectory(dirName, error);
    QVERIFY(success);
    QVERIFY(fs->exists(dirName));

    success = fs->deleteFile(dirName, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists(dirName));

    qDebug() << "Successfully deleted empty directory in FAT32";

    QFile::remove("test_fat32_rmdir.img");
}

QTEST_MAIN(TestFAT32DirectoryOperations)
#include "test_fat32_directories.moc"
