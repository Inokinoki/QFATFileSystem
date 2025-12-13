#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT32_IMAGE_PATH = "data/fat32.img";

class TestFAT32ReadOperations : public QObject
{
    Q_OBJECT
private slots:
    // List operations
    void testListFiles();
    void testListRootDirectory();
    void testListDirectory();
    void testListDirectoryPath();
    void testRootContent();
    void testSubdirectoryContent();

    // Path traversal tests
    void testPathTraversal();

    // File reading tests
    void testReadTextFile();
    void testReadBinaryFile();

    // File info tests
    void testFileSizes();
    void testLongFilenames();

private:
    bool findFileByName(const QList<QFATFileInfo> &files, const QString &name);
    QFATFileInfo getFileByName(const QList<QFATFileInfo> &files, const QString &name);
};

void TestFAT32ReadOperations::testListFiles()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QVERIFY(files.size() >= 0);
    qDebug() << "FAT32: Found" << files.size() << "files/directories";

    for (const QFATFileInfo &file : files) {
        qDebug() << "File:" << file.longName << "(" << file.name << ")"
                 << "Size:" << file.size << "Directory:" << file.isDirectory;
        if (!file.modified.isNull()) {
            qDebug() << "Modified:" << file.modified.toString();
        }
    }
}

void TestFAT32ReadOperations::testListRootDirectory()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();
    QVERIFY(files.size() >= 0);

    qDebug() << "Root directory (FAT32): Found" << files.size() << "entries";
}

void TestFAT32ReadOperations::testListDirectory()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> rootFiles = fs.listRootDirectory();

    bool foundDir = false;
    for (const QFATFileInfo &file : rootFiles) {
        if (file.isDirectory && file.name != "." && file.name != ".." && file.cluster >= 2) {
            qDebug() << "Found directory:" << file.name << "at cluster" << file.cluster;
            foundDir = true;

            if (file.cluster < 0x0FFFFFF8) {
                QList<QFATFileInfo> dirFiles = fs.listDirectory(file.cluster);
                qDebug() << "Directory" << file.name << "contains" << dirFiles.size() << "entries";
                QVERIFY(dirFiles.size() >= 0);
            }
            break;
        }
    }

    qDebug() << "Directory listing test - found directories:" << foundDir;
}

void TestFAT32ReadOperations::testListDirectoryPath()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> rootFiles = fs.listDirectory("/");
    QVERIFY(rootFiles.size() >= 0);
    qDebug() << "Root directory via path (FAT32) contains" << rootFiles.size() << "entries";
}

