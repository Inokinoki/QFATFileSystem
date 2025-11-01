#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QByteArray>
#include <cstring>

#include "qfatfilesystem.h"

QFATFileSystem::QFATFileSystem(const QString& filePath)
    : m_file(filePath)
{
}

bool QFATFileSystem::open() {
    if (!m_file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open file:" << m_file.fileName();
        return false;
    }
    m_stream.setDevice(&m_file);
    m_stream.setByteOrder(QDataStream::LittleEndian);
    return true;
}

void QFATFileSystem::close() {
    m_file.close();
}

void QFATFileSystem::readFAT16() {
    qDebug() << "Reading FAT16 file system";

    // Simplified example: Read the BIOS Parameter Block (BPB)
    m_stream.device()->seek(0x0B);
    quint16 bytesPerSector;
    quint8 sectorsPerCluster;

    m_stream >> bytesPerSector;
    m_stream >> sectorsPerCluster;

    qDebug() << "Bytes per sector:" << bytesPerSector;
    qDebug() << "Sectors per cluster:" << sectorsPerCluster;

    // Additional FAT16 reading logic goes here...
}

void QFATFileSystem::readFAT32() {
    qDebug() << "Reading FAT32 file system";

    // Simplified example: Read the BIOS Parameter Block (BPB)
    m_stream.device()->seek(0x0B);
    quint16 bytesPerSector;
    quint8 sectorsPerCluster;

    m_stream >> bytesPerSector;
    m_stream >> sectorsPerCluster;

    qDebug() << "Bytes per sector:" << bytesPerSector;
    qDebug() << "Sectors per cluster:" << sectorsPerCluster;

    // Additional FAT32 reading logic goes here...
}

// Helper methods for reading BPB values
quint16 QFATFileSystem::readBytesPerSector() {
    m_stream.device()->seek(0x0B);
    quint16 value;
    m_stream >> value;
    return value;
}

quint8 QFATFileSystem::readSectorsPerCluster() {
    m_stream.device()->seek(0x0D);
    quint8 value;
    m_stream >> value;
    return value;
}

quint16 QFATFileSystem::readReservedSectors() {
    m_stream.device()->seek(0x0E);
    quint16 value;
    m_stream >> value;
    return value;
}

quint8 QFATFileSystem::readNumberOfFATs() {
    m_stream.device()->seek(0x10);
    quint8 value;
    m_stream >> value;
    return value;
}

quint16 QFATFileSystem::readRootEntryCount() {
    m_stream.device()->seek(0x11);
    quint16 value;
    m_stream >> value;
    return value;
}

quint16 QFATFileSystem::readRootDirSectorFAT16() {
    // For FAT16, calculate root directory sector
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();
    
    m_stream.device()->seek(0x16);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;
    
    // Root directory starts after reserved sectors + FATs
    return reservedSectors + (numFATs * sectorsPerFAT);
}

quint32 QFATFileSystem::readRootDirClusterFAT32() {
    // For FAT32, root directory cluster is stored at offset 0x2C
    m_stream.device()->seek(0x2C);
    quint32 value;
    m_stream >> value;
    return value;
}

quint32 QFATFileSystem::calculateRootDirOffsetFAT16() {
    quint16 rootDirSector = readRootDirSectorFAT16();
    quint16 bytesPerSector = readBytesPerSector();
    return rootDirSector * bytesPerSector;
}

quint32 QFATFileSystem::calculateRootDirOffsetFAT32() {
    quint32 rootDirCluster = readRootDirClusterFAT32();
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();
    
    m_stream.device()->seek(0x24);
    quint32 sectorsPerFAT;
    m_stream >> sectorsPerFAT;
    
    // Calculate data area start
    quint32 dataAreaStart = reservedSectors + (numFATs * sectorsPerFAT);
    quint32 dataAreaOffset = dataAreaStart * bytesPerSector;
    
    // Cluster 2 is first data cluster, so root cluster starts at (cluster - 2) * cluster size
    quint32 clusterOffset = (rootDirCluster - 2) * sectorsPerCluster * bytesPerSector;
    
    return dataAreaOffset + clusterOffset;
}

