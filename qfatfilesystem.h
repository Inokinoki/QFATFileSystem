#ifndef QFATFILESYSTEM_H
#define QFATFILESYSTEM_H

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QMap>
#include <QSharedPointer>
#include <QScopedPointer>
#include <QString>

struct QFATFileInfo {
    QString name;
    QString longName;
    bool isDirectory;
    quint32 size;
    QDateTime created;
    QDateTime modified;
    quint16 attributes;
    quint32 cluster; // First cluster number (for FAT16, only low 16 bits are used)

    QFATFileInfo()
        : isDirectory(false)
        , size(0)
        , attributes(0)
        , cluster(0)
    {
    }
};

// Error codes for FAT operations
enum class QFATError {
    None,
    DeviceNotOpen,
    InvalidPath,
    FileNotFound,
    DirectoryNotFound,
    InvalidCluster,
    ReadError,
    WriteError,
    NotImplemented,
    InsufficientSpace,
    InvalidFileName
};

// Base class with common FAT filesystem functionality
class QFATFileSystem
{
public:
    QFATFileSystem(QSharedPointer<QIODevice> device);
    virtual ~QFATFileSystem();

    // Pure virtual methods to be implemented by derived classes
    virtual QList<QFATFileInfo> listRootDirectory() = 0;
    virtual QList<QFATFileInfo> listDirectory(const QString &path) = 0;

    // File reading/writing operations
    virtual QByteArray readFile(const QString &path, QFATError &error) = 0;
    virtual QByteArray readFilePartial(const QString &path, quint32 offset, quint32 length, QFATError &error) = 0;
    virtual bool writeFile(const QString &path, const QByteArray &data, QFATError &error) = 0;
    virtual bool deleteFile(const QString &path, QFATError &error) = 0;

    // File/directory operations
    virtual bool renameFile(const QString &oldPath, const QString &newPath, QFATError &error) = 0;
    virtual bool moveFile(const QString &sourcePath, const QString &destPath, QFATError &error) = 0;

    // Directory operations
    virtual bool createDirectory(const QString &path, QFATError &error) = 0;
    virtual bool deleteDirectory(const QString &path, bool recursive, QFATError &error) = 0;

    // File/directory information
    virtual bool exists(const QString &path) = 0;
    virtual QFATFileInfo getFileInfo(const QString &path, QFATError &error) = 0;

    // Filesystem information
    virtual quint32 getFreeSpace(QFATError &error) = 0;
    virtual quint32 getTotalSpace(QFATError &error) = 0;

    // Error handling
    QFATError lastError() const { return m_lastError; }
    QString errorString() const;

protected:
    QDataStream m_stream;
    QSharedPointer<QIODevice> m_device;
    QFATError m_lastError;

    // Common helper methods
    QFATFileInfo parseDirectoryEntry(quint8 *entry, QString &longName);
    QString readLongFileName(quint8 *entry);
    bool isValidEntry(quint8 *entry);
    bool isDeletedEntry(quint8 *entry);
    bool isLongFileNameEntry(quint8 *entry);
    QDateTime parseDateTime(quint16 date, quint16 time);
    quint16 readBytesPerSector();
    quint8 readSectorsPerCluster();
    quint16 readReservedSectors();
    quint8 readNumberOfFATs();
    quint16 readRootEntryCount();
    QList<QFATFileInfo> readDirectoryEntries(quint32 offset, quint32 maxSize);

    // Path traversal helpers
    QStringList splitPath(const QString &path);
    QFATFileInfo findInDirectory(const QList<QFATFileInfo> &entries, const QString &name);

    // Writing helpers
    void encodeFATDateTime(const QDateTime &dt, quint16 &date, quint16 &time);
    QString generateShortName(const QString &longName, const QList<QFATFileInfo> &existingEntries);
    quint8 calculateLFNChecksum(const QString &shortName);
    int calculateLFNEntriesNeeded(const QString &longName);
    void writeLFNEntry(quint8 *entry, const QString &longName, int sequence, quint8 checksum, bool isLast);
};

// FAT12 specific filesystem implementation
class QFAT12FileSystem : public QFATFileSystem
{
public:
    QFAT12FileSystem(QSharedPointer<QIODevice> device);

