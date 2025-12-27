#include "qfatfilesystem.h"
#include "internal_constants.h"
#include <QDebug>
#include <QFile>

// ============================================================================
// QFAT16FileSystem
// ============================================================================

QFAT16FileSystem::QFAT16FileSystem(QSharedPointer<QIODevice> device)
    : QFATFileSystem(device)
{
}

QScopedPointer<QFAT16FileSystem> QFAT16FileSystem::create(const QString &imagePath)
{
    QSharedPointer<QFile> file(new QFile(imagePath));
    if (!file->open(QIODevice::ReadWrite)) {
        qWarning() << "Failed to open FAT16 image:" << imagePath;
        return QScopedPointer<QFAT16FileSystem>();
    }

    return QScopedPointer<QFAT16FileSystem>(new QFAT16FileSystem(file));
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

QList<QFATFileInfo> QFAT16FileSystem::listRootDirectory()
{
    if (!m_device->isOpen()) {
        qWarning() << "File not open";
        return QList<QFATFileInfo>();
    }

    quint32 rootDirOffset = calculateRootDirOffset();
    quint16 rootEntryCount = readRootEntryCount();

    if (rootEntryCount == 0) {
        rootEntryCount = 512; // Default if not specified
    }

    quint32 rootDirSize = rootEntryCount * ENTRY_SIZE;

    return readDirectoryEntries(rootDirOffset, rootDirSize);
}

QList<QFATFileInfo> QFAT16FileSystem::listDirectory(quint16 cluster)
{
    QList<QFATFileInfo> files;

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
        QList<QFATFileInfo> entries = readDirectoryEntries(clusterOffset, clusterSize);

        bool foundEnd = false;
        for (const QFATFileInfo &entry : entries) {
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

QList<QFATFileInfo> QFAT16FileSystem::listDirectory(const QString &path)
{
    // For empty path or root, list root directory
    if (path.isEmpty() || path == "/" || path == "\\") {
        return listRootDirectory();
    }

    // Use path traversal to find directory
    QFATError error = QFATError::None;
    QFATFileInfo dirInfo = findFileByPath(path, error);

    if (error != QFATError::None || !dirInfo.isDirectory) {
        qWarning() << "Directory not found or not a directory:" << path;
        return QList<QFATFileInfo>();
    }

    return listDirectory(static_cast<quint16>(dirInfo.cluster));
}

QFATFileInfo QFAT16FileSystem::findFileByPath(const QString &path, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return QFATFileInfo();
    }

    QStringList parts = splitPath(path);
    if (parts.isEmpty()) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return QFATFileInfo();
    }

    // Start from root directory
    QList<QFATFileInfo> currentDir = listRootDirectory();

    // Traverse the path
    for (int i = 0; i < parts.size(); i++) {
        QFATFileInfo found = findInDirectory(currentDir, parts[i]);

        // If not found by normal means, check the in-memory mapping for files written without LFN
        if (found.name.isEmpty() && m_longToShortNameMap.contains(parts[i].toLower())) {
            QString shortName = m_longToShortNameMap[parts[i].toLower()];
            qDebug() << "[findFileByPath] Using mapping for" << parts[i] << "->" << shortName;
            found = findInDirectory(currentDir, shortName);
        }

        if (found.name.isEmpty()) {
            error = (i < parts.size() - 1) ? QFATError::DirectoryNotFound : QFATError::FileNotFound;
            m_lastError = error;
            return QFATFileInfo();
        }

        // If this is the last component, return it
        if (i == parts.size() - 1) {
            return found;
        }

        // Otherwise, it must be a directory
        if (!found.isDirectory) {
            error = QFATError::DirectoryNotFound;
            m_lastError = error;
            return QFATFileInfo();
        }

        // Move to the subdirectory
        currentDir = listDirectory(static_cast<quint16>(found.cluster));
    }

    error = QFATError::FileNotFound;
    m_lastError = error;
    return QFATFileInfo();
}

QList<quint16> QFAT16FileSystem::getClusterChain(quint16 startCluster)
{
    QList<quint16> chain;

    if (startCluster < 2) {
        return chain;
    }

    quint16 currentCluster = startCluster;
    const int maxClusters = 65536; // Safety limit

    while (currentCluster >= 2 && currentCluster < 0xFFF8 && chain.size() < maxClusters) {
        chain.append(currentCluster);
        currentCluster = readNextCluster(currentCluster);

        if (currentCluster == 0) {
            break;
        }
    }

    return chain;
}

QByteArray QFAT16FileSystem::readClusterChain(quint16 startCluster, quint32 fileSize)
{
    QByteArray data;

    if (startCluster < 2 || fileSize == 0) {
        return data;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    QList<quint16> clusters = getClusterChain(startCluster);
    quint32 bytesRead = 0;

    for (quint16 cluster : clusters) {
        quint32 clusterOffset = calculateClusterOffset(cluster);
        m_stream.device()->seek(clusterOffset);

        quint32 bytesToRead = qMin(clusterSize, fileSize - bytesRead);
        QByteArray clusterData(bytesToRead, 0);

        qint64 actualRead = m_stream.readRawData(clusterData.data(), bytesToRead);
        if (actualRead <= 0) {
            qWarning() << "Failed to read cluster" << cluster;
            break;
        }

        data.append(clusterData);
        bytesRead += actualRead;

        if (bytesRead >= fileSize) {
            break;
        }
    }

    return data;
}

QByteArray QFAT16FileSystem::readFile(const QString &path, QFATError &error)
{
    error = QFATError::None;

    QFATFileInfo fileInfo = findFileByPath(path, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return QByteArray();
    }

    if (fileInfo.isDirectory) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return QByteArray();
    }

    // Empty file
    if (fileInfo.size == 0 || fileInfo.cluster < 2) {
        return QByteArray();
    }

    return readClusterChain(static_cast<quint16>(fileInfo.cluster), fileInfo.size);
}

quint16 QFAT16FileSystem::findFreeCluster()
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint32 fatOffset = reservedSectors * bytesPerSector;
    quint32 totalClusters = (sectorsPerFAT * bytesPerSector) / 2; // 2 bytes per FAT16 entry

    // Start from cluster 2 (first valid data cluster)
    for (quint16 cluster = 2; cluster < totalClusters && cluster < 0xFFF0; cluster++) {
        m_stream.device()->seek(fatOffset + cluster * 2);
        quint16 value;
        m_stream >> value;

        if (value == 0) {
            return cluster;
        }
    }

    return 0; // No free cluster found
}

