#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QString>

#include "internal_constants.h"
#include "qfatfilesystem.h"

// ============================================================================
// Base class: QFATFileSystem
// ============================================================================

QFATFileSystem::QFATFileSystem(QSharedPointer<QIODevice> device)
    : m_device(device)
    , m_lastError(QFATError::None)
{
    m_stream.setDevice(m_device.data());
    // Set byte order to Little Endian so that the data is read correctly
    m_stream.setByteOrder(QDataStream::LittleEndian);
}

QFATFileSystem::~QFATFileSystem()
{
}

QString QFATFileSystem::errorString() const
{
    switch (m_lastError) {
    case QFATError::None:
        return "No error";
    case QFATError::DeviceNotOpen:
        return "Device not open";
    case QFATError::InvalidPath:
        return "Invalid path";
    case QFATError::FileNotFound:
        return "File not found";
    case QFATError::DirectoryNotFound:
        return "Directory not found";
    case QFATError::InvalidCluster:
        return "Invalid cluster";
    case QFATError::ReadError:
        return "Read error";
    case QFATError::WriteError:
        return "Write error";
    case QFATError::NotImplemented:
        return "Not implemented";
    case QFATError::InsufficientSpace:
        return "Insufficient space";
    case QFATError::InvalidFileName:
        return "Invalid file name";
    default:
        return "Unknown error";
    }
}

QStringList QFATFileSystem::splitPath(const QString &path)
{
    QString normalized = path;
    // Normalize path separators
    normalized.replace('\\', '/');

    // Remove leading and trailing slashes
    while (normalized.startsWith('/')) {
        normalized = normalized.mid(1);
    }
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }

    if (normalized.isEmpty()) {
        return QStringList();
    }

    return normalized.split('/', Qt::SkipEmptyParts);
}

QFATFileInfo QFATFileSystem::findInDirectory(const QList<QFATFileInfo> &entries, const QString &name)
{
    QString upperName = name.toUpper();

    for (const QFATFileInfo &entry : entries) {
        QString entryShortName = entry.name.toUpper();
        QString entryLongName = entry.longName.toUpper();

        // Match by exact name (short or long)
        if (entryShortName == upperName || entryLongName == upperName) {
            qDebug() << "[findInDirectory] Matched" << name << "to short:" << entry.name << "long:" << entry.longName;
            return entry;
        }
    }

    // Helper lambda to detect if a long name looks like garbage
    auto isGarbageLFN = [](const QString &longName, const QString &shortName) -> bool {
        // If long and short names are identical, it's not really an LFN
        if (longName.toUpper() == shortName.toUpper()) {
            return false;
        }
        // Check if longName contains mostly non-ASCII or control characters (likely garbage)
        int nonAsciiCount = 0;
        for (const QChar &ch : longName) {
            if (ch.unicode() > 127 || ch.unicode() < 32) {
                nonAsciiCount++;
            }
        }
        // If more than 50% of characters are non-ASCII, likely garbage
        return longName.length() > 0 && (nonAsciiCount * 2 > longName.length());
    };

    // If no direct match found, try to find entries that were written without LFN entries.
    // For such entries, longName == name (both are the short name).
    // We need to check if any of these could match the search name.

    // Generate what the base short name would be (without considering duplicates)
    QString searchUpper = name.toUpper();
    QString searchBase = searchUpper;
    QString searchExt;

    int dotPos = searchBase.lastIndexOf('.');
    if (dotPos > 0) {
        searchExt = searchBase.mid(dotPos + 1);
        searchBase = searchBase.left(dotPos);
    }

    // Remove invalid chars and truncate to generate the base
    QString originalBase = searchBase;
    searchBase.remove(QRegularExpression("[^A-Z0-9_^$~!#%&\\-{}@'`()]"));
    searchExt.remove(QRegularExpression("[^A-Z0-9_^$~!#%&\\-{}@'`()]"));

    // Check if the name would need truncation
    bool needsTruncation = (searchBase.length() > 8 || searchExt.length() > 3 ||
                           searchBase != originalBase); // Invalid chars were removed

    if (searchBase.length() > 6) {
        searchBase = searchBase.left(6);
    }
    if (searchExt.length() > 3) {
        searchExt = searchExt.left(3);
    }


    for (const QFATFileInfo &entry : entries) {
        QString entryShortName = entry.name.toUpper();

        // Only match entries where longName == name (no LFN written) OR where longName is garbage
        // Skip entries with valid LFNs
        bool hasValidLFN = (entry.longName.toUpper() != entry.name.toUpper()) &&
                           !isGarbageLFN(entry.longName, entry.name);

        if (hasValidLFN) {
            // This entry has a valid LFN, skip it for fallback matching
            continue;
        }

        // Entry either has no LFN or has garbage LFN - treat it as short-name-only
        if (entry.longName.toUpper() != entry.name.toUpper()) {
        }

        // Parse the entry's short name
        QString entryBase = entryShortName;
        QString entryExt;
        int entryDot = entryBase.lastIndexOf('.');
        if (entryDot > 0) {
            entryExt = entryBase.mid(entryDot + 1);
            entryBase = entryBase.left(entryDot);
        }


        // Check if extensions match
        if (entryExt != searchExt) {
            continue;
        }

        // Match if the base names are identical
        // This works for entries without tails (like "EMPTY_.TXT")
        // Entries with tails (like "TESTFI~1.TXT") cannot be reliably matched
        // without LFN entries, as we can't distinguish between "testfile0.txt" and "testfile1.txt"
        //
        // IMPORTANT: Only match if the search name did NOT need truncation.
        // If "testfile0.txt" truncates to "TESTFI", we should NOT match an existing "TESTFI.TXT"
        // because they are different files.
        if (entryBase == searchBase && !entryBase.contains("~") && !needsTruncation) {
            return entry;
        }
    }

    return QFATFileInfo();
}

