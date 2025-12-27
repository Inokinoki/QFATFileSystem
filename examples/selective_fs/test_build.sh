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

    # Create a temporary source directory with the specific CMakeLists
    TEMP_SRC="temp_src_${NAME}"
    rm -rf "$TEMP_SRC"
    mkdir -p "$TEMP_SRC"
    cp "${CMAKE_FILE}" "$TEMP_SRC/CMakeLists.txt"
    cp "example_${NAME}_only.cpp" "$TEMP_SRC/"

    cd "$TEST_DIR"

    # Try to configure and build (point to temp source directory)
    if cmake "../${TEMP_SRC}" > cmake_output.log 2>&1; then
        echo -e "${GREEN}✓ CMake configuration successful${NC}"
    else
        echo -e "${RED}✗ CMake configuration failed${NC}"
        echo "See ${TEST_DIR}/cmake_output.log for details"
        cd ..
        rm -rf "$TEMP_SRC"
        return 1
    fi

    # Use cmake --build for better cross-platform compatibility
    if cmake --build . > make_output.log 2>&1; then
        echo -e "${GREEN}✓ Build successful${NC}"

        # Check binary exists (try both current directory and common build locations)
        BINARY_PATH=""
        if [ -f "example_${NAME}_only" ]; then
            BINARY_PATH="example_${NAME}_only"
        elif [ -f "./example_${NAME}_only" ]; then
            BINARY_PATH="./example_${NAME}_only"
        elif [ -f "bin/example_${NAME}_only" ]; then
            BINARY_PATH="bin/example_${NAME}_only"
        fi

        if [ -n "$BINARY_PATH" ]; then
            SIZE=$(stat -f%z "$BINARY_PATH" 2>/dev/null || stat -c%s "$BINARY_PATH" 2>/dev/null || echo "unknown")
            echo "  Binary size: ${SIZE} bytes"
        else
            echo -e "${RED}✗ Binary not found: example_${NAME}_only${NC}"
            echo "  Files in build directory:"
            ls -la . | head -20
            echo "See ${TEST_DIR}/make_output.log for details"
            cd ..
            rm -rf "$TEMP_SRC"
            return 1
        fi
    else
        echo -e "${RED}✗ Build failed${NC}"
        echo "See ${TEST_DIR}/make_output.log for details"
        cd ..
        rm -rf "$TEMP_SRC"
        return 1
    fi

    echo ""
    cd ..

    # Cleanup temp source directory
    rm -rf "$TEMP_SRC"

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
