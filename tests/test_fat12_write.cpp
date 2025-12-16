#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT12_IMAGE_PATH = "data/fat12.img";

class TestFAT12WriteOperations : public QObject
{
    Q_OBJECT
private slots:
    void testWriteSmallFile();
    void testOverwriteFile();
    void testWriteEmptyFile();
    void testDeleteFile();
};

void TestFAT12WriteOperations::testWriteSmallFile()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_write.img");
    QFile::setPermissions("test_fat12_write.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_write.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "Hello, FAT12 World!";
    QFATError error;
    bool success = fs->writeFile("/TEST.TXT", testData, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    // Read back and verify
    QByteArray readData = fs->readFile("/TEST.TXT", error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully wrote and read small file in FAT12";

    QFile::remove("test_fat12_write.img");
}

void TestFAT12WriteOperations::testOverwriteFile()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_overwrite.img");
    QFile::setPermissions("test_fat12_overwrite.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_overwrite.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray data1 = "First content";
    fs->writeFile("/OVER.TXT", data1, error);
    QCOMPARE(error, QFATError::None);

    // Overwrite with different content
    QByteArray data2 = "Second content - different length!";
    bool success = fs->writeFile("/OVER.TXT", data2, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    // Verify new content
    QByteArray readData = fs->readFile("/OVER.TXT", error);
    QCOMPARE(readData.left(data2.size()), data2);

    qDebug() << "Successfully overwrote file in FAT12";

    QFile::remove("test_fat12_overwrite.img");
}

void TestFAT12WriteOperations::testWriteEmptyFile()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_empty.img");
    QFile::setPermissions("test_fat12_empty.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_empty.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray emptyData;
    bool success = fs->writeFile("/EMPTY.TXT", emptyData, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/EMPTY.TXT"));

    QFATFileInfo info = fs->getFileInfo("/EMPTY.TXT", error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(info.size, 0U);

    qDebug() << "Successfully wrote empty file in FAT12";

    QFile::remove("test_fat12_empty.img");
}

void TestFAT12WriteOperations::testDeleteFile()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_delete.img");
    QFile::setPermissions("test_fat12_delete.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_delete.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->writeFile("/DEL.TXT", "To be deleted", error);
    QVERIFY(fs->exists("/DEL.TXT"));

    // Delete the file
    bool success = fs->deleteFile("/DEL.TXT", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/DEL.TXT"));

    qDebug() << "Successfully deleted file in FAT12";

    QFile::remove("test_fat12_delete.img");
}

QTEST_MAIN(TestFAT12WriteOperations)
#include "test_fat12_write.moc"