bool QFAT16FileSystem::writeNextCluster(quint16 cluster, quint16 value)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();
    quint32 fatOffset = reservedSectors * bytesPerSector + cluster * 2;

    quint8 numFATs = readNumberOfFATs();

    // Write to all FAT copies
    for (quint8 i = 0; i < numFATs; i++) {
        m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
        quint16 sectorsPerFAT;
        m_stream >> sectorsPerFAT;

        quint32 fatCopyOffset = fatOffset + (i * sectorsPerFAT * bytesPerSector);
        m_stream.device()->seek(fatCopyOffset);
        m_stream << value;
    }

    return m_stream.status() == QDataStream::Ok;
}

bool QFAT16FileSystem::writeClusterData(quint16 cluster, const QByteArray &data, quint32 offset)
{
    quint32 clusterOffset = calculateClusterOffset(cluster);
    m_stream.device()->seek(clusterOffset + offset);

    qint64 written = m_stream.writeRawData(data.constData(), data.size());
    return written == data.size();
}

QList<quint16> QFAT16FileSystem::allocateClusterChain(quint32 numClusters)
{
    QList<quint16> chain;

    if (numClusters == 0) {
        return chain;
    }

    // Find and allocate clusters
    for (quint32 i = 0; i < numClusters; i++) {
        quint16 freeCluster = findFreeCluster();
        if (freeCluster == 0) {
            // No more free clusters, free what we allocated and return empty
            if (!chain.isEmpty()) {
                freeClusterChain(chain.first());
            }
            return QList<quint16>();
        }

        chain.append(freeCluster);

        // Mark cluster as used (write 0xFFFF for end of chain, or link to next)
        if (i == numClusters - 1) {
            writeNextCluster(freeCluster, 0xFFFF); // End of chain
        } else {
            // We'll update this once we know the next cluster
            writeNextCluster(freeCluster, 0xFFFF);
        }
    }

    // Link the chain
    for (int i = 0; i < chain.size() - 1; i++) {
        writeNextCluster(chain[i], chain[i + 1]);
    }

    return chain;
}

bool QFAT16FileSystem::freeClusterChain(quint16 startCluster)
{
    QList<quint16> chain = getClusterChain(startCluster);

    for (quint16 cluster : chain) {
        if (!writeNextCluster(cluster, 0)) {
            return false;
        }
    }

    return true;
}

bool QFAT16FileSystem::createDirectoryEntry(quint32 dirOffset, const QFATFileInfo &fileInfo)
{
    // This is a simplified implementation - doesn't handle long file names
    quint8 entry[ENTRY_SIZE];
    memset(entry, 0, ENTRY_SIZE);

    // Write short name (8.3 format)
    QString name = fileInfo.name.toUpper();
    QString baseName, ext;

    int dotPos = name.indexOf('.');
    if (dotPos >= 0) {
        baseName = name.left(dotPos);
        ext = name.mid(dotPos + 1);
    } else {
        baseName = name;
    }

    // Pad with spaces
    baseName = baseName.leftJustified(8, ' ');
    ext = ext.leftJustified(3, ' ');

    for (int i = 0; i < 8; i++) {
        entry[i] = (i < baseName.length()) ? baseName[i].toLatin1() : ' ';
    }
    for (int i = 0; i < 3; i++) {
        entry[8 + i] = (i < ext.length()) ? ext[i].toLatin1() : ' ';
    }

    // Attributes
    entry[ENTRY_ATTRIBUTE_OFFSET] = fileInfo.attributes;

    // Timestamps
    quint16 modDate, modTime, createDate, createTime;
    encodeFATDateTime(fileInfo.modified.isValid() ? fileInfo.modified : QDateTime::currentDateTime(), modDate, modTime);
    encodeFATDateTime(fileInfo.created.isValid() ? fileInfo.created : QDateTime::currentDateTime(), createDate, createTime);

    entry[ENTRY_CREATION_DATE_TIME_OFFSET] = createTime & 0xFF;
    entry[ENTRY_CREATION_DATE_TIME_OFFSET + 1] = (createTime >> 8) & 0xFF;
    entry[ENTRY_CREATION_DATE_TIME_OFFSET + 2] = createDate & 0xFF;
    entry[ENTRY_CREATION_DATE_TIME_OFFSET + 3] = (createDate >> 8) & 0xFF;

    entry[ENTRY_WRITTEN_DATE_TIME_OFFSET] = modTime & 0xFF;
    entry[ENTRY_WRITTEN_DATE_TIME_OFFSET + 1] = (modTime >> 8) & 0xFF;
    entry[ENTRY_WRITTEN_DATE_TIME_OFFSET + 2] = modDate & 0xFF;
    entry[ENTRY_WRITTEN_DATE_TIME_OFFSET + 3] = (modDate >> 8) & 0xFF;

    // Cluster
    quint16 cluster = static_cast<quint16>(fileInfo.cluster & 0xFFFF);
    entry[ENTRY_CLUSTER_OFFSET] = cluster & 0xFF;
    entry[ENTRY_CLUSTER_OFFSET + 1] = (cluster >> 8) & 0xFF;

    // Size
    entry[ENTRY_SIZE_OFFSET] = fileInfo.size & 0xFF;
    entry[ENTRY_SIZE_OFFSET + 1] = (fileInfo.size >> 8) & 0xFF;
    entry[ENTRY_SIZE_OFFSET + 2] = (fileInfo.size >> 16) & 0xFF;
    entry[ENTRY_SIZE_OFFSET + 3] = (fileInfo.size >> 24) & 0xFF;

    // Write entry to directory
    m_stream.device()->seek(dirOffset);
    qint64 written = m_stream.writeRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

    return written == ENTRY_SIZE;
}