bool QFATFileSystem::isLongFileNameEntry(quint8* entry) {
    // Long filename entries have attribute 0x0F
    return (entry[0x0B] & 0x3F) == 0x0F;
}

bool QFATFileSystem::isDeletedEntry(quint8* entry) {
    // Deleted entries start with 0xE5
    return entry[0] == 0xE5;
}

bool QFATFileSystem::isValidEntry(quint8* entry) {
    // Valid entries don't start with 0x00, 0xE5, or 0x2E (current/parent directory)
    if (entry[0] == 0x00 || entry[0] == 0xE5) {
        return false;
    }
    // Skip . and .. entries
    if (entry[0] == 0x2E) {
        return false;
    }
    return true;
}

QString QFATFileSystem::readLongFileName(quint8* entry) {
    // Long filename entries contain UTF-16LE characters
    QString name;
    // Read characters from offset 0x01, 0x0E, 0x1A (3 parts: 5, 6, 2 chars)
    quint16 chars[13];
    int pos = 0;
    
    // Part 1: offset 0x01-0x0A (5 characters, 10 bytes)
    for (int i = 0; i < 5; i++) {
        chars[pos++] = entry[0x01 + i*2] | (entry[0x01 + i*2 + 1] << 8);
    }
    // Part 2: offset 0x0E-0x19 (6 characters, 12 bytes)
    for (int i = 0; i < 6; i++) {
        chars[pos++] = entry[0x0E + i*2] | (entry[0x0E + i*2 + 1] << 8);
    }
    // Part 3: offset 0x1C-0x1F (2 characters, 4 bytes) - Note: offset is 0x1C, not 0x1A
    for (int i = 0; i < 2; i++) {
        chars[pos++] = entry[0x1C + i*2] | (entry[0x1C + i*2 + 1] << 8);
    }
    
    // Convert to QString, stopping at null terminator or 0xFFFF
    for (int i = 0; i < 13; i++) {
        if (chars[i] == 0 || chars[i] == 0xFFFF) break;
        name.append(QChar(chars[i]));
    }
    
    return name;
}

FileInfo QFATFileSystem::parseDirectoryEntry(quint8* entry, QString& longName) {
    FileInfo info;
    
    // Read 8.3 filename (remove trailing spaces)
    QString name8_3;
    int nameEnd = 7;
    while (nameEnd >= 0 && entry[nameEnd] == 0x20) nameEnd--;
    for (int i = 0; i <= nameEnd; i++) {
        name8_3.append(QChar(entry[i]));
    }
    
    // Read extension (remove trailing spaces)
    QString ext;
    int extEnd = 10;
    while (extEnd >= 8 && entry[extEnd] == 0x20) extEnd--;
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
    info.attributes = entry[0x0B];
    info.isDirectory = (info.attributes & 0x10) != 0;
    
    // Read file size (bytes 0x1C-0x1F)
    info.size = entry[0x1C] | (entry[0x1D] << 8) | (entry[0x1E] << 16) | (entry[0x1F] << 24);
    
    // Read first cluster (bytes 0x1A-0x1B for FAT16, 0x1A-0x1D for FAT32)
    // For FAT16, use low word; for FAT32, we'll need to read the high word from offset 0x14
    info.cluster = entry[0x1A] | (entry[0x1B] << 8);
    
    // For FAT32, also read high word (offset 0x14-0x15)
    // Note: This assumes FAT32 - we'd need to detect FAT type to be precise
    quint16 clusterHigh = entry[0x14] | (entry[0x15] << 8);
    if (clusterHigh > 0) {
        info.cluster |= (static_cast<quint32>(clusterHigh) << 16);
    }
    
    // Read date/time
    // Created time: offset 0x0E-0x0F
    // Created date: offset 0x10-0x11
    quint16 createdTime = entry[0x0E] | (entry[0x0F] << 8);
    quint16 createdDate = entry[0x10] | (entry[0x11] << 8);
    
    // Modified date/time: offset 0x18-0x1B
    // Modified time: offset 0x16-0x17
    // Modified date: offset 0x18-0x19
    quint16 modifiedTime = entry[0x16] | (entry[0x17] << 8);
    quint16 modifiedDate = entry[0x18] | (entry[0x19] << 8);
    
    // Parse FAT date/time format (simplified)
    if (modifiedDate != 0) {
        int year = 1980 + ((modifiedDate >> 9) & 0x7F);
        int month = (modifiedDate >> 5) & 0x0F;
        int day = modifiedDate & 0x1F;
        int hour = (modifiedTime >> 11) & 0x1F;
        int minute = (modifiedTime >> 5) & 0x3F;
        int second = (modifiedTime & 0x1F) * 2;
        
        info.modified = QDateTime(QDate(year, month, day), QTime(hour, minute, second));
    }
    
    if (createdDate != 0) {
        int year = 1980 + ((createdDate >> 9) & 0x7F);
        int month = (createdDate >> 5) & 0x0F;
        int day = createdDate & 0x1F;
        int hour = (createdTime >> 11) & 0x1F;
        int minute = (createdTime >> 5) & 0x3F;
        int second = (createdTime & 0x1F) * 2;
        
        info.created = QDateTime(QDate(year, month, day), QTime(hour, minute, second));
    }
    
    return info;
}

