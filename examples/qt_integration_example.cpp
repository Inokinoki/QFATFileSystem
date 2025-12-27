/**
 * Qt Integration Example
 *
 * This example demonstrates how QFATFileSystem could integrate with
 * Qt's standard file I/O classes through QAbstractFileEngine.
 *
 * This is a PROOF OF CONCEPT showing the proposed architecture.
 * Actual implementation would go in src/qt_integration/
 */

#include <QAbstractFileEngine>
#include <QAbstractFileEngineHandler>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QTreeView>
#include <QApplication>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

// Simplified interface - would use actual QFATFileSystem
class SimpleFATInterface {
public:
    virtual QByteArray readFile(const QString &path) = 0;
    virtual bool writeFile(const QString &path, const QByteArray &data) = 0;
    virtual QStringList listDirectory(const QString &path) = 0;
    virtual bool exists(const QString &path) = 0;
    virtual bool isDirectory(const QString &path) = 0;
    virtual qint64 fileSize(const QString &path) = 0;
};

/**
 * Example QAbstractFileEngine implementation for FAT filesystems
 *
 * This allows Qt's QFile, QDir, QFileDialog, etc. to work directly
 * with FAT filesystem images.
 */
class QFATFileEngine : public QAbstractFileEngine
{
public:
    explicit QFATFileEngine(const QString &fileName, SimpleFATInterface *fs)
        : m_fileName(fileName)
        , m_fs(fs)
        , m_position(0)
        , m_openMode(QIODevice::NotOpen)
    {
        qDebug() << "[QFATFileEngine] Created for" << fileName;
    }

    ~QFATFileEngine() override
    {
        close();
    }

    // Open the file and cache its contents
    bool open(QIODevice::OpenMode mode) override
    {
        qDebug() << "[QFATFileEngine] Opening" << m_fileName << "mode:" << mode;

        if (m_openMode != QIODevice::NotOpen) {
            return false; // Already open
        }

        m_openMode = mode;
        m_position = 0;

        if (mode & QIODevice::ReadOnly) {
            // Read file contents into memory
            m_fileData = m_fs->readFile(m_fileName);
            return !m_fileData.isNull();
        }

        if (mode & QIODevice::WriteOnly) {
            // Start with empty buffer for writing
            m_fileData.clear();
            return true;
        }

        return false;
    }

    bool close() override
    {
        qDebug() << "[QFATFileEngine] Closing" << m_fileName;

        if (m_openMode == QIODevice::NotOpen) {
            return true;
        }

        // If opened for writing, flush to filesystem
        if (m_openMode & QIODevice::WriteOnly) {
            bool success = m_fs->writeFile(m_fileName, m_fileData);
            if (!success) {
                return false;
            }
        }

        m_fileData.clear();
        m_position = 0;
        m_openMode = QIODevice::NotOpen;
        return true;
    }

    qint64 read(char *data, qint64 maxlen) override
    {
        if (!(m_openMode & QIODevice::ReadOnly)) {
            return -1;
        }

        qint64 available = m_fileData.size() - m_position;
        qint64 toRead = qMin(maxlen, available);

        if (toRead > 0) {
            memcpy(data, m_fileData.constData() + m_position, toRead);
            m_position += toRead;
        }

        return toRead;
    }

    qint64 write(const char *data, qint64 len) override
    {
        if (!(m_openMode & QIODevice::WriteOnly)) {
            return -1;
        }

        // Expand buffer if needed
        if (m_position + len > m_fileData.size()) {
            m_fileData.resize(m_position + len);
        }

        memcpy(m_fileData.data() + m_position, data, len);
        m_position += len;

        return len;
    }

    bool seek(qint64 pos) override
    {
        if (pos < 0 || pos > m_fileData.size()) {
            return false;
        }
        m_position = pos;
        return true;
    }

    qint64 size() const override
    {
        if (m_openMode != QIODevice::NotOpen) {
            return m_fileData.size();
        }
        return m_fs->fileSize(m_fileName);
    }

    qint64 pos() const override
    {
        return m_position;
    }

