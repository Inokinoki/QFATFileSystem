#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT12_IMAGE_PATH = "data/fat12.img";

class TestFAT12AdvancedOperations : public QObject
{
    Q_OBJECT
private slots:
    // Filesystem info tests
    void testGetFreeSpace();
    void testGetTotalSpace();
};

void TestFAT12AdvancedOperations::testGetFreeSpace()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_freespace.img");
    QFile::setPermissions("test_fat12_freespace.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_freespace.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    quint32 freeSpace = fs->getFreeSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(freeSpace > 0);

    qDebug() << "FAT12 free space:" << freeSpace << "bytes";

    QFile::remove("test_fat12_freespace.img");
}

void TestFAT12AdvancedOperations::testGetTotalSpace()
{
    QFile::copy(TEST_FAT12_IMAGE_PATH, "test_fat12_totalspace.img");
    QFile::setPermissions("test_fat12_totalspace.img", QFile::ReadUser | QFile::WriteUser);

    QScopedPointer<QFAT12FileSystem> fs = QFAT12FileSystem::create("test_fat12_totalspace.img");
    QVERIFY(!fs.isNull());

    QFATError error;
    quint32 totalSpace = fs->getTotalSpace(error);
    QCOMPARE(error, QFATError::None);
    QVERIFY(totalSpace > 0);

    quint32 freeSpace = fs->getFreeSpace(error);
    QVERIFY(freeSpace <= totalSpace);

    qDebug() << "FAT12 total space:" << totalSpace << "bytes";
    qDebug() << "FAT12 free space:" << freeSpace << "bytes";
    qDebug() << "FAT12 used space:" << (totalSpace - freeSpace) << "bytes";

    QFile::remove("test_fat12_totalspace.img");
}

QTEST_MAIN(TestFAT12AdvancedOperations)
#include "test_fat12_advanced.moc"