bool QFAT16FileSystem::updateDirectoryEntry(const QString &parentPath, const QFATFileInfo &fileInfo)
{
    // Find the directory
    quint32 dirOffset;
    quint32 maxEntries;

    if (parentPath.isEmpty() || parentPath == "/" || parentPath == "\\") {
        dirOffset = calculateRootDirOffset();
        maxEntries = readRootEntryCount();
    } else {
        QFATError error;
        QFATFileInfo dirInfo = findFileByPath(parentPath, error);
        if (error != QFATError::None || !dirInfo.isDirectory) {
            return false;
        }
        dirOffset = calculateClusterOffset(static_cast<quint16>(dirInfo.cluster));

        // For cluster-based directories, calculate max entries per cluster
        quint16 bytesPerSector = readBytesPerSector();
        quint8 sectorsPerCluster = readSectorsPerCluster();
        quint32 clusterSize = bytesPerSector * sectorsPerCluster;
        maxEntries = clusterSize / ENTRY_SIZE;
    }

    // Determine if we need LFN entries
    // Only write LFN for files with numeric tails (e.g., TESTFI~1.TXT) where the long name
    // differs from the short name. This is essential for distinguishing files with similar names.
    bool needsLFN = (fileInfo.name.contains('~') &&
                     !fileInfo.longName.isEmpty() &&
                     fileInfo.longName.toUpper() != fileInfo.name.toUpper());
    qDebug() << "[updateDirectoryEntry] File:" << fileInfo.longName << "Short:" << fileInfo.name << "needsLFN:" << needsLFN;
    int lfnEntriesNeeded = needsLFN ? calculateLFNEntriesNeeded(fileInfo.longName) : 0;
    int totalEntriesNeeded = lfnEntriesNeeded + 1; // LFN entries + short name entry

    // Find existing entry by name, or find consecutive free slots
    quint32 entryOffset = dirOffset;
    quint32 freeSlotOffset = 0;
    int consecutiveFreeSlots = 0;
    bool foundExisting = false;
    bool foundFree = false;

    for (quint32 i = 0; i < maxEntries; i++) {
        m_stream.device()->seek(entryOffset);
        quint8 entry[ENTRY_SIZE];
        m_stream.readRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

        quint8 firstByte = entry[ENTRY_NAME_OFFSET];

        // Check for free or deleted entries
        if (firstByte == ENTRY_END_OF_DIRECTORY || firstByte == ENTRY_DELETED) {
            if (consecutiveFreeSlots == 0) {
                freeSlotOffset = entryOffset;
            }
            consecutiveFreeSlots++;

            // Check if we have enough consecutive slots
            if (consecutiveFreeSlots >= totalEntriesNeeded) {
                foundFree = true;
            }

            if (firstByte == ENTRY_END_OF_DIRECTORY) {
                break;
            }

            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Reset consecutive count when we hit a non-free entry
        consecutiveFreeSlots = 0;

        // Skip long file name entries
        if (isLongFileNameEntry(entry)) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Parse the entry name
        QString name8_3;
        int nameEnd = 7;
        while (nameEnd >= 0 && entry[nameEnd] == ' ')
            nameEnd--;
        for (int j = 0; j <= nameEnd; j++) {
            name8_3.append(QChar(entry[j]));
        }

        QString ext;
        int extEnd = 10;
        while (extEnd >= 8 && entry[extEnd] == ' ')
            extEnd--;
        for (int j = 8; j <= extEnd; j++) {
            ext.append(QChar(entry[j]));
        }

        if (!ext.isEmpty()) {
            if (!name8_3.isEmpty()) {
                name8_3.append('.');
            }
            name8_3.append(ext);
        }

        // Check if this matches the file we're updating
        if (name8_3.toUpper() == fileInfo.name.toUpper()) {
            foundExisting = true;
            break;
        }

        entryOffset += ENTRY_SIZE;
    }

    // Use existing entry offset if found, otherwise use free slot
    if (foundExisting) {
        // For existing entries, just update the short name entry (keep LFN as-is for now)
        return createDirectoryEntry(entryOffset, fileInfo);
    } else if (foundFree) {
        // Write LFN entries if needed, followed by short name entry
        if (needsLFN) {
            qDebug() << "[updateDirectoryEntry] Writing LFN for" << fileInfo.longName << "short:" << fileInfo.name << "entries:" << lfnEntriesNeeded;
            quint8 checksum = calculateLFNChecksum(fileInfo.name);
            quint32 currentOffset = freeSlotOffset;

            // Write LFN entries in reverse order (highest sequence first)
            for (int seq = lfnEntriesNeeded; seq >= 1; seq--) {
                quint8 lfnEntry[ENTRY_SIZE];
                writeLFNEntry(lfnEntry, fileInfo.longName, seq, checksum, seq == lfnEntriesNeeded);

                m_stream.device()->seek(currentOffset);
                m_stream.writeRawData(reinterpret_cast<char*>(lfnEntry), ENTRY_SIZE);

                currentOffset += ENTRY_SIZE;
            }

            // Write short name entry after LFN entries
            return createDirectoryEntry(currentOffset, fileInfo);
        } else {
            // Writing without LFN - store mapping if long and short names differ
            if (!fileInfo.longName.isEmpty() && fileInfo.longName.toLower() != fileInfo.name.toLower()) {
                m_longToShortNameMap[fileInfo.longName.toLower()] = fileInfo.name;
                qDebug() << "[updateDirectoryEntry] Stored mapping (no LFN):" << fileInfo.longName.toLower() << "->" << fileInfo.name;
            }
            return createDirectoryEntry(freeSlotOffset, fileInfo);
        }
    }

    // If we need LFN but couldn't find consecutive space, try to find a single slot
    // and write short-name-only as a fallback
    if (needsLFN && !foundFree) {
        qDebug() << "[updateDirectoryEntry] No consecutive space for LFN, trying fallback for" << fileInfo.longName;
        // Re-scan for a single free slot
        entryOffset = dirOffset;
        for (quint32 i = 0; i < maxEntries; i++) {
            m_stream.device()->seek(entryOffset);
            quint8 entry[ENTRY_SIZE];
            m_stream.readRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

            quint8 firstByte = entry[ENTRY_NAME_OFFSET];
            if (firstByte == ENTRY_END_OF_DIRECTORY || firstByte == ENTRY_DELETED) {
                // Found a single free slot, write short name only
                qDebug() << "[updateDirectoryEntry] Found single slot at offset" << entryOffset << ", writing short-name-only";
                // Store mapping so we can find this file by long name later
                m_longToShortNameMap[fileInfo.longName.toLower()] = fileInfo.name;
                qDebug() << "[updateDirectoryEntry] Stored mapping:" << fileInfo.longName.toLower() << "->" << fileInfo.name;
                return createDirectoryEntry(entryOffset, fileInfo);
            }
            entryOffset += ENTRY_SIZE;
        }
        qDebug() << "[updateDirectoryEntry] No free slots found at all!";
    }

    qDebug() << "[updateDirectoryEntry] Returning false - foundExisting:" << foundExisting << "foundFree:" << foundFree << "needsLFN:" << needsLFN;
    return false;
}

bool QFAT16FileSystem::writeFile(const QString &path, const QByteArray &data, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Parse path
    QStringList parts = splitPath(path);
    if (parts.isEmpty()) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    QString fileName = parts.last();
    QString parentPath;

    if (parts.size() > 1) {
        parts.removeLast();
        parentPath = "/" + parts.join("/");
    } else {
        parentPath = "/";
    }

    // Check if file already exists
    QFATError checkError;
    QFATFileInfo existingFile = findFileByPath(path, checkError);
    bool fileExists = (checkError == QFATError::None);

    // Reset error state
    error = QFATError::None;

    // Calculate required clusters
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = bytesPerSector * sectorsPerCluster;
    quint32 numClusters = (data.size() + clusterSize - 1) / clusterSize;

    // Free old cluster chain if file exists
    if (fileExists && existingFile.cluster >= 2) {
        freeClusterChain(static_cast<quint16>(existingFile.cluster));
    }

    // Allocate new cluster chain (only if we have data)
    QList<quint16> clusters;
    quint16 firstCluster = 0;

    if (numClusters > 0) {
        clusters = allocateClusterChain(numClusters);
        if (clusters.isEmpty()) {
            error = QFATError::InsufficientSpace;
            m_lastError = error;
            return false;
        }
        firstCluster = clusters.first();

        // Write data to clusters
        quint32 bytesWritten = 0;
        for (int i = 0; i < clusters.size(); i++) {
            quint32 bytesToWrite = qMin(clusterSize, static_cast<quint32>(data.size()) - bytesWritten);
            QByteArray clusterData = data.mid(bytesWritten, bytesToWrite);

            // Pad cluster with zeros if needed
            if (clusterData.size() < static_cast<int>(clusterSize)) {
                clusterData.append(QByteArray(clusterSize - clusterData.size(), 0));
            }

            if (!writeClusterData(clusters[i], clusterData)) {
                error = QFATError::WriteError;
                m_lastError = error;
                freeClusterChain(clusters.first());
                return false;
            }

            bytesWritten += bytesToWrite;
        }
    }

    // Create/update directory entry
    QFATFileInfo fileInfo;
    QFATError parentError;
    QList<QFATFileInfo> parentEntries;

    if (parentPath == "/") {
        parentEntries = listRootDirectory();
    } else {
        QFATFileInfo parentInfo = findFileByPath(parentPath, parentError);
        if (parentError == QFATError::None && parentInfo.isDirectory) {
            parentEntries = listDirectory(static_cast<quint16>(parentInfo.cluster));
        }
    }

    // Use existing file info if file exists, otherwise generate new short name
    if (fileExists) {
        fileInfo.name = existingFile.name;
        // Always use the provided fileName as the long name, not the existing one
        // This ensures we don't carry over garbage LFN data
        fileInfo.longName = fileName;
    } else {
        fileInfo.name = generateShortName(fileName, parentEntries);
        fileInfo.longName = fileName;
    }

    fileInfo.isDirectory = false;
    fileInfo.size = data.size();
    fileInfo.cluster = firstCluster;
    fileInfo.attributes = ENTRY_ATTRIBUTE_ARCHIVE;
    fileInfo.modified = QDateTime::currentDateTime();
    fileInfo.created = fileExists ? existingFile.created : QDateTime::currentDateTime();

    if (!updateDirectoryEntry(parentPath, fileInfo)) {
        error = QFATError::WriteError;
        m_lastError = error;
        if (firstCluster >= 2) {
            freeClusterChain(firstCluster);
        }
        return false;
    }

    return true;
}

bool QFAT16FileSystem::exists(const QString &path)
{
    QFATError error;
    QFATFileInfo info = findFileByPath(path, error);
    return error == QFATError::None && !info.name.isEmpty();
}

QFATFileInfo QFAT16FileSystem::getFileInfo(const QString &path, QFATError &error)
{
    return findFileByPath(path, error);
}

bool QFAT16FileSystem::deleteDirectoryEntry(const QString &path)
{
    if (!m_device->isOpen()) {
        return false;
    }

    // Parse path to get parent and filename
    QStringList parts = splitPath(path);
    if (parts.isEmpty()) {
        return false;
    }

    QString fileName = parts.last();
    QString parentPath;

    if (parts.size() > 1) {
        parts.removeLast();
        parentPath = "/" + parts.join("/");
    } else {
        parentPath = "/";
    }

    // Find the directory offset and max entries
    quint32 dirOffset;
    quint32 maxEntries;

    if (parentPath.isEmpty() || parentPath == "/" || parentPath == "\\") {
        dirOffset = calculateRootDirOffset();
        maxEntries = readRootEntryCount();
    } else {
        QFATError error;
        QFATFileInfo dirInfo = findFileByPath(parentPath, error);
        if (error != QFATError::None || !dirInfo.isDirectory) {
            return false;
        }
        dirOffset = calculateClusterOffset(static_cast<quint16>(dirInfo.cluster));

        // For cluster-based directories, calculate max entries
        quint16 bytesPerSector = readBytesPerSector();
        quint8 sectorsPerCluster = readSectorsPerCluster();
        quint32 clusterSize = bytesPerSector * sectorsPerCluster;
        maxEntries = clusterSize / ENTRY_SIZE;
    }

    // Scan raw directory data to find the entry
    quint32 entryOffset = dirOffset;
    bool found = false;
    quint32 foundOffset = 0;

    for (quint32 i = 0; i < maxEntries; i++) {
        m_stream.device()->seek(entryOffset);
        quint8 entryData[ENTRY_SIZE];
        m_stream.readRawData(reinterpret_cast<char*>(entryData), ENTRY_SIZE);

        quint8 firstByte = entryData[ENTRY_NAME_OFFSET];

        // Stop at end of directory
        if (firstByte == ENTRY_END_OF_DIRECTORY) {
            break;
        }

        // Skip deleted entries
        if (firstByte == ENTRY_DELETED) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Skip long file name entries
        if (isLongFileNameEntry(entryData)) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Parse the entry name
        QString name8_3;
        int nameEnd = 7;
        while (nameEnd >= 0 && entryData[nameEnd] == ' ')
            nameEnd--;
        for (int j = 0; j <= nameEnd; j++) {
            name8_3.append(QChar(entryData[j]));
        }

        QString ext;
        int extEnd = 10;
        while (extEnd >= 8 && entryData[extEnd] == ' ')
            extEnd--;
        for (int j = 8; j <= extEnd; j++) {
            ext.append(QChar(entryData[j]));
        }

        if (!ext.isEmpty()) {
            if (!name8_3.isEmpty()) {
                name8_3.append('.');
            }
            name8_3.append(ext);
        }

        // Check if this matches the file we're looking for
        if (name8_3.toUpper() == fileName.toUpper()) {
            found = true;
            foundOffset = entryOffset;
            break;
        }

        entryOffset += ENTRY_SIZE;
    }

    if (!found) {
        return false;
    }

    // Mark entry as deleted
    m_stream.device()->seek(foundOffset);
    quint8 deletedMarker = ENTRY_DELETED;
    m_stream.writeRawData(reinterpret_cast<char*>(&deletedMarker), 1);

    return m_stream.status() == QDataStream::Ok;
}

QString QFAT16FileSystem::modifyDirectoryEntryName(const QString &path, const QString &newName)
{
    if (!m_device->isOpen()) {
        return QString();
    }

    // Parse path to get parent and filename
    QStringList parts = splitPath(path);
    if (parts.isEmpty()) {
        return QString();
    }

    QString fileName = parts.last();
    QString parentPath;

    if (parts.size() > 1) {
        parts.removeLast();
        parentPath = "/" + parts.join("/");
    } else {
        parentPath = "/";
    }

    // Find the directory offset and max entries
    quint32 dirOffset;
    quint32 maxEntries;

    if (parentPath.isEmpty() || parentPath == "/" || parentPath == "\\") {
        dirOffset = calculateRootDirOffset();
        maxEntries = readRootEntryCount();
    } else {
        QFATError error;
        QFATFileInfo dirInfo = findFileByPath(parentPath, error);
        if (error != QFATError::None || !dirInfo.isDirectory) {
            return QString();
        }
        dirOffset = calculateClusterOffset(static_cast<quint16>(dirInfo.cluster));

        // For cluster-based directories, calculate max entries
        quint16 bytesPerSector = readBytesPerSector();
        quint8 sectorsPerCluster = readSectorsPerCluster();
        quint32 clusterSize = bytesPerSector * sectorsPerCluster;
        maxEntries = clusterSize / ENTRY_SIZE;
    }

    // Get existing entries to generate a proper short name
    QList<QFATFileInfo> existingEntries;
    if (parentPath.isEmpty() || parentPath == "/" || parentPath == "\\") {
        existingEntries = listRootDirectory();
    } else {
        QFATError error;
        QFATFileInfo parentInfo = findFileByPath(parentPath, error);
        if (error == QFATError::None && parentInfo.isDirectory) {
            existingEntries = listDirectory(static_cast<quint16>(parentInfo.cluster));
        }
    }

    QString newShortName = generateShortName(newName, existingEntries);

    // Scan raw directory data to find the entry
    quint32 entryOffset = dirOffset;
    bool found = false;
    quint32 foundOffset = 0;

    for (quint32 i = 0; i < maxEntries; i++) {
        m_stream.device()->seek(entryOffset);
        quint8 entryData[ENTRY_SIZE];
        m_stream.readRawData(reinterpret_cast<char*>(entryData), ENTRY_SIZE);

        quint8 firstByte = entryData[ENTRY_NAME_OFFSET];

        // Stop at end of directory
        if (firstByte == ENTRY_END_OF_DIRECTORY) {
            break;
        }

        // Skip deleted entries
        if (firstByte == ENTRY_DELETED) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Skip long file name entries
        if (isLongFileNameEntry(entryData)) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Parse the entry name
        QString name8_3;
        int nameEnd = 7;
        while (nameEnd >= 0 && entryData[nameEnd] == ' ')
            nameEnd--;
        for (int j = 0; j <= nameEnd; j++) {
            name8_3.append(QChar(entryData[j]));
        }

        QString ext;
        int extEnd = 10;
        while (extEnd >= 8 && entryData[extEnd] == ' ')
            extEnd--;
        for (int j = 8; j <= extEnd; j++) {
            ext.append(QChar(entryData[j]));
        }

        if (!ext.isEmpty()) {
            if (!name8_3.isEmpty()) {
                name8_3.append('.');
            }
            name8_3.append(ext);
        }

        // Check if this matches the file we're looking for
        if (name8_3.toUpper() == fileName.toUpper()) {
            found = true;
            foundOffset = entryOffset;
            break;
        }

        entryOffset += ENTRY_SIZE;
    }

    if (!found) {
        return QString();
    }

    // Update the entry name
    m_stream.device()->seek(foundOffset);
    quint8 newEntryData[ENTRY_SIZE];
    m_stream.readRawData(reinterpret_cast<char*>(newEntryData), ENTRY_SIZE);

    // Parse short name (8.3 format)
    QStringList nameParts = newShortName.toUpper().split('.');
    QString baseName = nameParts.isEmpty() ? "" : nameParts[0];
    QString extension = nameParts.size() > 1 ? nameParts[1] : "";

    // Fill name (8 bytes, space-padded)
    for (int i = 0; i < 8; i++) {
        if (i < baseName.length()) {
            newEntryData[i] = baseName[i].toLatin1();
        } else {
            newEntryData[i] = ' ';
        }
    }

    // Fill extension (3 bytes, space-padded)
    for (int i = 0; i < 3; i++) {
        if (i < extension.length()) {
            newEntryData[8 + i] = extension[i].toLatin1();
        } else {
            newEntryData[8 + i] = ' ';
        }
    }

    // Write the modified entry back
    m_stream.device()->seek(foundOffset);
    m_stream.writeRawData(reinterpret_cast<char*>(newEntryData), ENTRY_SIZE);

    if (m_stream.status() != QDataStream::Ok) {
        return QString();
    }

    return newShortName;
}

bool QFAT16FileSystem::isDirectoryEmpty(quint16 cluster)
{
    if (cluster < 2) {
        return true;
    }

    QList<QFATFileInfo> entries = listDirectory(cluster);

    // An empty directory should have no entries (. and .. are filtered out by listDirectory)
    return entries.isEmpty();
}

bool QFAT16FileSystem::deleteFile(const QString &path, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Find the file
    QFATFileInfo fileInfo = findFileByPath(path, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return false;
    }

    // Can't delete a directory with deleteFile
    if (fileInfo.isDirectory) {
        // Check if it's empty before deleting
        if (!isDirectoryEmpty(static_cast<quint16>(fileInfo.cluster))) {
            error = QFATError::InvalidPath;
            m_lastError = error;
            return false;
        }
    }

    // Free the cluster chain
    if (fileInfo.cluster >= 2) {
        if (!freeClusterChain(static_cast<quint16>(fileInfo.cluster))) {
            error = QFATError::WriteError;
            m_lastError = error;
            return false;
        }
    }

    // Delete the directory entry
    if (!deleteDirectoryEntry(path)) {
        error = QFATError::WriteError;
        m_lastError = error;
        return false;
    }

    return true;
}

bool QFAT16FileSystem::createDirectory(const QString &path, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Check if directory already exists
    QFATError checkError;
    QFATFileInfo existingDir = findFileByPath(path, checkError);
    if (checkError == QFATError::None) {
        error = QFATError::InvalidPath; // Already exists
        m_lastError = error;
        return false;
    }

    // Reset error state
    error = QFATError::None;

    // Parse path
    QStringList parts = splitPath(path);
    if (parts.isEmpty()) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    QString dirName = parts.last();
    QString parentPath;

    if (parts.size() > 1) {
        parts.removeLast();
        parentPath = "/" + parts.join("/");
    } else {
        parentPath = "/";
    }

    // Verify parent exists
    if (parentPath != "/") {
        QFATError parentError;
        QFATFileInfo parentInfo = findFileByPath(parentPath, parentError);
        if (parentError != QFATError::None || !parentInfo.isDirectory) {
            error = QFATError::DirectoryNotFound;
            m_lastError = error;
            return false;
        }
    }

    // Allocate one cluster for the directory
    QList<quint16> clusters = allocateClusterChain(1);
    if (clusters.isEmpty()) {
        error = QFATError::InsufficientSpace;
        m_lastError = error;
        return false;
    }

    quint16 dirCluster = clusters.first();

    // Initialize directory with . and .. entries
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = bytesPerSector * sectorsPerCluster;

    QByteArray dirData(clusterSize, 0);
    quint8 *dirPtr = reinterpret_cast<quint8*>(dirData.data());

    // Create . entry (current directory)
    memset(dirPtr, ' ', ENTRY_SIZE);
    dirPtr[0] = '.';
    dirPtr[ENTRY_ATTRIBUTE_OFFSET] = ENTRY_ATTRIBUTE_DIRECTORY;
    quint16 cluster = dirCluster;
    dirPtr[ENTRY_CLUSTER_OFFSET] = cluster & 0xFF;
    dirPtr[ENTRY_CLUSTER_OFFSET + 1] = (cluster >> 8) & 0xFF;

    // Create .. entry (parent directory)
    dirPtr += ENTRY_SIZE;
    memset(dirPtr, ' ', ENTRY_SIZE);
    dirPtr[0] = '.';
    dirPtr[1] = '.';
    dirPtr[ENTRY_ATTRIBUTE_OFFSET] = ENTRY_ATTRIBUTE_DIRECTORY;

    // For root directory, parent cluster is 0
    quint16 parentCluster = 0;
    if (parentPath != "/") {
        QFATError parentError;
        QFATFileInfo parentInfo = findFileByPath(parentPath, parentError);
        if (parentError == QFATError::None) {
            parentCluster = static_cast<quint16>(parentInfo.cluster);
        }
    }
    dirPtr[ENTRY_CLUSTER_OFFSET] = parentCluster & 0xFF;
    dirPtr[ENTRY_CLUSTER_OFFSET + 1] = (parentCluster >> 8) & 0xFF;

    // Write directory data
    if (!writeClusterData(dirCluster, dirData)) {
        error = QFATError::WriteError;
        m_lastError = error;
        freeClusterChain(dirCluster);
        return false;
    }

    // Create directory entry in parent
    QFATFileInfo dirInfo;
    QFATError parentError;
    QList<QFATFileInfo> parentEntries;

    if (parentPath == "/") {
        parentEntries = listRootDirectory();
    } else {
        QFATFileInfo parentInfo = findFileByPath(parentPath, parentError);
        if (parentError == QFATError::None && parentInfo.isDirectory) {
            parentEntries = listDirectory(static_cast<quint16>(parentInfo.cluster));
        }
    }

    dirInfo.name = generateShortName(dirName, parentEntries);
    dirInfo.longName = dirName;
    dirInfo.isDirectory = true;
    dirInfo.size = 0; // Directories have size 0
    dirInfo.cluster = dirCluster;
    dirInfo.attributes = ENTRY_ATTRIBUTE_DIRECTORY;
    dirInfo.modified = QDateTime::currentDateTime();
    dirInfo.created = QDateTime::currentDateTime();

    if (!updateDirectoryEntry(parentPath, dirInfo)) {
        error = QFATError::WriteError;
        m_lastError = error;
        freeClusterChain(dirCluster);
        return false;
    }

    return true;
}

QByteArray QFAT16FileSystem::readFilePartial(const QString &path, quint32 offset, quint32 length, QFATError &error)
{
    error = QFATError::None;

    QFATFileInfo fileInfo = findFileByPath(path, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return QByteArray();
    }

    if (fileInfo.isDirectory) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return QByteArray();
    }

    // Empty file or offset beyond file size
    if (fileInfo.size == 0 || offset >= fileInfo.size || fileInfo.cluster < 2) {
        return QByteArray();
    }

    // Adjust length if it exceeds file size
    quint32 actualLength = qMin(length, fileInfo.size - offset);

    // Read the entire file first (we can optimize this later to skip clusters)
    QByteArray fullData = readClusterChain(static_cast<quint16>(fileInfo.cluster), fileInfo.size);
    if (fullData.isEmpty() && fileInfo.size > 0) {
        error = QFATError::ReadError;
        m_lastError = error;
        return QByteArray();
    }

    // Return the requested portion
    return fullData.mid(offset, actualLength);
}

