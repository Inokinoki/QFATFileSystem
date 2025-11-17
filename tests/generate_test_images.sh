#!/bin/bash

# Script to generate FAT16 and FAT32 test images with known content
# Compatible with macOS and Linux

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/data"

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    IS_MACOS=1
else
    IS_MACOS=0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check for required commands
check_requirements() {
    local missing=0

    if [[ $IS_MACOS -eq 1 ]]; then
        if ! command -v hdiutil &> /dev/null; then
            echo_error "hdiutil not found (required on macOS)"
            missing=1
        fi
    else
        if ! command -v mkfs.fat &> /dev/null; then
            echo_error "mkfs.fat not found. Install dosfstools package."
            missing=1
        fi
        if ! command -v mount &> /dev/null; then
            echo_error "mount command not found"
            missing=1
        fi
    fi

    if ! command -v dd &> /dev/null; then
        echo_error "dd not found"
        missing=1
    fi

    if [[ $missing -eq 1 ]]; then
        echo_error "Missing required tools. Exiting."
        exit 1
    fi
}

# Create output directory
mkdir -p "${OUTPUT_DIR}"

# Function to create and mount a FAT image
create_fat_image() {
    local IMAGE_PATH=$1
    local SIZE_MB=$2
    local FAT_TYPE=$3
    local MOUNT_POINT=$4

    echo_info "Creating ${FAT_TYPE} image: ${IMAGE_PATH} (${SIZE_MB}MB)"

    # Create empty image file
    # Use bs=1M for both Linux and macOS (both support capital M)
    dd if=/dev/zero of="${IMAGE_PATH}" bs=1M count=${SIZE_MB} 2>/dev/null || {
        echo_error "Failed to create image file"
        exit 1
    }

    if [[ $IS_MACOS -eq 1 ]]; then
        # macOS-specific approach using hdiutil
        if [[ "${FAT_TYPE}" == "FAT16" ]]; then
            hdiutil attach -nomount "${IMAGE_PATH}" > /tmp/hdiutil_output.txt
            local DISK_DEV=$(cat /tmp/hdiutil_output.txt | awk '{print $1}')
            newfs_msdos -F 16 -v "FAT16TEST" "${DISK_DEV}"
            hdiutil detach "${DISK_DEV}"
            rm -f /tmp/hdiutil_output.txt

            # Mount the image
            mkdir -p "${MOUNT_POINT}"
            hdiutil attach "${IMAGE_PATH}" -mountpoint "${MOUNT_POINT}"
        else
            # FAT32
            hdiutil attach -nomount "${IMAGE_PATH}" > /tmp/hdiutil_output.txt
            local DISK_DEV=$(cat /tmp/hdiutil_output.txt | awk '{print $1}')
            newfs_msdos -F 32 -v "FAT32TEST" "${DISK_DEV}"
            hdiutil detach "${DISK_DEV}"
            rm -f /tmp/hdiutil_output.txt

            # Mount the image
            mkdir -p "${MOUNT_POINT}"
            hdiutil attach "${IMAGE_PATH}" -mountpoint "${MOUNT_POINT}"
        fi
    else
        # Linux-specific approach
        if [[ "${FAT_TYPE}" == "FAT16" ]]; then
            mkfs.fat -F 16 -n "FAT16TEST" "${IMAGE_PATH}" || {
                echo_error "Failed to create FAT16 filesystem"
                exit 1
            }
        else
            mkfs.fat -F 32 -n "FAT32TEST" "${IMAGE_PATH}" || {
                echo_error "Failed to create FAT32 filesystem"
                exit 1
            }
        fi
        
        # Mount the image
        mkdir -p "${MOUNT_POINT}"
        sudo mount -o loop "${IMAGE_PATH}" "${MOUNT_POINT}" || {
            echo_error "Failed to mount ${IMAGE_PATH}"
            exit 1
        }
        # Make it writable for current user
        sudo chmod 777 "${MOUNT_POINT}"
    fi

    echo_info "Image mounted at ${MOUNT_POINT}"
}

# Function to unmount and cleanup
unmount_image() {
    local MOUNT_POINT=$1

    if [[ $IS_MACOS -eq 1 ]]; then
        hdiutil detach "${MOUNT_POINT}" 2>/dev/null || true
    else
        sudo umount "${MOUNT_POINT}" 2>/dev/null || true
    fi

    rmdir "${MOUNT_POINT}" 2>/dev/null || true
}

