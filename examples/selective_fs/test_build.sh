#!/bin/bash
# Test script to verify selective filesystem builds work correctly

set -e  # Exit on error

echo "=== Testing Selective Filesystem Builds ==="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to test a build
test_build() {
    local NAME=$1
    local CMAKE_FILE=$2

    echo "Testing: $NAME"
    echo "-------------------"

    # Create test directory
    TEST_DIR="build_test_${NAME}"
    rm -rf "$TEST_DIR"
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"

    # Copy cmake file
    cp "../${CMAKE_FILE}" CMakeLists.txt

    # Try to configure and build
    if cmake .. > cmake_output.log 2>&1; then
        echo -e "${GREEN}✓ CMake configuration successful${NC}"
    else
        echo -e "${RED}✗ CMake configuration failed${NC}"
        echo "See ${TEST_DIR}/cmake_output.log for details"
        cd ..
        return 1
    fi

    if make > make_output.log 2>&1; then
        echo -e "${GREEN}✓ Build successful${NC}"

        # Check binary sizes
        if [ -f "example_${NAME}_only" ]; then
            SIZE=$(stat -f%z "example_${NAME}_only" 2>/dev/null || stat -c%s "example_${NAME}_only" 2>/dev/null || echo "unknown")
            echo "  Binary size: ${SIZE} bytes"
        fi
    else
        echo -e "${RED}✗ Build failed${NC}"
        echo "See ${TEST_DIR}/make_output.log for details"
        cd ..
        return 1
    fi

    echo ""
    cd ..
    return 0
}

# Test each filesystem type
FAILURES=0

test_build "fat12" "CMakeLists_FAT12_only.txt" || ((FAILURES++))
test_build "fat16" "CMakeLists_FAT16_only.txt" || ((FAILURES++))
test_build "fat32" "CMakeLists_FAT32_only.txt" || ((FAILURES++))

# Summary
echo "=== Test Summary ==="
if [ $FAILURES -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}${FAILURES} test(s) failed${NC}"
    exit 1
fi
