#include "../qfatfilesystem.h"
#include <QDebug>
#include <QtTest/QtTest>

const QString TEST_FAT16_IMAGE_PATH = "data/fat16.img";

class TestFAT16ReadOperations : public QObject
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
    void testInvalidPathTraversal();

    // File reading tests
    void testReadTextFile();
    void testReadBinaryFile();
    void testReadEmptyFile();
    void testReadLargeFile();
    void testReadFileFromSubdirectory();

    // File existence tests
    void testFileExists();
    void testFileNotExists();
    void testDirectoryExists();

    // File info tests
    void testGetFileInfo();
    void testGetDirectoryInfo();
    void testFileSizes();
    void testLongFilenames();

    // Error handling tests
    void testReadNonExistentFile();
    void testReadDirectoryAsFile();
    void testErrorStringMessages();

    // Cluster chain tests
    void testClusterChainReading();

private:
    bool findFileByName(const QList<QFATFileInfo> &files, const QString &name);
    QFATFileInfo getFileByName(const QList<QFATFileInfo> &files, const QString &name);
};

void TestFAT16ReadOperations::testListFiles()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QVERIFY(files.size() >= 0);
    qDebug() << "FAT16: Found" << files.size() << "files/directories";

    for (const QFATFileInfo &file : files) {
        qDebug() << "File:" << file.longName << "(" << file.name << ")"
                 << "Size:" << file.size << "Directory:" << file.isDirectory;
        if (!file.modified.isNull()) {
            qDebug() << "Modified:" << file.modified.toString();
        }
    }
}

void TestFAT16ReadOperations::testListRootDirectory()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();
    QVERIFY(files.size() >= 0);

    qDebug() << "Root directory (FAT16): Found" << files.size() << "entries";
}

void TestFAT16ReadOperations::testListDirectory()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> rootFiles = fs.listRootDirectory();

    bool foundDir = false;
    for (const QFATFileInfo &file : rootFiles) {
        if (file.isDirectory && file.name != "." && file.name != ".." && file.cluster >= 2) {
            qDebug() << "Found directory:" << file.name << "at cluster" << file.cluster;
            foundDir = true;

            if (file.cluster < 0xFFF8) {
                QList<QFATFileInfo> dirFiles = fs.listDirectory(static_cast<quint16>(file.cluster));
                qDebug() << "Directory" << file.name << "contains" << dirFiles.size() << "entries";
                QVERIFY(dirFiles.size() >= 0);
            }
            break;
        }
    }

    qDebug() << "Directory listing test - found directories:" << foundDir;
}

void TestFAT16ReadOperations::testListDirectoryPath()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> rootFiles = fs.listDirectory("/");
    QVERIFY(rootFiles.size() >= 0);
    qDebug() << "Root directory via path contains" << rootFiles.size() << "entries";
}

void TestFAT16ReadOperations::testPathTraversal()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QList<QFATFileInfo> subdir1Files = fs->listDirectory("/subdir1");
    QVERIFY(subdir1Files.size() > 0);

    qDebug() << "Found" << subdir1Files.size() << "files in /subdir1";

    QList<QFATFileInfo> nestedFiles = fs->listDirectory("/subdir1/nested");
    qDebug() << "Found" << nestedFiles.size() << "files in /subdir1/nested";
}

void TestFAT16ReadOperations::testInvalidPathTraversal()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QList<QFATFileInfo> files = fs->listDirectory("/nonexistent/path");
    QVERIFY(files.size() == 0);

    qDebug() << "Invalid path correctly returned empty list";
}

void TestFAT16ReadOperations::testReadTextFile()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/hello.txt", error);

    QCOMPARE(error, QFATError::None);
    QVERIFY(content.size() > 0);

    QString text = QString::fromUtf8(content);
    qDebug() << "Read hello.txt (" << content.size() << "bytes):" << text.left(50);
}

void TestFAT16ReadOperations::testReadBinaryFile()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/binary.dat", error);

    QCOMPARE(error, QFATError::None);
    QCOMPARE(content.size(), 10240); // Should be 10KB

    qDebug() << "Read binary.dat successfully, size:" << content.size();
}

void TestFAT16ReadOperations::testReadEmptyFile()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/empty.txt", error);

    QCOMPARE(error, QFATError::None);
    QCOMPARE(content.size(), 0);

    qDebug() << "Empty file correctly returned 0 bytes";
}

void TestFAT16ReadOperations::testReadLargeFile()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/largefile.bin", error);

    QCOMPARE(error, QFATError::None);
    QCOMPARE(content.size(), 102400); // Should be 100KB

    qDebug() << "Read largefile.bin successfully, size:" << content.size();
}

void TestFAT16ReadOperations::testReadFileFromSubdirectory()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/subdir1/file1.txt", error);

    QCOMPARE(error, QFATError::None);
    QVERIFY(content.size() > 0);

    QString text = QString::fromUtf8(content);
    qDebug() << "Read /subdir1/file1.txt (" << content.size() << "bytes):" << text.left(50);
}

void TestFAT16ReadOperations::testFileExists()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QVERIFY(fs->exists("/hello.txt"));
    QVERIFY(fs->exists("/test.txt"));
    QVERIFY(fs->exists("/binary.dat"));

    qDebug() << "File existence checks passed";
}

void TestFAT16ReadOperations::testFileNotExists()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QVERIFY(!fs->exists("/nonexistent.txt"));
    QVERIFY(!fs->exists("/subdir1/nonexistent.txt"));

    qDebug() << "Non-existent file checks passed";
}