QList<FileInfo> QFATFileSystem::listFilesFAT16() {
    if (!m_file.isOpen()) {
        qWarning() << "File not open";
        return QList<FileInfo>();
    }
    
    quint32 rootDirOffset = calculateRootDirOffsetFAT16();
    quint16 rootEntryCount = readRootEntryCount();
    
    if (rootEntryCount == 0) {
        rootEntryCount = 512; // Default if not specified
    }
    
    quint32 rootDirSize = rootEntryCount * 32; // Each entry is 32 bytes
    
    return readDirectoryEntries(rootDirOffset, rootDirSize);
}

QList<FileInfo> QFATFileSystem::listFilesFAT32() {
    if (!m_file.isOpen()) {
        qWarning() << "File not open";
        return QList<FileInfo>();
    }
    
    quint32 rootDirCluster = readRootDirClusterFAT32();
    return listDirectoryFAT32(rootDirCluster);
}

QList<FileInfo> QFATFileSystem::listRootDirectory() {
    // Try to detect FAT type and use appropriate method
    // For now, we'll try FAT32 first, then FAT16
    // In a real implementation, you'd detect based on BPB
    
    // Check FAT32 by looking at root entry count
    m_stream.device()->seek(0x11);
    quint16 rootEntryCount;
    m_stream >> rootEntryCount;
    
    if (rootEntryCount == 0) {
        // FAT32 has root entry count of 0
        return listFilesFAT32();
    } else {
        // FAT16/12 has non-zero root entry count
        return listFilesFAT16();
    }
}

quint32 QFATFileSystem::calculateClusterOffsetFAT16(quint16 cluster) {
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();
    
    m_stream.device()->seek(0x16);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;
    
    quint16 rootEntryCount = readRootEntryCount();
    quint32 rootDirSectors = (rootEntryCount * 32 + bytesPerSector - 1) / bytesPerSector;
    
    // Data area starts after reserved sectors + FATs + root directory
    quint32 dataAreaStart = reservedSectors + (numFATs * sectorsPerFAT) + rootDirSectors;
    quint32 dataAreaOffset = dataAreaStart * bytesPerSector;
    
    // Cluster 2 is first data cluster, so cluster starts at (cluster - 2) * cluster size
    quint32 clusterOffset = (cluster - 2) * sectorsPerCluster * bytesPerSector;
    
    return dataAreaOffset + clusterOffset;
}