void QFATFileSystem::encodeFATDateTime(const QDateTime &dt, quint16 &date, quint16 &time)
{
    // FAT date format: bits 0-4: day (1-31), bits 5-8: month (1-12), bits 9-15: year-1980
    // FAT time format: bits 0-4: seconds/2 (0-29), bits 5-10: minute (0-59), bits 11-15: hour (0-23)
    if (!dt.isValid()) {
        date = 0;
        time = 0;
        return;
    }

    QDate d = dt.date();
    QTime t = dt.time();

    int year = d.year() - ENTRY_DATE_TIME_START_OF_YEAR;
    if (year < 0) year = 0;
    if (year > 127) year = 127;

    date = (d.day() & MASK_5_BITS) | ((d.month() & MASK_4_BITS) << 5) | ((year & MASK_7_BITS) << 9);
    time = ((t.second() / 2) & MASK_5_BITS) | ((t.minute() & MASK_6_BITS) << 5) | ((t.hour() & MASK_5_BITS) << 11);
}

QString QFATFileSystem::generateShortName(const QString &longName, const QList<QFATFileInfo> &existingEntries)
{
    // Convert to uppercase and remove invalid characters
    QString baseName = longName.toUpper();
    QString ext;

    // Split extension
    int dotPos = baseName.lastIndexOf('.');
    if (dotPos > 0) {
        ext = baseName.mid(dotPos + 1);
        baseName = baseName.left(dotPos);
    }

    // Remove invalid characters and spaces
    baseName.remove(QRegularExpression("[^A-Z0-9_^$~!#%&\\-{}@'`()]"));
    ext.remove(QRegularExpression("[^A-Z0-9_^$~!#%&\\-{}@'`()]"));

    // Truncate to 8.3 format
    if (baseName.length() > 8) {
        baseName = baseName.left(6);
    }
    if (ext.length() > 3) {
        ext = ext.left(3);
    }

    // Generate unique name if needed
    QString shortName = baseName;
    if (!ext.isEmpty()) {
        shortName += "." + ext;
    }

    // Check for duplicates and add numeric tail if necessary
    int tailNum = 1;
    QString testName = shortName;
    bool duplicate = true;

    while (duplicate && tailNum < 1000) {
        duplicate = false;
        for (const QFATFileInfo &entry : existingEntries) {
            if (entry.name.toUpper() == testName.toUpper()) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            QString tail = QString("~%1").arg(tailNum);
            QString truncatedBase = baseName.left(8 - tail.length());
            testName = truncatedBase + tail;
            if (!ext.isEmpty()) {
                testName += "." + ext;
            }
            tailNum++;
        }
    }

    return testName;
}

