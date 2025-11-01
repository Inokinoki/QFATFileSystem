#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QString>

#include "internal_constants.h"
#include "qfatfilesystem.h"

// ============================================================================
// Base class: QFATFileSystem
// ============================================================================

QFATFileSystem::QFATFileSystem(QIODevice *device)
    : m_device(device)
{
    m_stream.setDevice(m_device);
    // Set byte order to Little Endian so that the data is read correctly
    m_stream.setByteOrder(QDataStream::LittleEndian);
}

QFATFileSystem::~QFATFileSystem()
{
}

// Helper methods for reading BPB values
quint16 QFATFileSystem::readBytesPerSector()
{
    m_stream.device()->seek(BPB_BYTES_PER_SECTOR_OFFSET);
    quint16 value;
    m_stream >> value;
    return value;
}

quint8 QFATFileSystem::readSectorsPerCluster()
{
    m_stream.device()->seek(BPB_SECTORS_PER_CLUSTER_OFFSET);
    quint8 value;
    m_stream >> value;
    return value;
}

quint16 QFATFileSystem::readReservedSectors()
{
    m_stream.device()->seek(BPB_RESERVED_SECTORS_OFFSET);
    quint16 value;
    m_stream >> value;
    return value;
}

quint8 QFATFileSystem::readNumberOfFATs()
{
    m_stream.device()->seek(BPB_NUMBER_OF_FATS_OFFSET);
    quint8 value;
    m_stream >> value;
    return value;
}

quint16 QFATFileSystem::readRootEntryCount()
{
    m_stream.device()->seek(BPB_ROOT_ENTRY_COUNT_OFFSET);
    quint16 value;
    m_stream >> value;
    return value;
}

bool QFATFileSystem::isLongFileNameEntry(quint8 *entry)
{
    // Long filename entries have attribute long file name
    return (entry[ENTRY_ATTRIBUTE_OFFSET] & ENTRY_ATTRIBUTE_LONG_FILE_NAME) != 0;
}

bool QFATFileSystem::isDeletedEntry(quint8 *entry)
{
    // Deleted entries have the deleted attribute
    return entry[ENTRY_NAME_OFFSET] == ENTRY_DELETED;
}

bool QFATFileSystem::isValidEntry(quint8 *entry)
{
    // Valid entries don't start with end of directory, deleted, or current/parent directory
    if (entry[ENTRY_NAME_OFFSET] == ENTRY_END_OF_DIRECTORY || entry[ENTRY_NAME_OFFSET] == ENTRY_DELETED) {
        return false;
    }
    // Skip . and .. entries
    if (entry[ENTRY_NAME_OFFSET] == ENTRY_CURRENT_DIRECTORY) {
        return false;
    }
    // Skip volume label entries
    quint8 attributes = entry[ENTRY_ATTRIBUTE_OFFSET];
    if ((attributes & ENTRY_ATTRIBUTE_VOLUME_LABEL) != 0) {
        return false;
    }
    return true;
}

QString QFATFileSystem::readLongFileName(quint8 *entry)
{
    // Long filename entries contain UTF-16LE characters
    QString name;
    // Read characters from 3 parts: 5, 6, 2 chars
    quint16 chars[ENTRY_LFN_CHARS];
    int pos = 0;

    // Part 1: 5 characters, 10 bytes
    for (int i = 0; i < ENTRY_LFN_PART1_LENGTH / 2; i++) {
        chars[pos++] = (entry[ENTRY_LFN_PART1_OFFSET + i * 2] | (entry[ENTRY_LFN_PART1_OFFSET + (i + 1) * 2] << 8));
    }
    // Part 2: 6 characters, 12 bytes
    for (int i = 0; i < ENTRY_LFN_PART2_LENGTH / 2; i++) {
        chars[pos++] = (entry[ENTRY_LFN_PART2_OFFSET + i * 2] | (entry[ENTRY_LFN_PART2_OFFSET + (i + 1) * 2] << 8));
    }
    // Part 3: 2 characters, 4 bytes
    for (int i = 0; i < ENTRY_LFN_PART3_LENGTH / 2; i++) {
        chars[pos++] = (entry[ENTRY_LFN_PART3_OFFSET + i * 2] | (entry[ENTRY_LFN_PART3_OFFSET + (i + 1) * 2] << 8));
    }

    // Convert to QString, stopping at null terminator or 0xFFFF
    for (int i = 0; i < ENTRY_LFN_CHARS; i++) {
        if (chars[i] == QChar::Null || chars[i] == 0xFFFF)
            break;
        name.append(QChar(chars[i]));
    }

    return name;
}