    bool remove() override
    {
        // Would call m_fs->deleteFile(m_fileName)
        qDebug() << "[QFATFileEngine] Remove" << m_fileName;
        return false; // Not implemented in example
    }

    bool mkdir(const QString &dirName, bool createParentDirectories) const override
    {
        qDebug() << "[QFATFileEngine] Mkdir" << dirName;
        // Would call m_fs->createDirectory(dirName)
        return false; // Not implemented in example
    }

    FileFlags fileFlags(FileFlags type) const override
    {
        FileFlags flags = FileFlags();

        if (m_fs->exists(m_fileName)) {
            flags |= ExistsFlag;

            if (m_fs->isDirectory(m_fileName)) {
                flags |= DirectoryType;
            } else {
                flags |= FileType;
            }

            // FAT filesystems are always readable/writable (no permissions)
            flags |= ReadUserPerm | WriteUserPerm;
            flags |= ReadOwnerPerm | WriteOwnerPerm;
        }

        return flags & type;
    }

    QString fileName(FileName file) const override
    {
        switch (file) {
        case DefaultName:
        case AbsoluteName:
        case CanonicalName:
            return m_fileName;
        case BaseName:
            return QFileInfo(m_fileName).fileName();
        case PathName:
            return QFileInfo(m_fileName).path();
        default:
            return QString();
        }
    }

    void setFileName(const QString &file) override
    {
        m_fileName = file;
    }

    // Directory iteration support
    class Iterator : public QAbstractFileEngineIterator
    {
    public:
        Iterator(QDir::Filters filters, const QStringList &nameFilters,
                 const QString &path, SimpleFATInterface *fs)
            : QAbstractFileEngineIterator(filters, nameFilters)
            , m_path(path)
            , m_fs(fs)
            , m_index(-1)
        {
            m_entries = m_fs->listDirectory(path);
        }

        bool hasNext() const override
        {
            return m_index < m_entries.size() - 1;
        }

        QString next() override
        {
            if (!hasNext()) {
                return QString();
            }
            m_index++;
            return currentFileName();
        }

        QString currentFileName() const override
        {
            if (m_index < 0 || m_index >= m_entries.size()) {
                return QString();
            }
            return m_entries[m_index];
        }

    private:
        QString m_path;
        SimpleFATInterface *m_fs;
        QStringList m_entries;
        int m_index;
    };

    QAbstractFileEngineIterator *beginEntryList(QDir::Filters filters,
                                                const QStringList &filterNames) override
    {
        return new Iterator(filters, filterNames, m_fileName, m_fs);
    }

private:
    QString m_fileName;
    SimpleFATInterface *m_fs;
    QByteArray m_fileData;
    qint64 m_position;
    QIODevice::OpenMode m_openMode;
};

/**
 * Handler that creates QFATFileEngine instances for paths with
 * the "fat://" scheme.
 */
class QFATFileEngineHandler : public QAbstractFileEngineHandler
{
public:
    explicit QFATFileEngineHandler(SimpleFATInterface *fs)
        : m_fs(fs)
    {
        qDebug() << "[QFATFileEngineHandler] Registered";
    }

    QAbstractFileEngine *create(const QString &fileName) const override
    {
        // Check if this is a FAT filesystem path
        if (fileName.startsWith("fat://")) {
            QString path = fileName.mid(6); // Remove "fat://" prefix
            qDebug() << "[QFATFileEngineHandler] Creating engine for" << path;
            return new QFATFileEngine(path, m_fs);
        }
        return nullptr; // Let Qt handle other paths
    }

private:
    SimpleFATInterface *m_fs;
};

// Example: Mock FAT filesystem for demonstration
class MockFATFilesystem : public SimpleFATInterface
{
public:
    MockFATFilesystem()
    {
        // Simulate some files in the filesystem
        m_files["/README.TXT"] = "This is a FAT16 filesystem\n";
        m_files["/HELLO.TXT"] = "Hello from FAT!\n";
        m_files["/SUBDIR/FILE.TXT"] = "File in subdirectory\n";
    }

    QByteArray readFile(const QString &path) override
    {
        qDebug() << "[MockFAT] Reading" << path;
        return m_files.value(path);
    }