// Calculate LFN checksum for a short name (in 8.3 format with padding)
quint8 QFATFileSystem::calculateLFNChecksum(const QString &shortName)
{
    // Convert short name to 11-byte format (8 chars + 3 extension, space-padded)
    quint8 name[11];
    for (int i = 0; i < 11; i++) {
        name[i] = ' ';
    }

    QString upper = shortName.toUpper();
    int dotPos = upper.indexOf('.');

    // Copy base name (up to 8 chars)
    QString base = (dotPos >= 0) ? upper.left(dotPos) : upper;
    for (int i = 0; i < qMin(8, base.length()); i++) {
        name[i] = base[i].toLatin1();
    }

    // Copy extension (up to 3 chars)
    if (dotPos >= 0) {
        QString ext = upper.mid(dotPos + 1);
        for (int i = 0; i < qMin(3, ext.length()); i++) {
            name[8 + i] = ext[i].toLatin1();
        }
    }

    // Calculate checksum
    quint8 checksum = 0;
    for (int i = 0; i < 11; i++) {
        checksum = ((checksum & 1) << 7) + (checksum >> 1) + name[i];
    }

    return checksum;
}

// Calculate how many LFN entries are needed for a long name
int QFATFileSystem::calculateLFNEntriesNeeded(const QString &longName)
{
    // Each LFN entry holds 13 characters
    return (longName.length() + 12) / 13;
}

// Write a single LFN entry
void QFATFileSystem::writeLFNEntry(quint8 *entry, const QString &longName, int sequence, quint8 checksum, bool isLast)
{
    // Clear entry
    for (int i = 0; i < ENTRY_SIZE; i++) {
        entry[i] = 0;
    }

    // Set sequence number (1-based, with 0x40 flag for last entry)
    entry[0] = sequence;
    if (isLast) {
        entry[0] |= ENTRY_LFN_SEQUENCE_LAST_MASK;
    }

    // Set LFN attribute
    entry[ENTRY_ATTRIBUTE_OFFSET] = ENTRY_ATTRIBUTE_LONG_FILE_NAME;

    // Set checksum
    entry[0x0D] = checksum;

    // Calculate starting position in long name (0-based, counting from end)
    int startPos = (sequence - 1) * 13;

    // Write characters in UTF-16LE format
    // Part 1: 5 characters at offset 0x01
    for (int i = 0; i < 5; i++) {
        int charIndex = startPos + i;
        quint16 ch = (charIndex < longName.length()) ? longName[charIndex].unicode() : 0xFFFF;
        entry[ENTRY_LFN_PART1_OFFSET + i * 2] = ch & 0xFF;
        entry[ENTRY_LFN_PART1_OFFSET + i * 2 + 1] = (ch >> 8) & 0xFF;
    }

    // Part 2: 6 characters at offset 0x0E
    for (int i = 0; i < 6; i++) {
        int charIndex = startPos + 5 + i;
        quint16 ch = (charIndex < longName.length()) ? longName[charIndex].unicode() : 0xFFFF;
        entry[ENTRY_LFN_PART2_OFFSET + i * 2] = ch & 0xFF;
        entry[ENTRY_LFN_PART2_OFFSET + i * 2 + 1] = (ch >> 8) & 0xFF;
    }

    // Part 3: 2 characters at offset 0x1C
    for (int i = 0; i < 2; i++) {
        int charIndex = startPos + 11 + i;
        quint16 ch = (charIndex < longName.length()) ? longName[charIndex].unicode() : 0xFFFF;
        entry[ENTRY_LFN_PART3_OFFSET + i * 2] = ch & 0xFF;
        entry[ENTRY_LFN_PART3_OFFSET + i * 2 + 1] = (ch >> 8) & 0xFF;
    }
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
        chars[pos++] = (entry[ENTRY_LFN_PART1_OFFSET + i * 2] | (entry[ENTRY_LFN_PART1_OFFSET + i * 2 + 1] << 8));
    }
    // Part 2: 6 characters, 12 bytes
    for (int i = 0; i < ENTRY_LFN_PART2_LENGTH / 2; i++) {
        chars[pos++] = (entry[ENTRY_LFN_PART2_OFFSET + i * 2] | (entry[ENTRY_LFN_PART2_OFFSET + i * 2 + 1] << 8));
    }
    // Part 3: 2 characters, 4 bytes
    for (int i = 0; i < ENTRY_LFN_PART3_LENGTH / 2; i++) {
        chars[pos++] = (entry[ENTRY_LFN_PART3_OFFSET + i * 2] | (entry[ENTRY_LFN_PART3_OFFSET + i * 2 + 1] << 8));
    }

    // Convert to QString, stopping at null terminator or 0xFFFF
    for (int i = 0; i < ENTRY_LFN_CHARS; i++) {
        if (chars[i] == QChar::Null || chars[i] == 0xFFFF)
            break;
        name.append(QChar(chars[i]));
    }

    return name;
}