bool QFAT16FileSystem::renameFile(const QString &oldPath, const QString &newPath, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Check if source exists
    QFATFileInfo fileInfo = findFileByPath(oldPath, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return false;
    }

    // Parse old and new paths
    QStringList oldParts = splitPath(oldPath);
    QStringList newParts = splitPath(newPath);

    if (oldParts.isEmpty() || newParts.isEmpty()) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    QString oldParentPath = oldParts.size() > 1 ? "/" + oldParts.mid(0, oldParts.size() - 1).join("/") : "/";
    QString newParentPath = newParts.size() > 1 ? "/" + newParts.mid(0, newParts.size() - 1).join("/") : "/";

    // If parent directories differ, this is a move operation
    if (oldParentPath != newParentPath) {
        return moveFile(oldPath, newPath, error);
    }

    // Same directory - just rename
    QString newName = newParts.last();

    // Check if new name already exists
    QFATError existError;
    findFileByPath(newPath, existError);
    if (existError == QFATError::None) {
        error = QFATError::InvalidPath; // Already exists
        m_lastError = error;
        return false;
    }

    // Update the directory entry name in place
    QString newShortName = modifyDirectoryEntryName(oldPath, newName);
    if (newShortName.isEmpty()) {
        error = QFATError::WriteError;
        m_lastError = error;
        return false;
    }

    // Update in-memory mapping
    QString oldNameLower = oldParts.last().toLower();
    m_longToShortNameMap.remove(oldNameLower);
    m_longToShortNameMap[newName.toLower()] = newShortName;

    return true;
}