    bool writeFile(const QString &path, const QByteArray &data) override
    {
        qDebug() << "[MockFAT] Writing" << path << "(" << data.size() << "bytes)";
        m_files[path] = data;
        return true;
    }

    QStringList listDirectory(const QString &path) override
    {
        qDebug() << "[MockFAT] Listing" << path;
        QStringList entries;

        // Simple implementation - just check prefixes
        QString prefix = path;
        if (!prefix.endsWith('/')) {
            prefix += '/';
        }

        for (auto it = m_files.begin(); it != m_files.end(); ++it) {
            QString filePath = it.key();
            if (filePath.startsWith(prefix)) {
                QString relative = filePath.mid(prefix.length());
                int slashPos = relative.indexOf('/');
                if (slashPos == -1) {
                    entries << relative;
                } else {
                    QString dir = relative.left(slashPos);
                    if (!entries.contains(dir)) {
                        entries << dir;
                    }
                }
            }
        }

        return entries;
    }

    bool exists(const QString &path) override
    {
        return m_files.contains(path) || isDirectory(path);
    }

    bool isDirectory(const QString &path) override
    {
        // Check if any file starts with this path
        QString prefix = path;
        if (!prefix.endsWith('/')) {
            prefix += '/';
        }

        for (auto it = m_files.begin(); it != m_files.end(); ++it) {
            if (it.key().startsWith(prefix)) {
                return true;
            }
        }
        return false;
    }

    qint64 fileSize(const QString &path) override
    {
        return m_files.value(path).size();
    }

private:
    QMap<QString, QByteArray> m_files;
};

/**
 * Example 1: Using QFile with FAT filesystem
 */
void example1_QFile()
{
    qDebug() << "\n=== Example 1: QFile Integration ===";

    // Standard Qt file operations work transparently!
    QFile file("fat:///README.TXT");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray content = file.readAll();
        qDebug() << "File content:" << content;
        file.close();
    }

    // Writing also works
    QFile outFile("fat:///OUTPUT.TXT");
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        outFile.write("Written via QFile!\n");
        outFile.close();
        qDebug() << "File written successfully";
    }
}

/**
 * Example 2: Using QFileDialog
 */
void example2_QFileDialog()
{
    qDebug() << "\n=== Example 2: QFileDialog Integration ===";

    // QFileDialog can browse the FAT filesystem!
    // (In a real app with GUI)
    // QString fileName = QFileDialog::getOpenFileName(
    //     nullptr, "Open File", "fat:///", "Text Files (*.TXT)");

    qDebug() << "QFileDialog would work with fat:// scheme";
}

/**
 * Example 3: Directory listing with QDir
 */
void example3_QDir()
{
    qDebug() << "\n=== Example 3: QDir Integration ===";

    QDir dir("fat:///");
    QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    qDebug() << "Directory contents:";
    for (const QString &entry : entries) {
        qDebug() << "  -" << entry;
    }
}

/**
 * Main function
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Create mock FAT filesystem
    MockFATFilesystem *mockFS = new MockFATFilesystem();

    // Register the handler - this makes fat:// scheme work everywhere in Qt!
    QFATFileEngineHandler *handler = new QFATFileEngineHandler(mockFS);

    qDebug() << "=== Qt FAT Filesystem Integration Demo ===\n";
    qDebug() << "After registering QFATFileEngineHandler, all Qt file operations";
    qDebug() << "work transparently with the fat:// URI scheme!\n";

    // Run examples
    example1_QFile();
    example3_QDir();

    // Example 4: Create a file browser GUI (if needed)
    if (false) {  // Set to true to show GUI
        QWidget window;
        QVBoxLayout *layout = new QVBoxLayout(&window);

        QTreeView *treeView = new QTreeView();

        // Note: QFileSystemModel doesn't work directly with custom engines
        // Would need custom model (QFATFileSystemModel) for full integration
        // But QFile and QDir work perfectly!

        layout->addWidget(treeView);
        window.setWindowTitle("FAT Filesystem Browser");
        window.resize(600, 400);
        window.show();

        return app.exec();
    }

    qDebug() << "\n=== Demo Complete ===";
    return 0;
}