    // Factory method
    static QScopedPointer<QFAT12FileSystem> create(const QString &imagePath);

    // FAT12 specific methods
    QList<QFATFileInfo> listRootDirectory() override;
    QList<QFATFileInfo> listDirectory(const QString &path) override;
    QList<QFATFileInfo> listDirectory(quint16 cluster);

    // File operations
    QByteArray readFile(const QString &path, QFATError &error) override;
    QByteArray readFilePartial(const QString &path, quint32 offset, quint32 length, QFATError &error) override;
    bool writeFile(const QString &path, const QByteArray &data, QFATError &error) override;
    bool deleteFile(const QString &path, QFATError &error) override;

    // File/directory operations
    bool renameFile(const QString &oldPath, const QString &newPath, QFATError &error) override;
    bool moveFile(const QString &sourcePath, const QString &destPath, QFATError &error) override;

    // Directory operations
    bool createDirectory(const QString &path, QFATError &error) override;
    bool deleteDirectory(const QString &path, bool recursive, QFATError &error) override;

    // File/directory information
    bool exists(const QString &path) override;
    QFATFileInfo getFileInfo(const QString &path, QFATError &error) override;

    // Filesystem information
    quint32 getFreeSpace(QFATError &error) override;
    quint32 getTotalSpace(QFATError &error) override;

private:
    quint16 readRootDirSector();
    quint32 calculateRootDirOffset();
    quint32 calculateClusterOffset(quint16 cluster);
    quint16 readNextCluster(quint16 cluster);

    // Path traversal
    QFATFileInfo findFileByPath(const QString &path, QFATError &error);

    // Cluster chain operations
    QList<quint16> getClusterChain(quint16 startCluster);
    QByteArray readClusterChain(quint16 startCluster, quint32 fileSize);

    // Writing operations
    quint16 findFreeCluster();
    bool writeNextCluster(quint16 cluster, quint16 value);
    bool writeClusterData(quint16 cluster, const QByteArray &data, quint32 offset = 0);
    QList<quint16> allocateClusterChain(quint32 numClusters);
    bool freeClusterChain(quint16 startCluster);
    bool updateDirectoryEntry(const QString &parentPath, const QFATFileInfo &fileInfo);
    bool createDirectoryEntry(quint32 dirOffset, const QFATFileInfo &fileInfo);
    bool deleteDirectoryEntry(const QString &path);
    QString modifyDirectoryEntryName(const QString &path, const QString &newName);
    bool isDirectoryEmpty(quint16 cluster);

    // In-memory mapping for files written without LFN entries
    // Maps long name (e.g., "testfile0.txt") to short name (e.g., "TESTF~31.TXT")
    QMap<QString, QString> m_longToShortNameMap;
};

// FAT16 specific filesystem implementation
class QFAT16FileSystem : public QFATFileSystem
{
public:
    QFAT16FileSystem(QSharedPointer<QIODevice> device);

    // Factory method
    static QScopedPointer<QFAT16FileSystem> create(const QString &imagePath);

    // FAT16 specific methods
    QList<QFATFileInfo> listRootDirectory() override;
    QList<QFATFileInfo> listDirectory(const QString &path) override;
    QList<QFATFileInfo> listDirectory(quint16 cluster);

    // File operations
    QByteArray readFile(const QString &path, QFATError &error) override;
    QByteArray readFilePartial(const QString &path, quint32 offset, quint32 length, QFATError &error) override;
    bool writeFile(const QString &path, const QByteArray &data, QFATError &error) override;
    bool deleteFile(const QString &path, QFATError &error) override;

    // File/directory operations
    bool renameFile(const QString &oldPath, const QString &newPath, QFATError &error) override;
    bool moveFile(const QString &sourcePath, const QString &destPath, QFATError &error) override;

    // Directory operations
    bool createDirectory(const QString &path, QFATError &error) override;
    bool deleteDirectory(const QString &path, bool recursive, QFATError &error) override;

    // File/directory information
    bool exists(const QString &path) override;
    QFATFileInfo getFileInfo(const QString &path, QFATError &error) override;

    // Filesystem information
    quint32 getFreeSpace(QFATError &error) override;
    quint32 getTotalSpace(QFATError &error) override;

private:
    quint16 readRootDirSector();
    quint32 calculateRootDirOffset();
    quint32 calculateClusterOffset(quint16 cluster);
    quint16 readNextCluster(quint16 cluster);