QFATFileInfo QFATFileSystem::parseDirectoryEntry(quint8 *entry, QString &longName)
{
    QFATFileInfo info;

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

QList<QFATFileInfo> QFATFileSystem::readDirectoryEntries(quint32 offset, quint32 maxSize)
{
    QList<QFATFileInfo> files;

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
            QFATFileInfo info = parseDirectoryEntry(entry, currentLongName);
            files.append(info);
            currentLongName.clear();
        }
    }

    return files;
}

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
    if (!m_device->isOpen() || m_device->atEnd()) {
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

// ============================================================================
// QFAT32FileSystem
// ============================================================================

QFAT32FileSystem::QFAT32FileSystem(QSharedPointer<QIODevice> device)
    : QFATFileSystem(device)
{
}

QScopedPointer<QFAT32FileSystem> QFAT32FileSystem::create(const QString &imagePath)
{
    QSharedPointer<QFile> file(new QFile(imagePath));
    if (!file->open(QIODevice::ReadWrite)) {
        qWarning() << "Failed to open FAT32 image:" << imagePath;
        return QScopedPointer<QFAT32FileSystem>();
    }

    return QScopedPointer<QFAT32FileSystem>(new QFAT32FileSystem(file));
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

    m_stream.device()->seek(BPB_SECTORS_PER_FAT32_OFFSET);
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

QList<QFATFileInfo> QFAT32FileSystem::listRootDirectory()
{
    if (!m_device->isOpen() || m_device->atEnd()) {
        qWarning() << "Device not open";
        return QList<QFATFileInfo>();
    }

    quint32 rootDirCluster = readRootDirCluster();
    return listDirectory(rootDirCluster);
}

QList<QFATFileInfo> QFAT32FileSystem::listDirectory(quint32 cluster)
{
    QList<QFATFileInfo> files;

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

QList<QFATFileInfo> QFAT32FileSystem::listDirectory(const QString &path)
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

    return listDirectory(dirInfo.cluster);
}

QFATFileInfo QFAT32FileSystem::findFileByPath(const QString &path, QFATError &error)
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
        currentDir = listDirectory(found.cluster);
    }

    error = QFATError::FileNotFound;
    m_lastError = error;
    return QFATFileInfo();
}

QList<quint32> QFAT32FileSystem::getClusterChain(quint32 startCluster)
{
    QList<quint32> chain;

    if (startCluster < 2) {
        return chain;
    }

    quint32 currentCluster = startCluster;
    const int maxClusters = 0x0FFFFFF0; // Safety limit

    while (currentCluster >= 2 && currentCluster < 0x0FFFFFF8 && chain.size() < maxClusters) {
        chain.append(currentCluster);
        currentCluster = readNextCluster(currentCluster);

        if (currentCluster == 0) {
            break;
        }
    }

    return chain;
}

