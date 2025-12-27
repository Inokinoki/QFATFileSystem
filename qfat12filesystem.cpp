#include "qfatfilesystem.h"
#include "internal_constants.h"
#include <QDebug>
#include <QFile>

// ============================================================================
// QFAT12FileSystem
// ============================================================================

QFAT12FileSystem::QFAT12FileSystem(QSharedPointer<QIODevice> device)
    : QFATFileSystem(device)
{
}

QScopedPointer<QFAT12FileSystem> QFAT12FileSystem::create(const QString &imagePath)
{
    QSharedPointer<QFile> file(new QFile(imagePath));
    if (!file->open(QIODevice::ReadWrite)) {
        qWarning() << "Failed to open FAT12 image:" << imagePath;
        return QScopedPointer<QFAT12FileSystem>();
    }

    return QScopedPointer<QFAT12FileSystem>(new QFAT12FileSystem(file));
}

quint16 QFAT12FileSystem::readRootDirSector()
{
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    return reservedSectors + (numFATs * sectorsPerFAT);
}

quint32 QFAT12FileSystem::calculateRootDirOffset()
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 rootDirSector = readRootDirSector();

    return rootDirSector * bytesPerSector;
}

quint32 QFAT12FileSystem::calculateClusterOffset(quint16 cluster)
{
    if (cluster < 2) {
        return 0;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint16 reservedSectors = readReservedSectors();
    quint8 numFATs = readNumberOfFATs();
    quint16 rootEntryCount = readRootEntryCount();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint32 rootDirSectors = ((rootEntryCount * 32) + (bytesPerSector - 1)) / bytesPerSector;
    quint32 firstDataSector = reservedSectors + (numFATs * sectorsPerFAT) + rootDirSectors;
    quint32 clusterSector = firstDataSector + ((cluster - 2) * sectorsPerCluster);

    return clusterSector * bytesPerSector;
}

quint16 QFAT12FileSystem::readNextCluster(quint16 cluster)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();

    // FAT12 uses 12-bit entries, so we need special handling
    // Every 3 bytes contain 2 FAT entries
    quint32 fatOffset = reservedSectors * bytesPerSector;
    quint32 entryOffset = cluster + (cluster / 2); // cluster * 1.5
    quint32 absoluteOffset = fatOffset + entryOffset;

    m_stream.device()->seek(absoluteOffset);
    quint16 value;
    m_stream >> value;

    // Extract the 12-bit value
    if (cluster & 1) {
        // Odd cluster number - use high 12 bits
        value = value >> 4;
    } else {
        // Even cluster number - use low 12 bits
        value = value & 0x0FFF;
    }

    // Check for end of chain markers
    if (value >= 0x0FF8) {
        return 0xFFFF; // End of chain
    }

    // Check for bad cluster
    if (value == 0x0FF7) {
        return 0xFFFF;
    }

    return value;
}

QList<QFATFileInfo> QFAT12FileSystem::listRootDirectory()
{
    quint32 rootDirOffset = calculateRootDirOffset();
    quint16 rootEntryCount = readRootEntryCount();
    quint32 rootDirSize = rootEntryCount * 32;

    return readDirectoryEntries(rootDirOffset, rootDirSize);
}

QList<QFATFileInfo> QFAT12FileSystem::listDirectory(const QString &path)
{
    QFATError error;
    QFATFileInfo dirInfo = findFileByPath(path, error);

    if (error != QFATError::None || !dirInfo.isDirectory) {
        return QList<QFATFileInfo>();
    }

    return listDirectory(static_cast<quint16>(dirInfo.cluster));
}