FileInfo QFATFileSystem::parseDirectoryEntry(quint8 *entry, QString &longName)
{
    FileInfo info;

    // Read 8.3 filename (remove trailing spaces)
    QString name8_3;
    int nameEnd = 7;
    while (nameEnd >= 0 && entry[nameEnd] == QChar::Space)
        nameEnd--;
    for (int i = 0; i <= nameEnd; i++) {
        name8_3.append(QChar(entry[i]));
    }

    // Read extension (remove trailing spaces)
    QString ext;
    int extEnd = 10;
    while (extEnd >= 8 && entry[extEnd] == QChar::Space)
        extEnd--;
    for (int i = 8; i <= extEnd; i++) {
        ext.append(QChar(entry[i]));
    }

    if (!ext.isEmpty()) {
        if (!name8_3.isEmpty()) {
            name8_3.append('.');
        }
        name8_3.append(ext);
    }

    // Clean up name - remove null characters and control characters
    name8_3 = name8_3.trimmed();

    info.name = name8_3;
    info.longName = longName.isEmpty() ? name8_3 : longName.trimmed();

    // Read attributes
    info.attributes = entry[ENTRY_ATTRIBUTE_OFFSET];
    info.isDirectory = (info.attributes & ENTRY_ATTRIBUTE_DIRECTORY) != 0;

    // Read file size (4 bytes, Little Endian)
    info.size = (entry[ENTRY_SIZE_OFFSET] | (entry[ENTRY_SIZE_OFFSET + 1] << 8) | (entry[ENTRY_SIZE_OFFSET + 2] << 16) | (entry[ENTRY_SIZE_OFFSET + 3] << 24));

    // Read first cluster (2 bytes, Little Endian)
    // For FAT16, use low word; for FAT32, we'll need to read the high word from high order cluster address
    info.cluster = entry[ENTRY_CLUSTER_OFFSET] | (entry[ENTRY_CLUSTER_OFFSET + 1] << 8);

    // For FAT32, also read high word (2 bytes, Little Endian)
    // Note: This assumes FAT32 - we'd need to detect FAT type to be precise
    quint16 clusterHigh = entry[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET] | (entry[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET + 1] << 8);
    if (clusterHigh > 0) {
        info.cluster |= (static_cast<quint32>(clusterHigh) << 16);
    }

    // Read date/time, little endian
    quint16 createdTime = (entry[ENTRY_CREATION_DATE_TIME_OFFSET] | (entry[ENTRY_CREATION_DATE_TIME_OFFSET + 1] << 8));
    quint16 createdDate = (entry[ENTRY_CREATION_DATE_TIME_OFFSET + 2] | (entry[ENTRY_CREATION_DATE_TIME_OFFSET + 3] << 8));
    quint16 modifiedTime = (entry[ENTRY_WRITTEN_DATE_TIME_OFFSET] | (entry[ENTRY_WRITTEN_DATE_TIME_OFFSET + 1] << 8));
    quint16 modifiedDate = (entry[ENTRY_WRITTEN_DATE_TIME_OFFSET + 2] | (entry[ENTRY_WRITTEN_DATE_TIME_OFFSET + 3] << 8));

    // Parse FAT date/time format
    if (modifiedDate != 0) {
        info.modified = parseDateTime(modifiedDate, modifiedTime);
    }
    if (createdDate != 0) {
        info.created = parseDateTime(createdDate, createdTime);
    }

    return info;
}

QDateTime QFATFileSystem::parseDateTime(quint16 date, quint16 time)
{
    // Parse date/time from FAT format
    // date: | year (1980-2107, 7 bits) | month (1-12, 4 bits) | day (1-31, 5 bits) |
    // time: | hour (0-23, 5 bits) | minute (0-59, 6 bits) | second (0-59, 5 bits) |
    int year = ENTRY_DATE_TIME_START_OF_YEAR + ((date >> 9) & MASK_7_BITS);
    int month = (date >> 5) & MASK_4_BITS;
    int day = (date & MASK_5_BITS) + 1;
    int hour = (time >> 11) & MASK_5_BITS;
    int minute = (time >> 5) & MASK_6_BITS;
    int second = (time & MASK_5_BITS) * 2;
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second));
}