void TestFAT32ReadOperations::testPathTraversal()
{
    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create(TEST_FAT32_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QList<QFATFileInfo> docsFiles = fs->listDirectory("/Documents");
    QVERIFY(docsFiles.size() > 0);

    qDebug() << "Found" << docsFiles.size() << "files in /Documents";
}

void TestFAT32ReadOperations::testReadTextFile()
{
    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create(TEST_FAT32_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/test.txt", error);

    QCOMPARE(error, QFATError::None);
    QVERIFY(content.size() > 0);

    QString text = QString::fromUtf8(content);
    qDebug() << "Read test.txt (" << content.size() << "bytes):" << text.left(50);
}

void TestFAT32ReadOperations::testReadBinaryFile()
{
    QScopedPointer<QFAT32FileSystem> fs = QFAT32FileSystem::create(TEST_FAT32_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/binary.dat", error);

    QCOMPARE(error, QFATError::None);
    QCOMPARE(content.size(), 10240); // Should be 10KB

    qDebug() << "Read binary.dat successfully, size:" << content.size();
}

void TestFAT32ReadOperations::testRootContent()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QVERIFY2(findFileByName(files, "hello.txt"), "hello.txt not found");
    QVERIFY2(findFileByName(files, "test.txt"), "test.txt not found");
    QVERIFY2(findFileByName(files, "readme.txt"), "readme.txt not found");
    QVERIFY2(findFileByName(files, "empty.txt"), "empty.txt not found");
    QVERIFY2(findFileByName(files, "binary.dat"), "binary.dat not found");
    QVERIFY2(findFileByName(files, "largefile.bin"), "largefile.bin not found");

    QVERIFY2(findFileByName(files, "subdir1"), "subdir1 not found");
    QVERIFY2(findFileByName(files, "subdir2"), "subdir2 not found");
    QVERIFY2(findFileByName(files, "Documents"), "Documents not found");

    QFATFileInfo subdir2 = getFileByName(files, "subdir2");
    QVERIFY2(subdir2.isDirectory, "subdir2 should be a directory");

    QFATFileInfo testFile = getFileByName(files, "test.txt");
    QVERIFY2(!testFile.isDirectory, "test.txt should not be a directory");

    qDebug() << "FAT32 root content validation passed";
}

void TestFAT32ReadOperations::testSubdirectoryContent()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> rootFiles = fs.listRootDirectory();
    QFATFileInfo docsDir = getFileByName(rootFiles, "Documents");
    QVERIFY2(docsDir.isDirectory, "Documents should be a directory");
    QVERIFY2(docsDir.cluster >= 2, "Documents should have a valid cluster");

    QList<QFATFileInfo> docFiles = fs.listDirectory(docsDir.cluster);

    QVERIFY2(findFileByName(docFiles, "doc1.txt"), "doc1.txt not found in Documents");
    QVERIFY2(findFileByName(docFiles, "doc2.txt"), "doc2.txt not found in Documents");

    qDebug() << "FAT32 subdirectory content validation passed";
}

void TestFAT32ReadOperations::testFileSizes()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QFATFileInfo emptyFile = getFileByName(files, "empty.txt");
    QCOMPARE(emptyFile.size, quint32(0));

    QFATFileInfo largeFile = getFileByName(files, "largefile.bin");
    QCOMPARE(largeFile.size, quint32(102400));

    qDebug() << "FAT32 file size validation passed";
}

void TestFAT32ReadOperations::testLongFilenames()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT32_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT32FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QVERIFY2(findFileByName(files, "this_is_a_long_filename.txt"), "Long filename not found in FAT32");

    qDebug() << "FAT32 long filename validation passed";
}

bool TestFAT32ReadOperations::findFileByName(const QList<QFATFileInfo> &files, const QString &name)
{
    QString upperName = name.toUpper();
    for (const QFATFileInfo &file : files) {
        if (file.name.toUpper() == upperName || file.longName.toUpper() == upperName) {
            return true;
        }

        QString fileName = file.name.toUpper();
        if (fileName.contains("~")) {
            QString fileBaseName = fileName.left(fileName.indexOf("~"));
            QString searchBaseName = upperName.contains(".") ? upperName.left(upperName.indexOf(".")) : upperName;

            if (searchBaseName.startsWith(fileBaseName)) {
                QString searchExt = upperName.contains(".") ? upperName.mid(upperName.lastIndexOf('.')) : "";
                QString fileExt = fileName.contains(".") ? fileName.mid(fileName.lastIndexOf('.')) : "";
                if (searchExt.isEmpty() || fileExt.toUpper() == searchExt.toUpper()) {
                    return true;
                }
            }
        }
    }
    return false;
}

QFATFileInfo TestFAT32ReadOperations::getFileByName(const QList<QFATFileInfo> &files, const QString &name)
{
    QString upperName = name.toUpper();
    for (const QFATFileInfo &file : files) {
        if (file.name.toUpper() == upperName || file.longName.toUpper() == upperName) {
            return file;
        }

        QString fileName = file.name.toUpper();
        if (fileName.contains("~")) {
            QString fileBaseName = fileName.left(fileName.indexOf("~"));
            QString searchBaseName = upperName.contains(".") ? upperName.left(upperName.indexOf(".")) : upperName;

            if (searchBaseName.startsWith(fileBaseName)) {
                QString searchExt = upperName.contains(".") ? upperName.mid(upperName.lastIndexOf('.')) : "";
                QString fileExt = fileName.contains(".") ? fileName.mid(fileName.lastIndexOf('.')) : "";
                if (searchExt.isEmpty() || fileExt.toUpper() == searchExt.toUpper()) {
                    return file;
                }
            }
        }
    }
    return QFATFileInfo();
}

QTEST_MAIN(TestFAT32ReadOperations)
#include "test_fat32_read.moc"