QList<QFATFileInfo> QFAT12FileSystem::listDirectory(quint16 cluster)
{
    QList<QFATFileInfo> entries;

    if (cluster < 2) {
        return entries;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = bytesPerSector * sectorsPerCluster;

    QList<quint16> clusters = getClusterChain(cluster);

    for (quint16 c : clusters) {
        quint32 clusterOffset = calculateClusterOffset(c);
        QList<QFATFileInfo> clusterEntries = readDirectoryEntries(clusterOffset, clusterSize);
        entries.append(clusterEntries);
    }

    return entries;
}

QFATFileInfo QFAT12FileSystem::findFileByPath(const QString &path, QFATError &error)
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

    QList<QFATFileInfo> currentDir = listRootDirectory();

    for (int i = 0; i < parts.size(); i++) {
        QFATFileInfo found = findInDirectory(currentDir, parts[i]);

        if (found.name.isEmpty()) {
            error = (i < parts.size() - 1) ? QFATError::DirectoryNotFound : QFATError::FileNotFound;
            m_lastError = error;
            return QFATFileInfo();
        }

        if (i == parts.size() - 1) {
            return found;
        }

        if (!found.isDirectory) {
            error = QFATError::DirectoryNotFound;
            m_lastError = error;
            return QFATFileInfo();
        }

        currentDir = listDirectory(static_cast<quint16>(found.cluster));
    }

    error = QFATError::FileNotFound;
    m_lastError = error;
    return QFATFileInfo();
}

QList<quint16> QFAT12FileSystem::getClusterChain(quint16 startCluster)
{
    QList<quint16> chain;

    if (startCluster < 2) {
        return chain;
    }

    quint16 currentCluster = startCluster;
    const int maxClusters = 0x0FF0;

    while (currentCluster >= 2 && currentCluster < 0x0FF8 && chain.size() < maxClusters) {
        chain.append(currentCluster);
        currentCluster = readNextCluster(currentCluster);

        if (currentCluster == 0) {
            break;
        }
    }

    return chain;
}

QByteArray QFAT12FileSystem::readClusterChain(quint16 startCluster, quint32 fileSize)
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
        m_stream.readRawData(clusterData.data(), bytesToRead);
        data.append(clusterData);

        bytesRead += bytesToRead;
        if (bytesRead >= fileSize) {
            break;
        }
    }

    return data;
}

QByteArray QFAT12FileSystem::readFile(const QString &path, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return QByteArray();
    }

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

    return readClusterChain(static_cast<quint16>(fileInfo.cluster), fileInfo.size);
}

QByteArray QFAT12FileSystem::readFilePartial(const QString &path, quint32 offset, quint32 length, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return QByteArray();
    }

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

    if (offset >= fileInfo.size) {
        return QByteArray();
    }

    quint32 actualLength = qMin(length, fileInfo.size - offset);
    QByteArray fullData = readClusterChain(static_cast<quint16>(fileInfo.cluster), fileInfo.size);

    return fullData.mid(offset, actualLength);
}

bool QFAT12FileSystem::exists(const QString &path)
{
    // Special case for root directory
    if (path == "/" || path.isEmpty()) {
        return true;
    }

    QFATError error;
    QFATFileInfo info = findFileByPath(path, error);
    return error == QFATError::None && !info.name.isEmpty();
}

QFATFileInfo QFAT12FileSystem::getFileInfo(const QString &path, QFATError &error)
{
    return findFileByPath(path, error);
}

// FAT12 Write Operations
quint16 QFAT12FileSystem::findFreeCluster()
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT_OFFSET);
    quint16 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint32 totalClusters = (sectorsPerFAT * bytesPerSector * 2) / 3; // 1.5 bytes per FAT12 entry

    // Start from cluster 2 (first valid data cluster)
    for (quint16 cluster = 2; cluster < totalClusters && cluster < 0x0FF0; cluster++) {
        quint16 nextCluster = readNextCluster(cluster);
        if (nextCluster == 0) {
            return cluster;
        }
    }

    return 0; // No free clusters
}

bool QFAT12FileSystem::writeNextCluster(quint16 cluster, quint16 value)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();

    // FAT12 uses 12-bit entries, so we need special handling
    quint32 fatOffset = reservedSectors * bytesPerSector;
    quint32 entryOffset = cluster + (cluster / 2); // cluster * 1.5
    quint32 absoluteOffset = fatOffset + entryOffset;

    // Read current value (need 2 bytes to handle 12-bit entry)
    m_stream.device()->seek(absoluteOffset);
    quint16 current;
    m_stream >> current;

    // Modify the 12-bit value
    if (cluster & 1) {
        // Odd cluster number - modify high 12 bits
        current = (current & 0x000F) | ((value & 0x0FFF) << 4);
    } else {
        // Even cluster number - modify low 12 bits
        current = (current & 0xF000) | (value & 0x0FFF);
    }

    // Write back
    m_stream.device()->seek(absoluteOffset);
    m_stream << current;

    return m_stream.status() == QDataStream::Ok;
}

