#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT12_IMAGE_PATH = "data/fat12.img";

class TestFAT12ReadOperations : public QObject
{
    Q_OBJECT
private slots:
    void testListRootDirectory();
    void testReadFile();
    void testGetFileInfo();
    void testExists();
};

void TestFAT12ReadOperations::testListRootDirectory()
{
    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create(TEST_FAT12_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QList<QFATFileInfo> files = fs->listRootDirectory();
    QVERIFY(!files.isEmpty());

    qDebug() << "Found" << files.size() << "files in FAT12 root directory:";
    for (const QFATFileInfo &file : files) {
        qDebug() << "  -" << file.name << (file.isDirectory ? "(DIR)" : "")
                 << "size:" << file.size;
    }
}

void TestFAT12ReadOperations::testReadFile()
{
    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create(TEST_FAT12_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray data = fs->readFile("/README.TXT", error);

    if (error == QFATError::FileNotFound) {
        qDebug() << "README.TXT not found, skipping test";
        QSKIP("Test file not found in FAT12 image");
    }

    QCOMPARE(error, QFATError::None);
    QVERIFY(!data.isEmpty());

    qDebug() << "Read" << data.size() << "bytes from FAT12 file";
}

void TestFAT12ReadOperations::testGetFileInfo()
{
    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create(TEST_FAT12_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QFATFileInfo info = fs->getFileInfo("/README.TXT", error);

    if (error == QFATError::FileNotFound) {
        qDebug() << "README.TXT not found, skipping test";
        QSKIP("Test file not found in FAT12 image");
    }

    QCOMPARE(error, QFATError::None);
    QVERIFY(!info.name.isEmpty());

    qDebug() << "File info for README.TXT:";
    qDebug() << "  Name:" << info.name;
    qDebug() << "  Size:" << info.size;
    qDebug() << "  IsDir:" << info.isDirectory;
}

void TestFAT12ReadOperations::testExists()
{
    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create(TEST_FAT12_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    // Test that root exists
    QVERIFY(fs->exists("/"));

    // Test non-existent file
    QVERIFY(!fs->exists("/NONEXISTENT.TXT"));
}

QTEST_MAIN(TestFAT12ReadOperations)
#include "test_fat12_read.moc"