quint32 QFATFileSystem::calculateClusterOffsetFAT32(quint32 cluster) {
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();
    
    m_stream.device()->seek(0x24);
    quint32 sectorsPerFAT;
    m_stream >> sectorsPerFAT;
    
    // Calculate data area start
    quint32 dataAreaStart = reservedSectors + (numFATs * sectorsPerFAT);
    quint32 dataAreaOffset = dataAreaStart * bytesPerSector;
    
    // Cluster 2 is first data cluster, so cluster starts at (cluster - 2) * cluster size
    quint32 clusterOffset = (cluster - 2) * sectorsPerCluster * bytesPerSector;
    
    return dataAreaOffset + clusterOffset;
}

quint16 QFATFileSystem::readNextClusterFAT16(quint16 cluster) {
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

quint32 QFATFileSystem::readNextClusterFAT32(quint32 cluster) {
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

QList<FileInfo> QFATFileSystem::readDirectoryEntries(quint32 offset, quint32 maxSize) {
    QList<FileInfo> files;
    
    if (!m_file.isOpen()) {
        return files;
    }
    
    m_stream.device()->seek(offset);
    
    QByteArray buffer(maxSize, 0);
    qint64 bytesRead = m_stream.readRawData(buffer.data(), maxSize);
    
    if (bytesRead <= 0) {
        return files;
    }
    
    quint32 numEntries = bytesRead / 32;
    QString currentLongName;
    
    for (quint32 i = 0; i < numEntries; i++) {
        quint8* entry = reinterpret_cast<quint8*>(buffer.data() + i * 32);
        
        if (entry[0] == 0x00) {
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
            quint8 sequence = entry[0] & 0x1F;
            bool isLastInSequence = (entry[0] & 0x40) != 0;
            
            // Long filename entries appear before the short entry in reverse order
            // The entry with sequence number marked with 0x40 appears first
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

QList<FileInfo> QFATFileSystem::listDirectoryFAT16(quint16 cluster) {
    QList<FileInfo> files;
    
    if (!m_file.isOpen() || cluster < 2) {
        return files;
    }
    
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;
    
    quint16 currentCluster = cluster;
    quint32 totalSize = 0;
    const quint32 maxDirSize = 64 * 1024; // Limit directory size to 64KB
    
    // Follow cluster chain
    while (currentCluster >= 2 && currentCluster < 0xFFF8 && totalSize < maxDirSize) {
        quint32 clusterOffset = calculateClusterOffsetFAT16(currentCluster);
        QList<FileInfo> entries = readDirectoryEntries(clusterOffset, clusterSize);
        
        bool foundEnd = false;
        for (const FileInfo& entry : entries) {
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
        currentCluster = readNextClusterFAT16(currentCluster);
        if (currentCluster == 0) {
            break;
        }
    }
    
    return files;
}

QList<FileInfo> QFATFileSystem::listDirectoryFAT32(quint32 cluster) {
    QList<FileInfo> files;
    
    if (!m_file.isOpen() || cluster < 2) {
        return files;
    }
    
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;
    
    quint32 currentCluster = cluster;
    quint32 totalSize = 0;
    const quint32 maxDirSize = 64 * 1024; // Limit directory size to 64KB
    
    // Follow cluster chain
    while (currentCluster >= 2 && currentCluster < 0x0FFFFFF8 && totalSize < maxDirSize) {
        quint32 clusterOffset = calculateClusterOffsetFAT32(currentCluster);
        QList<FileInfo> entries = readDirectoryEntries(clusterOffset, clusterSize);
        
        bool foundEnd = false;
        for (const FileInfo& entry : entries) {
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
        currentCluster = readNextClusterFAT32(currentCluster);
        if (currentCluster == 0) {
            break;
        }
    }
    
    return files;
}

QList<FileInfo> QFATFileSystem::listDirectory(const QString& path) {
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