bool QFAT16FileSystem::moveFile(const QString &sourcePath, const QString &destPath, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Read the file data
    QFATFileInfo sourceInfo = findFileByPath(sourcePath, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return false;
    }

    // Check if destination already exists
    QFATError destError;
    findFileByPath(destPath, destError);
    if (destError == QFATError::None) {
        error = QFATError::InvalidPath; // Destination already exists
        m_lastError = error;
        return false;
    }

    if (sourceInfo.isDirectory) {
        // For directories, we need to update the cluster reference
        // Create new directory entry in destination
        QStringList destParts = splitPath(destPath);
        QString destParentPath = destParts.size() > 1 ? "/" + destParts.mid(0, destParts.size() - 1).join("/") : "/";

        // Verify destination parent exists
        if (destParentPath != "/") {
            QFATError parentError;
            QFATFileInfo parentInfo = findFileByPath(destParentPath, parentError);
            if (parentError != QFATError::None || !parentInfo.isDirectory) {
                error = QFATError::DirectoryNotFound;
                m_lastError = error;
                return false;
            }
        }

        // Create new entry with same cluster
        sourceInfo.longName = destParts.last();
        if (!updateDirectoryEntry(destParentPath, sourceInfo)) {
            error = QFATError::WriteError;
            m_lastError = error;
            return false;
        }

        // Delete old entry (but don't free clusters)
        if (!deleteDirectoryEntry(sourcePath)) {
            error = QFATError::WriteError;
            m_lastError = error;
            return false;
        }
    } else {
        // For files, verify destination parent exists
        QStringList destParts = splitPath(destPath);
        QString destParentPath = destParts.size() > 1 ? "/" + destParts.mid(0, destParts.size() - 1).join("/") : "/";

        if (destParentPath != "/") {
            QFATError parentError;
            QFATFileInfo parentInfo = findFileByPath(destParentPath, parentError);
            if (parentError != QFATError::None || !parentInfo.isDirectory) {
                error = QFATError::DirectoryNotFound;
                m_lastError = error;
                return false;
            }
        }

        // Read and write to new location
        QByteArray data = readFile(sourcePath, error);
        if (error != QFATError::None) {
            m_lastError = error;
            return false;
        }

        if (!writeFile(destPath, data, error)) {
            m_lastError = error;
            return false;
        }

        // Delete source file
        if (!deleteFile(sourcePath, error)) {
            m_lastError = error;
            return false;
        }
    }

    return true;
}