QByteArray QFAT32FileSystem::readClusterChain(quint32 startCluster, quint32 fileSize)
{
    QByteArray data;

    if (startCluster < 2 || fileSize == 0) {
        return data;
    }

    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = sectorsPerCluster * bytesPerSector;

    QList<quint32> clusters = getClusterChain(startCluster);
    quint32 bytesRead = 0;

    for (quint32 cluster : clusters) {
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

QByteArray QFAT32FileSystem::readFile(const QString &path, QFATError &error)
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

    return readClusterChain(fileInfo.cluster, fileInfo.size);
}

quint32 QFAT32FileSystem::findFreeCluster()
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();

    m_stream.device()->seek(BPB_SECTORS_PER_FAT32_OFFSET);
    quint32 sectorsPerFAT;
    m_stream >> sectorsPerFAT;

    quint32 fatOffset = reservedSectors * bytesPerSector;
    quint32 totalClusters = (sectorsPerFAT * bytesPerSector) / 4; // 4 bytes per FAT32 entry

    // Start from cluster 2 (first valid data cluster)
    for (quint32 cluster = 2; cluster < totalClusters && cluster < 0x0FFFFFF0; cluster++) {
        m_stream.device()->seek(fatOffset + cluster * 4);
        quint32 value;
        m_stream >> value;
        value &= 0x0FFFFFFF; // Mask high 4 bits

        if (value == 0) {
            return cluster;
        }
    }

    return 0; // No free cluster found
}

bool QFAT32FileSystem::writeNextCluster(quint32 cluster, quint32 value)
{
    quint16 bytesPerSector = readBytesPerSector();
    quint16 reservedSectors = readReservedSectors();
    quint32 fatOffset = reservedSectors * bytesPerSector + cluster * 4;

    quint8 numFATs = readNumberOfFATs();

    // Mask to preserve high 4 bits
    value &= 0x0FFFFFFF;

    // Write to all FAT copies
    for (quint8 i = 0; i < numFATs; i++) {
        m_stream.device()->seek(BPB_SECTORS_PER_FAT32_OFFSET);
        quint32 sectorsPerFAT;
        m_stream >> sectorsPerFAT;

        quint32 fatCopyOffset = fatOffset + (i * sectorsPerFAT * bytesPerSector);
        m_stream.device()->seek(fatCopyOffset);
        m_stream << value;
    }

    return m_stream.status() == QDataStream::Ok;
}

bool QFAT32FileSystem::writeClusterData(quint32 cluster, const QByteArray &data, quint32 offset)
{
    quint32 clusterOffset = calculateClusterOffset(cluster);
    m_stream.device()->seek(clusterOffset + offset);

    qint64 written = m_stream.writeRawData(data.constData(), data.size());
    return written == data.size();
}

QList<quint32> QFAT32FileSystem::allocateClusterChain(quint32 numClusters)
{
    QList<quint32> chain;

    if (numClusters == 0) {
        return chain;
    }

    // Find and allocate clusters
    for (quint32 i = 0; i < numClusters; i++) {
        quint32 freeCluster = findFreeCluster();
        if (freeCluster == 0) {
            // No more free clusters, free what we allocated and return empty
            if (!chain.isEmpty()) {
                freeClusterChain(chain.first());
            }
            return QList<quint32>();
        }

        chain.append(freeCluster);

        // Mark cluster as used (write 0x0FFFFFFF for end of chain)
        if (i == numClusters - 1) {
            writeNextCluster(freeCluster, 0x0FFFFFFF); // End of chain
        } else {
            // We'll update this once we know the next cluster
            writeNextCluster(freeCluster, 0x0FFFFFFF);
        }
    }

    // Link the chain
    for (int i = 0; i < chain.size() - 1; i++) {
        writeNextCluster(chain[i], chain[i + 1]);
    }

    return chain;
}

bool QFAT32FileSystem::freeClusterChain(quint32 startCluster)
{
    QList<quint32> chain = getClusterChain(startCluster);

    for (quint32 cluster : chain) {
        if (!writeNextCluster(cluster, 0)) {
            return false;
        }
    }

    return true;
}

