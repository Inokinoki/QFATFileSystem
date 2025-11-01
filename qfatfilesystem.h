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

class QFATFileSystem
{
public:
    QFATFileSystem(const QString &filePath);
    bool open();
    void close();
    void readFAT16();
    void readFAT32();

    // File listing methods
    QList<FileInfo> listFilesFAT16();
    QList<FileInfo> listFilesFAT32();
    QList<FileInfo> listRootDirectory();
    QList<FileInfo> listDirectory(const QString &path);
    QList<FileInfo> listDirectoryFAT16(quint16 cluster);
    QList<FileInfo> listDirectoryFAT32(quint32 cluster);

private:
    QFile m_file;
    QDataStream m_stream;

    // Internal helper methods
    FileInfo parseDirectoryEntry(quint8 *entry, QString &longName);
    QString readLongFileName(quint8 *entry);
    bool isValidEntry(quint8 *entry);
    bool isDeletedEntry(quint8 *entry);
    bool isLongFileNameEntry(quint8 *entry);
    quint16 readRootDirSectorFAT16();
    quint32 readRootDirClusterFAT32();
    quint16 readBytesPerSector();
    quint8 readSectorsPerCluster();
    quint16 readReservedSectors();
    quint8 readNumberOfFATs();
    quint16 readRootEntryCount();
    quint32 calculateRootDirOffsetFAT16();
    quint32 calculateRootDirOffsetFAT32();
    quint32 calculateClusterOffsetFAT16(quint16 cluster);
    quint32 calculateClusterOffsetFAT32(quint32 cluster);
    quint16 readNextClusterFAT16(quint16 cluster);
    quint32 readNextClusterFAT32(quint32 cluster);
    QList<FileInfo> readDirectoryEntries(quint32 offset, quint32 maxSize);
};

#endif