bool QFAT12FileSystem::writeClusterData(quint16 cluster, const QByteArray &data, quint32 offset)
{
    if (cluster < 2) {
        return false;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    if (offset + data.size() > clusterSize) {
        return false;
    }

    quint32 clusterOffset = calculateClusterOffset(cluster);
    m_stream.device()->seek(clusterOffset + offset);
    m_stream.writeRawData(data.constData(), data.size());

    return m_stream.status() == QDataStream::Ok;
}

QList<quint16> QFAT12FileSystem::allocateClusterChain(quint32 numClusters)
{
    QList<quint16> chain;

    for (quint32 i = 0; i < numClusters; i++) {
        quint16 cluster = findFreeCluster();
        if (cluster == 0) {
            // Out of space - free what we allocated
            freeClusterChain(chain.isEmpty() ? 0 : chain.first());
            return QList<quint16>();
        }

        if (!chain.isEmpty()) {
            writeNextCluster(chain.last(), cluster);
        }

        writeNextCluster(cluster, 0x0FFF); // Mark as end of chain
        chain.append(cluster);
    }

    return chain;
}

bool QFAT12FileSystem::freeClusterChain(quint16 startCluster)
{
    if (startCluster < 2) {
        return true;
    }

    QList<quint16> chain = getClusterChain(startCluster);

    for (quint16 cluster : chain) {
        if (!writeNextCluster(cluster, 0)) {
            return false;
        }
    }

    return true;
}

bool QFAT12FileSystem::writeFile(const QString &path, const QByteArray &data, QFATError &error)
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

    // Store in mapping for future lookups
    m_longToShortNameMap[fileName.toLower()] = fileInfo.name;

    return true;
}

bool QFAT12FileSystem::deleteFile(const QString &path, QFATError &error)
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

    if (fileInfo.isDirectory) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    // Free cluster chain
    if (fileInfo.cluster >= 2) {
        if (!freeClusterChain(static_cast<quint16>(fileInfo.cluster))) {
            error = QFATError::WriteError;
            m_lastError = error;
            return false;
        }
    }

    // Mark directory entry as deleted
    if (!deleteDirectoryEntry(path)) {
        error = QFATError::WriteError;
        m_lastError = error;
        return false;
    }

    return true;
}

bool QFAT12FileSystem::createDirectory(const QString &path, QFATError &error)
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
    dirInfo.size = 0;
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

    // Store in mapping for future lookups
    m_longToShortNameMap[dirName.toLower()] = dirInfo.name;

    return true;
}


bool QFAT12FileSystem::deleteDirectory(const QString &path, bool recursive, QFATError &error)
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

    // Check if directory is empty (unless recursive)
    if (!recursive && !isDirectoryEmpty(static_cast<quint16>(dirInfo.cluster))) {
        error = QFATError::WriteError; // Not empty
        m_lastError = error;
        return false;
    }

    // If recursive, delete all contents first
    if (recursive) {
        QList<QFATFileInfo> entries = listDirectory(static_cast<quint16>(dirInfo.cluster));
        for (const QFATFileInfo &entry : entries) {
            QString entryPath = path + "/" + entry.name;
            if (entry.isDirectory) {
                if (!deleteDirectory(entryPath, true, error)) {
                    m_lastError = error;
                    return false;
                }
            } else {
                if (!deleteFile(entryPath, error)) {
                    m_lastError = error;
                    return false;
                }
            }
        }
    }

    // Free cluster chain
    if (dirInfo.cluster >= 2) {
        if (!freeClusterChain(static_cast<quint16>(dirInfo.cluster))) {
            error = QFATError::WriteError;
            m_lastError = error;
            return false;
        }
    }

    // Mark directory entry as deleted
    if (!deleteDirectoryEntry(path)) {
        error = QFATError::WriteError;
        m_lastError = error;
        return false;
    }

    return true;
}


