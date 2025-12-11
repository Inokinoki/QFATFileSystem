#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT16_IMAGE_PATH = "data/fat16.img";
const QString TEST_FAT32_IMAGE_PATH = "data/fat32.img";

class TestCommonOperations : public QObject
{
    Q_OBJECT
private slots:
    // Smart pointer tests
    void testSmartPointerMemoryManagement();

    // Factory method tests
    void testFactoryMethodFAT16();
    void testFactoryMethodFAT32();

    // File info structure tests
    void testFileInfoStructure();
};

void TestCommonOperations::testSmartPointerMemoryManagement()
{
    // Test that smart pointers properly manage memory
    {
        QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
        QVERIFY(file->open(QIODevice::ReadOnly));

        QSharedPointer<QIODevice> device = file;
        QFAT16FileSystem fs(device);

        QList<QFATFileInfo> files = fs.listRootDirectory();
        QVERIFY(files.size() > 0);
    } // file and device should be automatically cleaned up here

    // Verify no memory leaks by creating and destroying multiple instances
    for (int i = 0; i < 100; i++) {
        QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
        if (file->open(QIODevice::ReadOnly)) {
            QFAT16FileSystem fs(file);
            fs.listRootDirectory();
        }
    }

    qDebug() << "Smart pointer memory management test passed";
}

void TestCommonOperations::testFactoryMethodFAT16()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QList<QFATFileInfo> files = fs->listRootDirectory();
    QVERIFY(files.size() > 0);

    qDebug() << "FAT16 factory method created filesystem successfully";
}

void TestCommonOperations::testFactoryMethodFAT32()
{
    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create(TEST_FAT32_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QList<QFATFileInfo> files = fs->listRootDirectory();
    QVERIFY(files.size() > 0);

    qDebug() << "FAT32 factory method created filesystem successfully";
}

void TestCommonOperations::testFileInfoStructure()
{
    // Test that QFATFileInfo structure initializes correctly
    QFATFileInfo info;

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

QTEST_MAIN(TestCommonOperations)
#include "test_common.moc"