bool QFAT16FileSystem::deleteDirectory(const QString &path, bool recursive, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Find the directory
    QFATFileInfo dirInfo = findFileByPath(path, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return false;
    }

    if (!dirInfo.isDirectory) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    // List directory contents
    QList<QFATFileInfo> entries = listDirectory(static_cast<quint16>(dirInfo.cluster));

    if (recursive) {
        // Delete all contents recursively
        for (const QFATFileInfo &entry : entries) {
            // Skip . and .. entries
            if (entry.name == "." || entry.name == "..") {
                continue;
            }

            QString fullPath = path + "/" + entry.longName;

            if (entry.isDirectory) {
                if (!deleteDirectory(fullPath, true, error)) {
                    m_lastError = error;
                    return false;
                }
            } else {
                if (!deleteFile(fullPath, error)) {
                    m_lastError = error;
                    return false;
                }
            }
        }
    } else {
        // Check if directory is empty (only . and .. entries)
        for (const QFATFileInfo &entry : entries) {
            if (entry.name != "." && entry.name != "..") {
                error = QFATError::InvalidPath; // Directory not empty
                m_lastError = error;
                return false;
            }
        }
    }

    // Use deleteFile to remove the empty directory
    return deleteFile(path, error);
}

quint32 QFAT16FileSystem::getFreeSpace(QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return 0;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint32 fatOffset = reservedSectors * bytesPerSector;
    quint32 totalClusters = (sectorsPerFAT * bytesPerSector) / 2; // 2 bytes per FAT16 entry
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;
    quint32 freeClusters = 0;

    // Count free clusters
    for (quint16 cluster = 2; cluster < totalClusters && cluster < 0xFFF0; cluster++) {
        m_stream.device()->seek(fatOffset + cluster * 2);
        quint16 value;
        m_stream >> value;

        if (value == 0) {
            freeClusters++;
        }
    }

    return freeClusters * clusterSize;
}

quint32 QFAT16FileSystem::getTotalSpace(QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return 0;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint32 totalClusters = (sectorsPerFAT * bytesPerSector) / 2; // 2 bytes per FAT16 entry
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    // Total usable clusters (starting from cluster 2)
    quint32 usableClusters = totalClusters - 2;

    return usableClusters * clusterSize;
}