bool QFAT12FileSystem::renameFile(const QString &oldPath, const QString &newPath, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Find the file
    QFATFileInfo fileInfo = findFileByPath(oldPath, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return false;
    }

    // Check if new name already exists
    QFATError checkError;
    findFileByPath(newPath, checkError);
    if (checkError == QFATError::None) {
        error = QFATError::InvalidFileName; // Already exists
        m_lastError = error;
        return false;
    }

    // Parse new path
    QStringList newParts = splitPath(newPath);
    if (newParts.isEmpty()) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    QString newName = newParts.last();

    // Modify the directory entry name
    QString newShortName = modifyDirectoryEntryName(oldPath, newName);
    if (newShortName.isEmpty()) {
        error = QFATError::WriteError;
        m_lastError = error;
        return false;
    }

    // Update mapping
    m_longToShortNameMap.remove(fileInfo.longName.toLower());
    m_longToShortNameMap[newName.toLower()] = newShortName;

    return true;
}


bool QFAT12FileSystem::moveFile(const QString &sourcePath, const QString &destPath, QFATError &error)
{
    error = QFATError::None;

    if (!m_device->isOpen()) {
        error = QFATError::DeviceNotOpen;
        m_lastError = error;
        return false;
    }

    // Check that source exists
    QFATFileInfo sourceInfo = findFileByPath(sourcePath, error);
    if (error != QFATError::None) {
        m_lastError = error;
        return false;
    }

    // Check that destination doesn't exist
    QFATError destCheckError;
    findFileByPath(destPath, destCheckError);
    if (destCheckError == QFATError::None) {
        error = QFATError::InvalidFileName; // Destination already exists
        m_lastError = error;
        return false;
    }

    // Parse destination path to verify parent exists
    QStringList destParts = splitPath(destPath);
    if (destParts.isEmpty()) {
        error = QFATError::InvalidPath;
        m_lastError = error;
        return false;
    }

    QString destFileName = destParts.last();
    QString destParentPath;

    if (destParts.size() > 1) {
        destParts.removeLast();
        destParentPath = "/" + destParts.join("/");

        // Verify destination parent exists
        QFATError parentError;
        QFATFileInfo parentInfo = findFileByPath(destParentPath, parentError);
        if (parentError != QFATError::None || !parentInfo.isDirectory) {
            error = QFATError::DirectoryNotFound;
            m_lastError = error;
            return false;
        }
    } else {
        destParentPath = "/";
    }

    // Read source file data
    QByteArray data;
    if (!sourceInfo.isDirectory) {
        data = readFile(sourcePath, error);
        if (error != QFATError::None) {
            m_lastError = error;
            return false;
        }
    }

    // Delete source
    if (sourceInfo.isDirectory) {
        if (!deleteDirectory(sourcePath, true, error)) {
            m_lastError = error;
            return false;
        }
    } else {
        if (!deleteFile(sourcePath, error)) {
            m_lastError = error;
            return false;
        }
    }

    // Create at destination
    if (sourceInfo.isDirectory) {
        if (!createDirectory(destPath, error)) {
            m_lastError = error;
            return false;
        }
    } else {
        if (!writeFile(destPath, data, error)) {
            m_lastError = error;
            return false;
        }
    }

    return true;
}

// Helper methods

quint32 QFAT12FileSystem::getFreeSpace(QFATError &error)
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

    quint32 totalClusters = (sectorsPerFAT * bytesPerSector * 2) / 3; // 1.5 bytes per FAT12 entry
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;
    quint32 freeClusters = 0;

    // Count free clusters
    for (quint16 cluster = 2; cluster < totalClusters && cluster < 0x0FF0; cluster++) {
        quint16 value = readNextCluster(cluster);
        if (value == 0) {
            freeClusters++;
        }
    }

    return freeClusters * clusterSize;
}