QList<FileInfo> QFATFileSystem::readDirectoryEntries(quint32 offset, quint32 maxSize)
{
    QList<FileInfo> files;

    if (!m_device->isOpen() || m_device->atEnd()) {
        return files;
    }

    m_stream.device()->seek(offset);

    QByteArray buffer(maxSize, 0);
    qint64 bytesRead = m_stream.readRawData(buffer.data(), maxSize);

    if (bytesRead <= 0) {
        return files;
    }

    quint32 numEntries = bytesRead / (int)ENTRY_SIZE;
    QString currentLongName;

    for (quint32 i = 0; i < numEntries; i++) {
        quint8 *entry = reinterpret_cast<quint8 *>(buffer.data() + i * ENTRY_SIZE);

        if (entry[ENTRY_NAME_OFFSET] == ENTRY_END_OF_DIRECTORY) {
            // End of directory
            break;
        }

        if (isDeletedEntry(entry)) {
            currentLongName.clear();
            continue;
        }

        if (isLongFileNameEntry(entry)) {
            // Read long filename part
            QString part = readLongFileName(entry);
            quint8 sequence = entry[ENTRY_NAME_OFFSET] & ENTRY_LFN_SEQUENCE_MASK;
            bool isLastInSequence = ((entry[ENTRY_NAME_OFFSET] & ENTRY_LFN_SEQUENCE_LAST_MASK) != 0);

            // Long filename entries appear before the short entry in reverse order
            // The entry with sequence number marked with last mask appears first
            if (isLastInSequence) {
                // This is the first entry we see (highest sequence number)
                currentLongName = part;
            } else {
                // Prepend this part (earlier in the filename)
                currentLongName = part + currentLongName;
            }
            continue;
        }

        if (isValidEntry(entry)) {
            FileInfo info = parseDirectoryEntry(entry, currentLongName);
            files.append(info);
            currentLongName.clear();
        }
    }

    return files;
}

// ============================================================================
// QFAT16FileSystem
// ============================================================================

QFAT16FileSystem::QFAT16FileSystem(QIODevice *device)
    : QFATFileSystem(device)
{
}

quint16 QFAT16FileSystem::readRootDirSector()
{
    // For FAT16, calculate root directory sector
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    // Root directory starts after reserved sectors + FATs
    return reservedSectors + (numFATs * sectorsPerFAT);
}

quint32 QFAT16FileSystem::calculateRootDirOffset()
{
    quint16 rootDirSector = readRootDirSector();
    quint16 bytesPerSector = readBytesPerSector();
    return rootDirSector * bytesPerSector;
}

quint32 QFAT16FileSystem::calculateClusterOffset(quint16 cluster)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint16 rootEntryCount = readRootEntryCount();
    quint32 rootDirSectors = (rootEntryCount * ENTRY_SIZE + bytesPerSector - 1) / bytesPerSector;

    // Data area starts after reserved sectors + FATs + root directory
    quint32 dataAreaStart = reservedSectors + (numFATs * sectorsPerFAT) + rootDirSectors;
    quint32 dataAreaOffset = dataAreaStart * bytesPerSector;

    // Cluster 2 is first data cluster, so cluster starts at (cluster - 2) * cluster size
    quint32 clusterOffset = (cluster - 2) * sectorsPerCluster * bytesPerSector;

    return dataAreaOffset + clusterOffset;
}

quint16 QFAT16FileSystem::readNextCluster(quint16 cluster)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();
    quint16 fatOffset = reservedSectors * bytesPerSector + cluster * 2; // 2 bytes per cluster

    m_stream.device()->seek(fatOffset);
    quint16 nextCluster;
    m_stream >> nextCluster;

    // Check for end of cluster chain (0xFFF8-0xFFFF)
    if (nextCluster >= 0xFFF8) {
        return 0; // End of chain
    }

    return nextCluster;
}

QList<FileInfo> QFAT16FileSystem::listRootDirectory()
{
    if (!m_device->isOpen() || m_device->atEnd()) {
        qWarning() << "File not open";
        return QList<FileInfo>();
    }

    quint32 rootDirOffset = calculateRootDirOffset();
    quint16 rootEntryCount = readRootEntryCount();

    if (rootEntryCount == 0) {
        rootEntryCount = 512; // Default if not specified
    }

    quint32 rootDirSize = rootEntryCount * ENTRY_SIZE;

    return readDirectoryEntries(rootDirOffset, rootDirSize);
}

