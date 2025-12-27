#!/bin/bash
# Script to demonstrate binary size differences between full and selective builds

echo "=== Binary Size Comparison ==="
echo ""
echo "This script compares the object file sizes when building:"
echo "  1. All filesystems (FAT12 + FAT16 + FAT32)"
echo "  2. Single filesystem only"
echo ""

cd ../..

# Colors
BLUE='\033[0;34m'
GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${BLUE}Object File Sizes:${NC}"
echo "-------------------"

# Check if build directory exists
OBJ_DIR="build/CMakeFiles/QFATFS.dir"
if [ ! -d "$OBJ_DIR" ]; then
    echo "Build directory not found. Please build the project first:"
    echo "  cd ../.. && mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

# Get sizes of object files (using ls to get human-readable sizes)
get_size() {
    local FILE=$1
    if [ -f "$FILE" ]; then
        stat -f%z "$FILE" 2>/dev/null || stat -c%s "$FILE" 2>/dev/null || echo 0
    else
        echo 0
    fi
}

BASE_SIZE=$(get_size "${OBJ_DIR}/qfatfilesystem_base.cpp.o")
FAT12_SIZE=$(get_size "${OBJ_DIR}/qfat12filesystem.cpp.o")
FAT16_SIZE=$(get_size "${OBJ_DIR}/qfat16filesystem.cpp.o")
FAT32_SIZE=$(get_size "${OBJ_DIR}/qfat32filesystem.cpp.o")

# Format sizes
format_size() {
    local SIZE=$1
    local KB=$((SIZE / 1024))
    echo "${SIZE} bytes (${KB} KB)"
}

echo "Base (required):          $(format_size $BASE_SIZE)"
echo "FAT12 implementation:     $(format_size $FAT12_SIZE)"
echo "FAT16 implementation:     $(format_size $FAT16_SIZE)"
echo "FAT32 implementation:     $(format_size $FAT32_SIZE)"
echo ""

# Calculate combinations
ALL_SIZE=$((BASE_SIZE + FAT12_SIZE + FAT16_SIZE + FAT32_SIZE))
FAT12_ONLY=$((BASE_SIZE + FAT12_SIZE))
FAT16_ONLY=$((BASE_SIZE + FAT16_SIZE))
FAT32_ONLY=$((BASE_SIZE + FAT32_SIZE))

echo -e "${BLUE}Library Size by Configuration:${NC}"
echo "--------------------------------"
echo "All filesystems (Full):   $(format_size $ALL_SIZE)"
echo "FAT12 only:               $(format_size $FAT12_ONLY)"
echo "FAT16 only:               $(format_size $FAT16_ONLY)"
echo "FAT32 only:               $(format_size $FAT32_ONLY)"
echo ""

# Calculate savings
echo -e "${GREEN}Size Savings:${NC}"
echo "-------------"
FAT12_SAVINGS=$((ALL_SIZE - FAT12_ONLY))
FAT16_SAVINGS=$((ALL_SIZE - FAT16_ONLY))
FAT32_SAVINGS=$((ALL_SIZE - FAT32_ONLY))

FAT12_PERCENT=$((FAT12_SAVINGS * 100 / ALL_SIZE))
FAT16_PERCENT=$((FAT16_SAVINGS * 100 / ALL_SIZE))
FAT32_PERCENT=$((FAT32_SAVINGS * 100 / ALL_SIZE))

echo "FAT12 only vs Full:  -$(format_size $FAT12_SAVINGS) (-${FAT12_PERCENT}%)"
echo "FAT16 only vs Full:  -$(format_size $FAT16_SAVINGS) (-${FAT16_PERCENT}%)"
echo "FAT32 only vs Full:  -$(format_size $FAT32_SAVINGS) (-${FAT32_PERCENT}%)"
echo ""

echo "=== Recommendations ==="
echo "• Embedded systems with limited flash: Use single filesystem only"
echo "• Desktop applications: Include only needed filesystems"
echo "• General purpose libraries: Include all for compatibility"
echo ""
echo "Note: These are object file sizes. Final binary size depends on linking and optimization."