quint32 QFAT12FileSystem::getTotalSpace(QFATError &error)
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

    quint32 totalClusters = (sectorsPerFAT * bytesPerSector * 2) / 3; // 1.5 bytes per FAT12 entry
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    // Usable clusters start from 2
    quint32 usableClusters = (totalClusters > 2) ? (totalClusters - 2) : 0;
    if (usableClusters > 0x0FF0 - 2) {
        usableClusters = 0x0FF0 - 2;
    }

    return usableClusters * clusterSize;
}

// Helper methods
bool QFAT12FileSystem::updateDirectoryEntry(const QString &parentPath, const QFATFileInfo &fileInfo)
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
    bool needsLFN = (fileInfo.name.contains('~') &&
                     !fileInfo.longName.isEmpty() &&
                     fileInfo.longName.toUpper() != fileInfo.name.toUpper());
    int lfnEntriesNeeded = needsLFN ? calculateLFNEntriesNeeded(fileInfo.longName) : 0;
    int totalEntriesNeeded = lfnEntriesNeeded + 1;

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

            if (consecutiveFreeSlots >= totalEntriesNeeded) {
                foundFree = true;
            }

            if (firstByte == ENTRY_END_OF_DIRECTORY) {
                break;
            }

            entryOffset += ENTRY_SIZE;
            continue;
        }

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
        return createDirectoryEntry(entryOffset, fileInfo);
    } else if (foundFree) {
        // Write LFN entries if needed, followed by short name entry
        if (needsLFN) {
            quint8 checksum = calculateLFNChecksum(fileInfo.name);
            quint32 currentOffset = freeSlotOffset;

            // Write LFN entries in reverse order
            for (int seq = lfnEntriesNeeded; seq >= 1; seq--) {
                quint8 lfnEntry[ENTRY_SIZE];
                writeLFNEntry(lfnEntry, fileInfo.longName, seq, checksum, seq == lfnEntriesNeeded);

                m_stream.device()->seek(currentOffset);
                m_stream.writeRawData(reinterpret_cast<char*>(lfnEntry), ENTRY_SIZE);

                currentOffset += ENTRY_SIZE;
            }

            return createDirectoryEntry(currentOffset, fileInfo);
        } else {
            // Writing without LFN - store mapping if names differ
            if (!fileInfo.longName.isEmpty() && fileInfo.longName.toLower() != fileInfo.name.toLower()) {
                m_longToShortNameMap[fileInfo.longName.toLower()] = fileInfo.name;
            }
            return createDirectoryEntry(freeSlotOffset, fileInfo);
        }
    }

    // Fallback: try to find a single free slot
    if (needsLFN && !foundFree) {
        entryOffset = dirOffset;
        for (quint32 i = 0; i < maxEntries; i++) {
            m_stream.device()->seek(entryOffset);
            quint8 entry[ENTRY_SIZE];
            m_stream.readRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

            quint8 firstByte = entry[ENTRY_NAME_OFFSET];
            if (firstByte == ENTRY_END_OF_DIRECTORY || firstByte == ENTRY_DELETED) {
                m_longToShortNameMap[fileInfo.longName.toLower()] = fileInfo.name;
                return createDirectoryEntry(entryOffset, fileInfo);
            }
            entryOffset += ENTRY_SIZE;
        }
    }

    return false;
}


bool QFAT12FileSystem::createDirectoryEntry(quint32 dirOffset, const QFATFileInfo &fileInfo)
{
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


bool QFAT12FileSystem::deleteDirectoryEntry(const QString &path)
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

        quint16 bytesPerSector = readBytesPerSector();
        quint8 sectorsPerCluster = readSectorsPerCluster();
        quint32 clusterSize = bytesPerSector * sectorsPerCluster;
        maxEntries = clusterSize / ENTRY_SIZE;
    }

    // Find and delete the entry
    quint32 entryOffset = dirOffset;

    for (quint32 i = 0; i < maxEntries; i++) {
        m_stream.device()->seek(entryOffset);
        quint8 entry[ENTRY_SIZE];
        m_stream.readRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

        quint8 firstByte = entry[ENTRY_NAME_OFFSET];
        if (firstByte == ENTRY_END_OF_DIRECTORY) {
            break;
        }

        // Skip LFN entries
        if (isLongFileNameEntry(entry)) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        if (firstByte == ENTRY_DELETED) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Parse entry name
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

        // Check if this is the entry to delete
        QString shortName = m_longToShortNameMap.value(fileName.toLower(), fileName);
        if (name8_3.toUpper() == shortName.toUpper() || name8_3.toUpper() == fileName.toUpper()) {
            // Mark as deleted
            m_stream.device()->seek(entryOffset);
            quint8 deleted = ENTRY_DELETED;
            m_stream.writeRawData(reinterpret_cast<char*>(&deleted), 1);
            return true;
        }

        entryOffset += ENTRY_SIZE;
    }

    return false;
}