    // Path traversal
    QFATFileInfo findFileByPath(const QString &path, QFATError &error);

    // Cluster chain operations
    QList<quint16> getClusterChain(quint16 startCluster);
    QByteArray readClusterChain(quint16 startCluster, quint32 fileSize);

    // Writing operations
    quint16 findFreeCluster();
    bool writeNextCluster(quint16 cluster, quint16 value);
    bool writeClusterData(quint16 cluster, const QByteArray &data, quint32 offset = 0);
    QList<quint16> allocateClusterChain(quint32 numClusters);
    bool freeClusterChain(quint16 startCluster);
    bool updateDirectoryEntry(const QString &parentPath, const QFATFileInfo &fileInfo);
    bool createDirectoryEntry(quint32 dirOffset, const QFATFileInfo &fileInfo);
    bool deleteDirectoryEntry(const QString &path);
    QString modifyDirectoryEntryName(const QString &path, const QString &newName);
    bool isDirectoryEmpty(quint16 cluster);

    // In-memory mapping for files written without LFN entries
    // Maps long name (e.g., "testfile0.txt") to short name (e.g., "TESTF~31.TXT")
    QMap<QString, QString> m_longToShortNameMap;
};

// FAT32 specific filesystem implementation
class QFAT32FileSystem : public QFATFileSystem
{
public:
    QFAT32FileSystem(QSharedPointer<QIODevice> device);

    // Factory method
    static QScopedPointer<QFAT32FileSystem> create(const QString &imagePath);

    // FAT32 specific methods
    QList<QFATFileInfo> listRootDirectory() override;
    QList<QFATFileInfo> listDirectory(const QString &path) override;
    QList<QFATFileInfo> listDirectory(quint32 cluster);

    // File operations
    QByteArray readFile(const QString &path, QFATError &error) override;
    QByteArray readFilePartial(const QString &path, quint32 offset, quint32 length, QFATError &error) override;
    bool writeFile(const QString &path, const QByteArray &data, QFATError &error) override;
    bool deleteFile(const QString &path, QFATError &error) override;

    // File/directory operations
    bool renameFile(const QString &oldPath, const QString &newPath, QFATError &error) override;
    bool moveFile(const QString &sourcePath, const QString &destPath, QFATError &error) override;

    // Directory operations
    bool createDirectory(const QString &path, QFATError &error) override;
    bool deleteDirectory(const QString &path, bool recursive, QFATError &error) override;

    // File/directory information
    bool exists(const QString &path) override;
    QFATFileInfo getFileInfo(const QString &path, QFATError &error) override;

    // Filesystem information
    quint32 getFreeSpace(QFATError &error) override;
    quint32 getTotalSpace(QFATError &error) override;

private:
    quint32 readRootDirCluster();
    quint32 calculateClusterOffset(quint32 cluster);
    quint32 readNextCluster(quint32 cluster);

    // Path traversal
    QFATFileInfo findFileByPath(const QString &path, QFATError &error);

    // Cluster chain operations
    QList<quint32> getClusterChain(quint32 startCluster);
    QByteArray readClusterChain(quint32 startCluster, quint32 fileSize);

    // Writing operations
    quint32 findFreeCluster();
    bool writeNextCluster(quint32 cluster, quint32 value);
    bool writeClusterData(quint32 cluster, const QByteArray &data, quint32 offset = 0);
    QList<quint32> allocateClusterChain(quint32 numClusters);
    bool freeClusterChain(quint32 startCluster);
    bool updateDirectoryEntry(const QString &parentPath, const QFATFileInfo &fileInfo);
    bool createDirectoryEntry(quint32 dirOffset, const QFATFileInfo &fileInfo);
    bool deleteDirectoryEntry(const QString &path);
    QString modifyDirectoryEntryName(const QString &path, const QString &newName);
    bool isDirectoryEmpty(quint32 cluster);

    // In-memory mapping for files written without LFN entries
    // Maps long name (e.g., "testfile0.txt") to short name (e.g., "TESTF~31.TXT")
    QMap<QString, QString> m_longToShortNameMap;
};

#endif