void TestFAT16ReadOperations::testDirectoryExists()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QVERIFY(fs->exists("/subdir1"));
    QVERIFY(fs->exists("/subdir2"));
    QVERIFY(fs->exists("/Documents"));

    qDebug() << "Directory existence checks passed";
}

void TestFAT16ReadOperations::testGetFileInfo()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QFATFileInfo info = fs->getFileInfo("/hello.txt", error);

    QCOMPARE(error, QFATError::None);
    QVERIFY(!info.name.isEmpty());
    QVERIFY(!info.isDirectory);
    QVERIFY(info.size > 0);

    qDebug() << "File info:" << info.longName << "Size:" << info.size;
}

void TestFAT16ReadOperations::testGetDirectoryInfo()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QFATFileInfo info = fs->getFileInfo("/subdir1", error);

    QCOMPARE(error, QFATError::None);
    QVERIFY(!info.name.isEmpty());
    QVERIFY(info.isDirectory);

    qDebug() << "Directory info:" << info.longName << "Is directory:" << info.isDirectory;
}

void TestFAT16ReadOperations::testReadNonExistentFile()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/nonexistent.txt", error);

    QCOMPARE(error, QFATError::FileNotFound);
    QCOMPARE(content.size(), 0);
    QCOMPARE(fs->lastError(), QFATError::FileNotFound);

    qDebug() << "Non-existent file error handling passed";
}

void TestFAT16ReadOperations::testReadDirectoryAsFile()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/subdir1", error);

    QCOMPARE(error, QFATError::InvalidPath);
    QCOMPARE(content.size(), 0);

    qDebug() << "Reading directory as file error handling passed";
}

void TestFAT16ReadOperations::testErrorStringMessages()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    fs->readFile("/nonexistent.txt", error);

    QString errorMsg = fs->errorString();
    QVERIFY(!errorMsg.isEmpty());
    QVERIFY(errorMsg.contains("not found", Qt::CaseInsensitive));

    qDebug() << "Error message:" << errorMsg;
}

void TestFAT16ReadOperations::testClusterChainReading()
{
    QScopedPointer<QFAT16FileSystem> fs = QFAT16FileSystem::create(TEST_FAT16_IMAGE_PATH);
    QVERIFY(!fs.isNull());

    QFATError error;
    QByteArray content = fs->readFile("/largefile.bin", error);

    QCOMPARE(error, QFATError::None);
    QCOMPARE(content.size(), 102400); // 100KB

    qDebug() << "Cluster chain reading test passed for 100KB file";
}

void TestFAT16ReadOperations::testRootContent()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

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

    QFATFileInfo subdir1 = getFileByName(files, "subdir1");
    QVERIFY2(subdir1.isDirectory, "subdir1 should be a directory");

    QFATFileInfo helloFile = getFileByName(files, "hello.txt");
    QVERIFY2(!helloFile.isDirectory, "hello.txt should not be a directory");

    qDebug() << "FAT16 root content validation passed";
}

void TestFAT16ReadOperations::testSubdirectoryContent()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> rootFiles = fs.listRootDirectory();
    QFATFileInfo subdir1 = getFileByName(rootFiles, "subdir1");
    QVERIFY2(subdir1.isDirectory, "subdir1 should be a directory");
    QVERIFY2(subdir1.cluster >= 2, "subdir1 should have a valid cluster");

    QList<QFATFileInfo> subdir1Files = fs.listDirectory(static_cast<quint16>(subdir1.cluster));

    QVERIFY2(findFileByName(subdir1Files, "file1.txt"), "file1.txt not found in subdir1");
    QVERIFY2(findFileByName(subdir1Files, "file2.txt"), "file2.txt not found in subdir1");
    QVERIFY2(findFileByName(subdir1Files, "nested"), "nested directory not found in subdir1");

    QFATFileInfo nested = getFileByName(subdir1Files, "nested");
    QVERIFY2(nested.isDirectory, "nested should be a directory");

    qDebug() << "FAT16 subdirectory content validation passed";
}

void TestFAT16ReadOperations::testFileSizes()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QFATFileInfo emptyFile = getFileByName(files, "empty.txt");
    QCOMPARE(emptyFile.size, quint32(0));

    QFATFileInfo largeFile = getFileByName(files, "largefile.bin");
    QCOMPARE(largeFile.size, quint32(102400));

    QFATFileInfo binaryFile = getFileByName(files, "binary.dat");
    QCOMPARE(binaryFile.size, quint32(10240));

    qDebug() << "FAT16 file size validation passed";
}

void TestFAT16ReadOperations::testLongFilenames()
{
    QSharedPointer<QFile> file(new QFile(TEST_FAT16_IMAGE_PATH));
    QVERIFY(file->open(QIODevice::ReadOnly));
    QFAT16FileSystem fs(file);

    QList<QFATFileInfo> files = fs.listRootDirectory();

    QVERIFY2(findFileByName(files, "this_is_a_long_filename.txt"), "Long filename not found in FAT16");

    qDebug() << "FAT16 long filename validation passed";
}

bool TestFAT16ReadOperations::findFileByName(const QList<QFATFileInfo> &files, const QString &name)
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

QFATFileInfo TestFAT16ReadOperations::getFileByName(const QList<QFATFileInfo> &files, const QString &name)
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

QTEST_MAIN(TestFAT16ReadOperations)
#include "test_fat16_read.moc"