QString QFAT12FileSystem::modifyDirectoryEntryName(const QString &path, const QString &newName)
{
    if (!m_device->isOpen()) {
        return QString();
    }

    // Parse path
    QStringList parts = splitPath(path);
    if (parts.isEmpty()) {
        return QString();
    }

    QString oldName = parts.last();
    QString parentPath;

    if (parts.size() > 1) {
        parts.removeLast();
        parentPath = "/" + parts.join("/");
    } else {
        parentPath = "/";
    }

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
            return QString();
        }
        dirOffset = calculateClusterOffset(static_cast<quint16>(dirInfo.cluster));

        quint16 bytesPerSector = readBytesPerSector();
        quint8 sectorsPerCluster = readSectorsPerCluster();
        quint32 clusterSize = bytesPerSector * sectorsPerCluster;
        maxEntries = clusterSize / ENTRY_SIZE;
    }

    // Get parent entries for short name generation
    QList<QFATFileInfo> parentEntries;
    if (parentPath == "/") {
        parentEntries = listRootDirectory();
    } else {
        QFATError error;
        QFATFileInfo parentInfo = findFileByPath(parentPath, error);
        if (error == QFATError::None && parentInfo.isDirectory) {
            parentEntries = listDirectory(static_cast<quint16>(parentInfo.cluster));
        }
    }

    // Generate new short name
    QString newShortName = generateShortName(newName, parentEntries);

    // Find and update the entry
    quint32 entryOffset = dirOffset;

    for (quint32 i = 0; i < maxEntries; i++) {
        m_stream.device()->seek(entryOffset);
        quint8 entry[ENTRY_SIZE];
        m_stream.readRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

        quint8 firstByte = entry[ENTRY_NAME_OFFSET];
        if (firstByte == ENTRY_END_OF_DIRECTORY) {
            break;
        }

        if (isLongFileNameEntry(entry) || firstByte == ENTRY_DELETED) {
            entryOffset += ENTRY_SIZE;
            continue;
        }

        // Parse entry name
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

        // Check if this is the entry to rename
        QString shortName = m_longToShortNameMap.value(oldName.toLower(), oldName);
        if (name8_3.toUpper() == shortName.toUpper() || name8_3.toUpper() == oldName.toUpper()) {
            // Update with new name
            QString baseName, extension;
            int dotPos = newShortName.indexOf('.');
            if (dotPos >= 0) {
                baseName = newShortName.left(dotPos).toUpper();
                extension = newShortName.mid(dotPos + 1).toUpper();
            } else {
                baseName = newShortName.toUpper();
            }

            baseName = baseName.leftJustified(8, ' ');
            extension = extension.leftJustified(3, ' ');

            for (int j = 0; j < 8; j++) {
                entry[j] = (j < baseName.length()) ? baseName[j].toLatin1() : ' ';
            }
            for (int j = 0; j < 3; j++) {
                entry[8 + j] = (j < extension.length()) ? extension[j].toLatin1() : ' ';
            }

            // Write back
            m_stream.device()->seek(entryOffset);
            m_stream.writeRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

            return newShortName;
        }

        entryOffset += ENTRY_SIZE;
    }

    return QString();
}


bool QFAT12FileSystem::isDirectoryEmpty(quint16 cluster)
{
    if (cluster < 2) {
        return true;
    }

    QList<QFATFileInfo> entries = listDirectory(cluster);

    // An empty directory should have no entries (. and .. are filtered out by listDirectory)
    return entries.isEmpty();
}

