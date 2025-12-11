#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT16_IMAGE_PATH = "data/fat16.img";

class TestFAT16WriteOperations : public QObject
{
    Q_OBJECT
private slots:
    // File writing tests
    void testWriteNewFile();
    void testOverwriteExistingFile();
    void testWriteEmptyFile();
    void testWriteLargeFile();
    void testWriteReadCycle();

    // File deletion tests
    void testDeleteFile();
    void testDeleteNonExistentFile();
};

void TestFAT16WriteOperations::testWriteNewFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_write.img");
    QFile::setPermissions("test_fat16_write.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_write.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "Hello from write test!";
    QFATError error;
    bool success = fs->writeFile("/newfile.txt", testData, error);

    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/newfile.txt"));

    QByteArray readData = fs->readFile("/newfile.txt", error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(readData.left(testData.size()), testData);

    qDebug() << "Successfully wrote and read new file in FAT16";

    QFile::remove("test_fat16_write.img");
}

void TestFAT16WriteOperations::testOverwriteExistingFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_overwrite.img");
    QFile::setPermissions("test_fat16_overwrite.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_overwrite.img");
    QVERIFY(!fs.isNull());

    QByteArray initialData = "Initial content";
    QFATError error;
    bool success = fs->writeFile("/overwrite.txt", initialData, error);
    QVERIFY(success);

    QByteArray newData = "Updated content that is longer";
    success = fs->writeFile("/overwrite.txt", newData, error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QByteArray readData = fs->readFile("/overwrite.txt", error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(readData.left(newData.size()), newData);

    qDebug() << "Successfully overwrote existing file";

    QFile::remove("test_fat16_overwrite.img");
}

void TestFAT16WriteOperations::testWriteEmptyFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_empty.img");
    QFile::setPermissions("test_fat16_empty.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_empty.img");
    QVERIFY(!fs.isNull());

    QByteArray emptyData;
    QFATError error;
    bool success = fs->writeFile("/empty_new.txt", emptyData, error);

    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/empty_new.txt"));

    qDebug() << "Successfully wrote empty file";

    QFile::remove("test_fat16_empty.img");
}

void TestFAT16WriteOperations::testWriteLargeFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_large.img");
    QFile::setPermissions("test_fat16_large.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_large.img");
    QVERIFY(!fs.isNull());

    QByteArray largeData;
    for (int i = 0; i < 20480; i++) {
        largeData.append(static_cast<char>(i % 256));
    }

    QFATError error;
    bool success = fs->writeFile("/large_write.bin", largeData, error);

    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(fs->exists("/large_write.bin"));

    QByteArray readData = fs->readFile("/large_write.bin", error);
    QCOMPARE(error, QFATError::None);
    QCOMPARE(readData.size(), largeData.size());
    QCOMPARE(readData, largeData);

    qDebug() << "Successfully wrote and verified large file (" << largeData.size() << "bytes)";

    QFile::remove("test_fat16_large.img");
}

void TestFAT16WriteOperations::testWriteReadCycle()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_cycle.img");
    QFile::setPermissions("test_fat16_cycle.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_cycle.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    for (int i = 0; i < 5; i++) {
        QString fileName = QString("/testfile%1.txt").arg(i);
        QByteArray data = QString("Test content for file %1").arg(i).toUtf8();

        bool success = fs->writeFile(fileName, data, error);
        QVERIFY(success);
        QCOMPARE(error, QFATError::None);
    }

    for (int i = 0; i < 5; i++) {
        QString fileName = QString("/testfile%1.txt").arg(i);
        QVERIFY(fs->exists(fileName));

        QByteArray expectedData = QString("Test content for file %1").arg(i).toUtf8();
        QByteArray readData = fs->readFile(fileName, error);
        QCOMPARE(error, QFATError::None);
        QCOMPARE(readData.left(expectedData.size()), expectedData);
    }

    qDebug() << "Successfully completed write-read cycle for 5 files";

    QFile::remove("test_fat16_cycle.img");
}

void TestFAT16WriteOperations::testDeleteFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_delete.img");
    QFile::setPermissions("test_fat16_delete.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_delete.img");
    QVERIFY(!fs.isNull());

    QByteArray testData = "File to be deleted";
    QFATError error;
    bool success = fs->writeFile("/deleteme.txt", testData, error);
    QVERIFY(success);
    QVERIFY(fs->exists("/deleteme.txt"));

    success = fs->deleteFile("/deleteme.txt", error);
    QVERIFY(success);
    QCOMPARE(error, QFATError::None);

    QVERIFY(!fs->exists("/deleteme.txt"));

    qDebug() << "Successfully deleted file in FAT16";

    QFile::remove("test_fat16_delete.img");
}

void TestFAT16WriteOperations::testDeleteNonExistentFile()
{
    QFile::copy(TEST_FAT16_IMAGE_PATH, "test_fat16_delete_nonexist.img");
    QFile::setPermissions("test_fat16_delete_nonexist.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create("test_fat16_delete_nonexist.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    bool success = fs->deleteFile("/nonexistent_file.txt", error);

    QVERIFY(!success);
    QCOMPARE(error, QFATError::FileNotFound);

    qDebug() << "Correctly handled deletion of non-existent file";

    QFile::remove("test_fat16_delete_nonexist.img");
}

QTEST_MAIN(TestFAT16WriteOperations)
#include "test_fat16_write.moc"