# Function to populate image with test content
populate_test_content() {
    local MOUNT_POINT=$1

    echo_info "Populating test content at ${MOUNT_POINT}..."

    # Verify mount point is accessible
    if [[ ! -d "${MOUNT_POINT}" ]]; then
        echo_error "Mount point ${MOUNT_POINT} is not accessible"
        return 1
    fi

    # Create test files in root
    echo "Hello, World!" > "${MOUNT_POINT}/hello.txt"
    echo "This is a test file for FAT filesystem testing." > "${MOUNT_POINT}/test.txt"
    echo "README content" > "${MOUNT_POINT}/readme.txt"

    # Create a file with long filename
    echo "Long filename test" > "${MOUNT_POINT}/this_is_a_long_filename.txt"

    # Create a binary file
    dd if=/dev/urandom of="${MOUNT_POINT}/binary.dat" bs=1024 count=10 2>/dev/null

    # Create empty file
    touch "${MOUNT_POINT}/empty.txt"

    # Create directories
    mkdir -p "${MOUNT_POINT}/subdir1"
    mkdir -p "${MOUNT_POINT}/subdir2"
    mkdir -p "${MOUNT_POINT}/Documents"

    # Create files in subdirectories
    echo "File in subdir1" > "${MOUNT_POINT}/subdir1/file1.txt"
    echo "File in subdir1 again" > "${MOUNT_POINT}/subdir1/file2.txt"
    mkdir -p "${MOUNT_POINT}/subdir1/nested"
    echo "Nested file" > "${MOUNT_POINT}/subdir1/nested/nested.txt"

    echo "File in subdir2" > "${MOUNT_POINT}/subdir2/data.txt"

    echo "Document 1" > "${MOUNT_POINT}/Documents/doc1.txt"
    echo "Document 2" > "${MOUNT_POINT}/Documents/doc2.txt"

    # Create a file with specific size for testing
    dd if=/dev/zero of="${MOUNT_POINT}/largefile.bin" bs=1024 count=100 2>/dev/null

    # Sync to ensure all data is written to disk
    sync

    echo_info "Test content created successfully"
}

# Main execution
echo_info "Generating FAT test images..."
check_requirements

# Clean up any existing mount points
unmount_image "/tmp/fat16_mount" 2>/dev/null || true
unmount_image "/tmp/fat32_mount" 2>/dev/null || true

# Generate FAT16 image
FAT16_IMAGE="${OUTPUT_DIR}/fat16.img"
FAT16_MOUNT="/tmp/fat16_mount"

if [[ -f "${FAT16_IMAGE}" ]]; then
    echo_warn "Removing existing FAT16 image"
    rm -f "${FAT16_IMAGE}"
fi

create_fat_image "${FAT16_IMAGE}" 16 "FAT16" "${FAT16_MOUNT}"
populate_test_content "${FAT16_MOUNT}" || {
    echo_error "Failed to populate FAT16 image"
    unmount_image "${FAT16_MOUNT}"
    exit 1
}
unmount_image "${FAT16_MOUNT}"
echo_info "FAT16 image created successfully: ${FAT16_IMAGE}"

# Generate FAT32 image
FAT32_IMAGE="${OUTPUT_DIR}/fat32.img"
FAT32_MOUNT="/tmp/fat32_mount"

if [[ -f "${FAT32_IMAGE}" ]]; then
    echo_warn "Removing existing FAT32 image"
    rm -f "${FAT32_IMAGE}"
fi

create_fat_image "${FAT32_IMAGE}" 64 "FAT32" "${FAT32_MOUNT}"
populate_test_content "${FAT32_MOUNT}" || {
    echo_error "Failed to populate FAT32 image"
    unmount_image "${FAT32_MOUNT}"
    exit 1
}
unmount_image "${FAT32_MOUNT}"
echo_info "FAT32 image created successfully: ${FAT32_IMAGE}"

echo_info "All test images generated successfully!"
echo_info "FAT16: ${FAT16_IMAGE}"
echo_info "FAT32: ${FAT32_IMAGE}"