QList<FileInfo> QFAT16FileSystem::listDirectory(quint16 cluster)
{
    QList<FileInfo> files;

    if (!m_device->isOpen() || cluster < 2) {
        return files;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    quint16 currentCluster = cluster;
    quint32 totalSize = 0;
    const quint32 maxDirSize = DATA_AREA_SIZE; // Limit directory size to DATA_AREA_SIZE

    // Follow cluster chain
    while (currentCluster >= 2 && currentCluster < 0xFFF8 && totalSize < maxDirSize) {
        quint32 clusterOffset = calculateClusterOffset(currentCluster);
        QList<FileInfo> entries = readDirectoryEntries(clusterOffset, clusterSize);

        bool foundEnd = false;
        for (const FileInfo &entry : entries) {
            if (entry.name.isEmpty() && entry.size == 0) {
                foundEnd = true;
                break;
            }
            files.append(entry);
        }

        if (foundEnd) {
            break;
        }

        totalSize += clusterSize;
        currentCluster = readNextCluster(currentCluster);
        if (currentCluster == 0) {
            break;
        }
    }

    return files;
}

QList<FileInfo> QFAT16FileSystem::listDirectory(const QString &path)
{
    // For now, this is a placeholder for path-based directory listing
    // Would need to implement path traversal to find directory cluster
    // For simplicity, if path is empty or "/", list root directory
    if (path.isEmpty() || path == "/" || path == "\\") {
        return listRootDirectory();
    }

    // TODO: Implement path traversal to find and list subdirectories
    QList<FileInfo> empty;
    qWarning() << "Path-based directory listing not yet implemented:" << path;
    return empty;
}

// ============================================================================
// QFAT32FileSystem
// ============================================================================

QFAT32FileSystem::QFAT32FileSystem(QIODevice *device)
    : QFATFileSystem(device)
{
}

quint32 QFAT32FileSystem::readRootDirCluster()
{
    // For FAT32, root directory cluster is stored in the BIOS Parameter Block
    m_stream.device()->seek(BPB_ROOT_DIRECTORY_CLUSTER_OFFSET);
    quint32 value;
    m_stream >> value;
    return value;
}

quint32 QFAT32FileSystem::calculateClusterOffset(quint32 cluster)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint32 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    // Calculate data area start
    quint32 dataAreaStart = reservedSectors + (numFATs * sectorsPerFAT);
    quint32 dataAreaOffset = dataAreaStart * bytesPerSector;

    // Cluster 2 is first data cluster, so cluster starts at (cluster - 2) * cluster size
    quint32 clusterOffset = (cluster - 2) * sectorsPerCluster * bytesPerSector;

    return dataAreaOffset + clusterOffset;
}

quint32 QFAT32FileSystem::readNextCluster(quint32 cluster)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();
    quint32 fatOffset = reservedSectors * bytesPerSector + cluster * 4; // 4 bytes per cluster

    m_stream.device()->seek(fatOffset);
    quint32 nextCluster;
    m_stream >> nextCluster;

    // Mask off high 4 bits (only use 28 bits for FAT32)
    nextCluster &= 0x0FFFFFFF;

    // Check for end of cluster chain (0x0FFFFFF8-0x0FFFFFFF)
    if (nextCluster >= 0x0FFFFFF8) {
        return 0; // End of chain
    }

    return nextCluster;
}

QList<FileInfo> QFAT32FileSystem::listRootDirectory()
{
    if (!m_device->isOpen() || m_device->atEnd()) {
        qWarning() << "Device not open";
        return QList<FileInfo>();
    }

    quint32 rootDirCluster = readRootDirCluster();
    return listDirectory(rootDirCluster);
}

QList<FileInfo> QFAT32FileSystem::listDirectory(quint32 cluster)
{
    QList<FileInfo> files;

    if (!m_device->isOpen() || cluster < 2) {
        return files;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    quint32 currentCluster = cluster;
    quint32 totalSize = 0;
    const quint32 maxDirSize = DATA_AREA_SIZE; // Limit directory size to DATA_AREA_SIZE

    // Follow cluster chain
    while (currentCluster >= 2 && currentCluster < 0x0FFFFFF8 && totalSize < maxDirSize) {
        quint32 clusterOffset = calculateClusterOffset(currentCluster);
        QList<FileInfo> entries = readDirectoryEntries(clusterOffset, clusterSize);

        bool foundEnd = false;
        for (const FileInfo &entry : entries) {
            if (entry.name.isEmpty() && entry.size == 0) {
                foundEnd = true;
                break;
            }
            files.append(entry);
        }

        if (foundEnd) {
            break;
        }

        totalSize += clusterSize;
        currentCluster = readNextCluster(currentCluster);
        if (currentCluster == 0) {
            break;
        }
    }

    return files;
}

QList<FileInfo> QFAT32FileSystem::listDirectory(const QString &path)
{
    // For now, this is a placeholder for path-based directory listing
    // Would need to implement path traversal to find directory cluster
    // For simplicity, if path is empty or "/", list root directory
    if (path.isEmpty() || path == "/" || path == "\\") {
        return listRootDirectory();
    }

    QString normalizedPath = path.toLower();
    if (normalizedPath.endsWith('/') || normalizedPath.endsWith('\\')) {
        normalizedPath = normalizedPath.left(normalizedPath.length() - 1);
    }

    // TODO: Implement path traversal to find and list subdirectories
    QList<FileInfo> empty;
    qWarning() << "Path-based directory listing not yet implemented:" << path;
    return empty;
}
