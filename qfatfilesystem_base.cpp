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
        if (entryShortName == upperName) {
            qDebug() << "[findInDirectory] Matched" << name << "to short:" << entry.name;
            return entry;
        } else if (entryLongName == upperName) {
            qDebug() << "[findInDirectory] Matched" << name << "to long:" << entry.longName;
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

    if (!m_device->isOpen()) {
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