bool QFAT32FileSystem::createDirectoryEntry(quint32 dirOffset, const QFATFileInfo &fileInfo)
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

    // Cluster (FAT32 uses both low and high words)
    quint16 clusterLow = static_cast<quint16>(fileInfo.cluster & 0xFFFF);
    quint16 clusterHigh = static_cast<quint16>((fileInfo.cluster >> 16) & 0xFFFF);

    entry[ENTRY_CLUSTER_OFFSET] = clusterLow & 0xFF;
    entry[ENTRY_CLUSTER_OFFSET + 1] = (clusterLow >> 8) & 0xFF;
    entry[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET] = clusterHigh & 0xFF;
    entry[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET + 1] = (clusterHigh >> 8) & 0xFF;

    // Size
    entry[ENTRY_SIZE_OFFSET] = fileInfo.size & 0xFF;
    entry[ENTRY_SIZE_OFFSET + 1] = (fileInfo.size >> 8) & 0xFF;
    entry[ENTRY_SIZE_OFFSET + 2] = (fileInfo.size >> 16) & 0xFF;
    entry[ENTRY_SIZE_OFFSET + 3] = (fileInfo.size >> 24) & 0xFF;

    // Clear any potential garbage LFN entries immediately before this entry
    // This prevents old LFN data from being associated with our new short-name entry
    if (dirOffset >= ENTRY_SIZE) {
        m_stream.device()->seek(dirOffset - ENTRY_SIZE);
        quint8 prevEntry[ENTRY_SIZE];
        m_stream.readRawData(reinterpret_cast<char*>(prevEntry), ENTRY_SIZE);

        // If previous entry looks like an LFN entry (attribute = 0x0F), mark it as deleted
        if (prevEntry[ENTRY_ATTRIBUTE_OFFSET] == ENTRY_ATTRIBUTE_LONG_FILE_NAME) {
            prevEntry[ENTRY_NAME_OFFSET] = ENTRY_DELETED;
            m_stream.device()->seek(dirOffset - ENTRY_SIZE);
            m_stream.writeRawData(reinterpret_cast<char*>(prevEntry), ENTRY_SIZE);
        }
    }

    // Write entry to directory
    m_stream.device()->seek(dirOffset);
    qint64 written = m_stream.writeRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

    return written == ENTRY_SIZE;
}

bool QFAT32FileSystem::updateDirectoryEntry(const QString &parentPath, const QFATFileInfo &fileInfo)
{
    // Find the directory cluster
    quint32 startCluster;

    if (parentPath.isEmpty() || parentPath == "/" || parentPath == "\\") {
        startCluster = readRootDirCluster();
    } else {
        QFATError error;
        QFATFileInfo dirInfo = findFileByPath(parentPath, error);
        if (error != QFATError::None || !dirInfo.isDirectory) {
            return false;
        }
        startCluster = dirInfo.cluster;
    }

    // For FAT32, all directories are cluster-based and can span multiple clusters
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = bytesPerSector * sectorsPerCluster;
    quint32 entriesPerCluster = clusterSize / ENTRY_SIZE;

    // Follow the cluster chain to find existing entry or free slot
    quint32 currentCluster = startCluster;
    quint32 freeSlotOffset = 0;
    bool foundExisting = false;
    bool foundFree = false;
    quint32 existingOffset = 0;

    while (currentCluster >= 2 && currentCluster < 0x0FFFFFF8) {
        quint32 clusterOffset = calculateClusterOffset(currentCluster);
        quint32 entryOffset = clusterOffset;

        for (quint32 i = 0; i < entriesPerCluster; i++) {
            m_stream.device()->seek(entryOffset);
            quint8 entry[ENTRY_SIZE];
            m_stream.readRawData(reinterpret_cast<char*>(entry), ENTRY_SIZE);

            quint8 firstByte = entry[ENTRY_NAME_OFFSET];

            if (firstByte == ENTRY_END_OF_DIRECTORY) {
                if (!foundFree) {
                    freeSlotOffset = entryOffset;
                    foundFree = true;
                }
                break; // End of directory entries in this cluster
            }

            if (firstByte == ENTRY_DELETED) {
                if (!foundFree) {
                    freeSlotOffset = entryOffset;
                    foundFree = true;
                }
                entryOffset += ENTRY_SIZE;
                continue;
            }

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
                existingOffset = entryOffset;
                foundExisting = true;
                break;
            }

            entryOffset += ENTRY_SIZE;
        }

        // If found what we need, stop searching
        if (foundExisting || foundFree) {
            break;
        }

        // Move to next cluster in chain
        currentCluster = readNextCluster(currentCluster);
    }

    // Use existing entry offset if found, otherwise use free slot
    if (foundExisting) {
        return createDirectoryEntry(existingOffset, fileInfo);
    } else if (foundFree) {
        // Store mapping for files that might need it
        if (!fileInfo.longName.isEmpty() && fileInfo.longName.toLower() != fileInfo.name.toLower()) {
            m_longToShortNameMap[fileInfo.longName.toLower()] = fileInfo.name;
            qDebug() << "[FAT32 updateDirectoryEntry] Stored mapping:" << fileInfo.longName.toLower() << "->" << fileInfo.name;
        }
        return createDirectoryEntry(freeSlotOffset, fileInfo);
    }

    // TODO: Could allocate a new cluster and extend the directory here
    return false;
}

