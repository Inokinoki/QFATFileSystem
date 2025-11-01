// ============================================================================
// FAT constants
// ============================================================================
#define FAT16_SIGNATURE 0x55AA
#define FAT32_SIGNATURE 0x55AA

// ============================================================================
// FAT BIOS Parameter Block constants
// ============================================================================
#define BPB_BYTES_PER_SECTOR_OFFSET 0x0B
#define BPB_SECTORS_PER_CLUSTER_OFFSET 0x0D
#define BPB_RESERVED_SECTORS_OFFSET 0x0E
#define BPB_NUMBER_OF_FATS_OFFSET 0x10
#define BPB_ROOT_ENTRY_COUNT_OFFSET 0x11
#define BPB_SECTORS_PER_FAT_OFFSET 0x16
#define BPB_ROOT_DIRECTORY_CLUSTER_OFFSET 0x2C

// ============================================================================
// Data area constants
// ============================================================================
#define DATA_AREA_SIZE 64 * 1024 // 64KB per data area

// ============================================================================
// Entry constants
// ============================================================================
#define ENTRY_SIZE 32 // 32 bytes per entry

#define ENTRY_END_OF_DIRECTORY 0x00
#define ENTRY_DELETED 0xE5
#define ENTRY_CURRENT_DIRECTORY 0x2E

#define ENTRY_NAME_OFFSET 0x00
#define ENTRY_NAME_LENGTH 11
#define ENTRY_ATTRIBUTE_OFFSET 0x0B
#define ENTRY_ATTRIBUTE_LENGTH 1
#define ENTRY_CREATION_DATE_TIME_OFFSET 0x0C
#define ENTRY_CREATION_DATE_TIME_LENGTH 5
#define ENTRY_ACCESSED_DATE_OFFSET 0x12
#define ENTRY_ACCESSED_DATE_LENGTH 2
#define ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_OFFSET 0x14
#define ENTRY_HIGH_ORDER_CLUSTER_ADDRESS_LENGTH 2
#define ENTRY_WRITTEN_DATE_TIME_OFFSET 0x16
#define ENTRY_WRITTEN_DATE_TIME_LENGTH 4
#define ENTRY_CLUSTER_OFFSET 0x1A
#define ENTRY_CLUSTER_LENGTH 2
#define ENTRY_SIZE_OFFSET 0x1C
#define ENTRY_SIZE_LENGTH 4

#define ENTRY_ATTRIBUTE_READ_ONLY 0x01
#define ENTRY_ATTRIBUTE_HIDDEN 0x02
#define ENTRY_ATTRIBUTE_SYSTEM 0x04
#define ENTRY_ATTRIBUTE_VOLUME_LABEL 0x08
#define ENTRY_ATTRIBUTE_DIRECTORY 0x10
#define ENTRY_ATTRIBUTE_ARCHIVE 0x20
#define ENTRY_ATTRIBUTE_LONG_FILE_NAME 0x0F

#define ENTRY_DATE_TIME_START_OF_YEAR 1980

// 0x0 (1B): sequence number, starting at 1, not 0; last one is ORed with 0x40
#define ENTRY_LFN_SEQUENCE_START 1
#define ENTRY_LFN_SEQUENCE_MASK 0x1F
#define ENTRY_LFN_SEQUENCE_LAST_MASK 0x40
#define ENTRY_LFN_CHARS 13 // 13 characters per part
#define ENTRY_LFN_PART1_OFFSET 0x01
#define ENTRY_LFN_PART1_LENGTH 10
#define ENTRY_LFN_PART2_OFFSET 0x0E
#define ENTRY_LFN_PART2_LENGTH 12
#define ENTRY_LFN_PART3_OFFSET 0x1B
#define ENTRY_LFN_PART3_LENGTH 4

// ============================================================================
// Helper masks
// ============================================================================
#define MASK_1_BIT 0x01
#define MASK_2_BITS 0x03
#define MASK_3_BITS 0x07
#define MASK_4_BITS 0x0F
#define MASK_5_BITS 0x1F
#define MASK_6_BITS 0x3F
#define MASK_7_BITS 0x7F
#define MASK_8_BITS 0xFF
