#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT32_IMAGE_PATH = "data/fat32.img";

class TestFAT32WriteOperations : public QObject
{
    Q_OBJECT
private slots:
    // File writing tests
    void testWriteNewFile();

    // File deletion tests
    void testDeleteFile();
};

void TestFAT32WriteOperations::testWriteNewFile()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_write.img");
    QFile::setPermissions("test_fat32_write.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_write.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "Hello from FAT32 write test!";
    QFATError error;
    bool success = fs->writeFile("/newfile32.txt", testData, error);

    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/newfile32.txt"));

    QByteArray readData = fs->readFile("/newfile32.txt", error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully wrote and read new file in FAT32";

    QFile::remove("test_fat32_write.img");
}

void TestFAT32WriteOperations::testDeleteFile()
{
    QFile::copy(TEST_FAT32_IMAGE_PATH, "test_fat32_delete.img");
    QFile::setPermissions("test_fat32_delete.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create("test_fat32_delete.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "File to be deleted in FAT32";
    QFATError error;
    bool success = fs->writeFile("/deleteme32.txt", testData, error);
    QVERIFY(success);
    QVERIFY(fs->exists("/deleteme32.txt"));

    success = fs->deleteFile("/deleteme32.txt", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/deleteme32.txt"));

    qDebug() << "Successfully deleted file in FAT32";

    QFile::remove("test_fat32_delete.img");
}

QTEST_MAIN(TestFAT32WriteOperations)
#include "test_fat32_write.moc"