bool QFAT32FileSystem::writeFile(const QString &path, const QByteArray &data, QFATError &error)
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
        freeClusterChain(existingFile.cluster);
    }

    // Allocate new cluster chain (only if we have data)
    QList<quint32> clusters;
    quint32 firstCluster = 0;

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
            parentEntries = listDirectory(parentInfo.cluster);
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

bool QFAT32FileSystem::exists(const QString &path)
{
    QFATError error;
    QFATFileInfo info = findFileByPath(path, error);
    return error == QFATError::None && !info.name.isEmpty();
}

QFATFileInfo QFAT32FileSystem::getFileInfo(const QString &path, QFATError &error)
{
    return findFileByPath(path, error);
}

bool QFAT32FileSystem::deleteDirectoryEntry(const QString &path)
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

    // Find the parent directory cluster
    quint32 startCluster;
    if (parentPath.isEmpty() || parentPath == "/" || parentPath == "\\") {
        startCluster = readRootDirCluster();
    } else {
        QFATError error;
        QFATFileInfo dirInfo = findFileByPath(parentPath, error);
        if (error != QFATError::None || !dirInfo.isDirectory) {
            return false;
        }
        startCluster = dirInfo.cluster;
    }

    // Scan through the directory clusters to find the entry
    quint16 bytesPerSector = readBytesPerSector();
    quint8 sectorsPerCluster = readSectorsPerCluster();
    quint32 clusterSize = bytesPerSector * sectorsPerCluster;
    quint32 entriesPerCluster = clusterSize / ENTRY_SIZE;

    quint32 currentCluster = startCluster;
    bool found = false;
    quint32 foundOffset = 0;

    while (currentCluster >= 2 && currentCluster < 0x0FFFFFF8) {
        quint32 clusterOffset = calculateClusterOffset(currentCluster);
        quint32 entryOffset = clusterOffset;

        for (quint32 i = 0; i < entriesPerCluster; i++) {
            m_stream.device()->seek(entryOffset);
            quint8 entryData[ENTRY_SIZE];
            m_stream.readRawData(reinterpret_cast<char*>(entryData), ENTRY_SIZE);

            quint8 firstByte = entryData[ENTRY_NAME_OFFSET];

            // End of directory
            if (firstByte == ENTRY_END_OF_DIRECTORY) {
                break;
            }

            // Skip deleted entries
            if (firstByte == ENTRY_DELETED) {
                entryOffset += ENTRY_SIZE;
                continue;
            }

            // Skip LFN entries
            if (isLongFileNameEntry(entryData)) {
                entryOffset += ENTRY_SIZE;
                continue;
            }

            // Parse the short name
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

            // Check if this is the file we're looking for
            if (name8_3.toUpper() == fileName.toUpper()) {
                found = true;
                foundOffset = entryOffset;
                break;
            }

            entryOffset += ENTRY_SIZE;
        }

        if (found) {
            break;
        }

        // Move to next cluster
        currentCluster = readNextCluster(currentCluster);
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

bool QFAT32FileSystem::isDirectoryEmpty(quint32 cluster)
{
    if (cluster < 2) {
        return true;
    }

    QList<QFATFileInfo> entries = listDirectory(cluster);

    // An empty directory should have no entries (. and .. are filtered out by listDirectory)
    return entries.isEmpty();
}

bool QFAT32FileSystem::deleteFile(const QString &path, QFATError &error)
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
        if (!isDirectoryEmpty(fileInfo.cluster)) {
            error = QFATError::InvalidPath;
            m_lastError = error;
            return false;
        }
    }

    // Free the cluster chain
    if (fileInfo.cluster >= 2) {
        if (!freeClusterChain(fileInfo.cluster)) {
            error = QFATError::WriteError;
            m_lastError = error;
            return false;
        }
    }

    // Delete the directory entry - use the actual short name we found
    QString deletePath = path;
    // If we found the file, reconstruct the path with its actual short name
    if (!fileInfo.name.isEmpty()) {
        QStringList parts = splitPath(path);
        if (parts.size() > 1) {
            parts.removeLast();
            deletePath = "/" + parts.join("/") + "/" + fileInfo.name;
        } else {
            deletePath = "/" + fileInfo.name;
        }
    }
    if (!deleteDirectoryEntry(deletePath)) {
        error = QFATError::WriteError;
        m_lastError = error;
        return false;
    }

    return true;
}

