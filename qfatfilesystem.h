#ifndef QFATFILESYSTEM_H
#define QFATFILESYSTEM_H

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QString>

struct FileInfo {
    QString name;
    QString longName;
    bool isDirectory;
    quint32 size;
    QDateTime created;
    QDateTime modified;
    quint16 attributes;
    quint32 cluster; // First cluster number (for FAT16, only low 16 bits are used)

    FileInfo()
        : isDirectory(false)
        , size(0)
        , attributes(0)
        , cluster(0)
    {
    }
};

// Base class with common FAT filesystem functionality
class QFATFileSystem
{
public:
    QFATFileSystem(QIODevice *device);
    virtual ~QFATFileSystem();

    // Pure virtual methods to be implemented by derived classes
    virtual QList<FileInfo> listRootDirectory() = 0;
    virtual QList<FileInfo> listDirectory(const QString &path) = 0;

protected:
    QDataStream m_stream;
    QIODevice *m_device;

    // Common helper methods
    FileInfo parseDirectoryEntry(quint8 *entry, QString &longName);
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
    QList<FileInfo> readDirectoryEntries(quint32 offset, quint32 maxSize);
};

// FAT16 specific filesystem implementation
class QFAT16FileSystem : public QFATFileSystem
{
public:
    QFAT16FileSystem(QIODevice *device);

    // FAT16 specific methods
    QList<FileInfo> listRootDirectory() override;
    QList<FileInfo> listDirectory(const QString &path) override;
    QList<FileInfo> listDirectory(quint16 cluster);

private:
    quint16 readRootDirSector();
    quint32 calculateRootDirOffset();
    quint32 calculateClusterOffset(quint16 cluster);
    quint16 readNextCluster(quint16 cluster);
};

// FAT32 specific filesystem implementation
class QFAT32FileSystem : public QFATFileSystem
{
public:
    QFAT32FileSystem(QIODevice *device);

    // FAT32 specific methods
    QList<FileInfo> listRootDirectory() override;
    QList<FileInfo> listDirectory(const QString &path) override;
    QList<FileInfo> listDirectory(quint32 cluster);

private:
    quint32 readRootDirCluster();
    quint32 calculateClusterOffset(quint32 cluster);
    quint32 readNextCluster(quint32 cluster);
};

#endif