bool QFAT32FileSystem::createDirectory(const QString &path, QFATError &error)
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
    QList<quint32> clusters = allocateClusterChain(1);
    if (clusters.isEmpty()) {
        error = QFATError::InsufficientSpace;
        m_lastError = error;
        return false;
    }

    quint32 dirCluster = clusters.first();

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

    // For FAT32, write both low and high cluster words
    quint16 clusterLow = static_cast<quint16>(dirCluster & 0xFFFF);
    quint16 clusterHigh = static_cast<quint16>((dirCluster >> 16) & 0xFFFF);
    dirPtr[ENTRY_CLUSTER_OFFSET] = clusterLow & 0xFF;
    dirPtr[ENTRY_CLUSTER_OFFSET + 1] = (clusterLow >> 8) & 0xFF;
    dirPtr[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET] = clusterHigh & 0xFF;
    dirPtr[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET + 1] = (clusterHigh >> 8) & 0xFF;

    // Create .. entry (parent directory)
    dirPtr += ENTRY_SIZE;
    memset(dirPtr, ' ', ENTRY_SIZE);
    dirPtr[0] = '.';
    dirPtr[1] = '.';
    dirPtr[ENTRY_ATTRIBUTE_OFFSET] = ENTRY_ATTRIBUTE_DIRECTORY;

    // For root directory, parent cluster is root cluster; otherwise use parent's cluster
    quint32 parentCluster = readRootDirCluster();
    if (parentPath != "/") {
        QFATError parentError;
        QFATFileInfo parentInfo = findFileByPath(parentPath, parentError);
        if (parentError == QFATError::None) {
            parentCluster = parentInfo.cluster;
        }
    }

    clusterLow = static_cast<quint16>(parentCluster & 0xFFFF);
    clusterHigh = static_cast<quint16>((parentCluster >> 16) & 0xFFFF);
    dirPtr[ENTRY_CLUSTER_OFFSET] = clusterLow & 0xFF;
    dirPtr[ENTRY_CLUSTER_OFFSET + 1] = (clusterLow >> 8) & 0xFF;
    dirPtr[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET] = clusterHigh & 0xFF;
    dirPtr[ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET + 1] = (clusterHigh >> 8) & 0xFF;

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
            parentEntries = listDirectory(parentInfo.cluster);
        }
    }

    dirInfo.name = generateShortName(dirName, parentEntries);
    // Since we're not writing LFN entries, set longName to match the short name
    // This prevents garbage LFN data from being associated with this entry
    dirInfo.longName = dirInfo.name;
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
