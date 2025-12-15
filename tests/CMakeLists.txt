find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

set(test_libraries QFATFS Qt${QT_VERSION_MAJOR}::Test)

# Generate test images using the script
set(GENERATE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_images.sh")
set(TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

# Option to skip test image generation (useful for Docker builds where images are pre-generated)
option(SKIP_TEST_IMAGE_GENERATION "Skip automatic test image generation" OFF)

# Check for required tools
if(NOT SKIP_TEST_IMAGE_GENERATION)
    if(UNIX AND NOT APPLE)
        # Linux: Check for dosfstools and mtools
        find_program(MKFS_FAT mkfs.fat)
        find_program(MTOOLS_MMD mmd)

        if(NOT MKFS_FAT)
            message(WARNING "mkfs.fat not found. Install dosfstools package.\n"
                           "  Ubuntu/Debian: sudo apt-get install dosfstools mtools\n"
                           "  Fedora/RHEL: sudo dnf install dosfstools mtools\n"
                           "  Arch Linux: sudo pacman -S dosfstools mtools")
        endif()

        if(NOT MTOOLS_MMD)
            message(WARNING "mtools not found. Install mtools package.\n"
                           "  Ubuntu/Debian: sudo apt-get install mtools\n"
                           "  Fedora/RHEL: sudo dnf install mtools\n"
                           "  Arch Linux: sudo pacman -S mtools")
        endif()

        if(MKFS_FAT AND MTOOLS_MMD)
            message(STATUS "Found mkfs.fat: ${MKFS_FAT}")
            message(STATUS "Found mtools: ${MTOOLS_MMD}")
        endif()
    elseif(APPLE)
        # macOS: Check for hdiutil (should be built-in)
        find_program(HDIUTIL hdiutil)
        if(NOT HDIUTIL)
            message(WARNING "hdiutil not found (this should be available on macOS)")
        else()
            message(STATUS "Found hdiutil: ${HDIUTIL}")
        endif()
    endif()

    # Add custom command to generate test images
    add_custom_command(
        OUTPUT ${TEST_DATA_DIR}/fat12.img ${TEST_DATA_DIR}/fat16.img ${TEST_DATA_DIR}/fat32.img
        COMMAND chmod +x ${GENERATE_SCRIPT}
        COMMAND ${GENERATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating FAT test images..."
        VERBATIM
    )

    # Add custom target for image generation
    add_custom_target(generate_test_images
        DEPENDS ${TEST_DATA_DIR}/fat12.img ${TEST_DATA_DIR}/fat16.img ${TEST_DATA_DIR}/fat32.img
    )

    # Copy generated test files to build directory
    add_custom_command(
        TARGET generate_test_images POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/data
        COMMAND ${CMAKE_COMMAND} -E copy ${TEST_DATA_DIR}/fat12.img ${CMAKE_CURRENT_BINARY_DIR}/data/
        COMMAND ${CMAKE_COMMAND} -E copy ${TEST_DATA_DIR}/fat16.img ${CMAKE_CURRENT_BINARY_DIR}/data/
        COMMAND ${CMAKE_COMMAND} -E copy ${TEST_DATA_DIR}/fat32.img ${CMAKE_CURRENT_BINARY_DIR}/data/
        COMMENT "Copying test images to build directory..."
    )
else()
    # When skipping generation, just copy existing images to build directory
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data)
    file(COPY ${TEST_DATA_DIR}/fat12.img ${TEST_DATA_DIR}/fat16.img ${TEST_DATA_DIR}/fat32.img
         DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
    message(STATUS "Skipping test image generation, using pre-existing images")
endif()

# Add test executables
add_executable(test_common test_common.cpp)
add_executable(test_fat12_read test_fat12_read.cpp)
add_executable(test_fat16_read test_fat16_read.cpp)
add_executable(test_fat32_read test_fat32_read.cpp)
add_executable(test_fat12_write test_fat12_write.cpp)
add_executable(test_fat16_write test_fat16_write.cpp)
add_executable(test_fat32_write test_fat32_write.cpp)
add_executable(test_fat12_directories test_fat12_directories.cpp)
add_executable(test_fat16_directories test_fat16_directories.cpp)
add_executable(test_fat32_directories test_fat32_directories.cpp)
add_executable(test_fat16_advanced test_fat16_advanced.cpp)
add_executable(test_fat32_advanced test_fat32_advanced.cpp)
add_executable(test_fat12_advanced test_fat12_advanced.cpp)

# Make tests depend on image generation (only if not skipping)
if(NOT SKIP_TEST_IMAGE_GENERATION)
    add_dependencies(test_common generate_test_images)
    add_dependencies(test_fat12_read generate_test_images)
    add_dependencies(test_fat16_read generate_test_images)
    add_dependencies(test_fat32_read generate_test_images)
    add_dependencies(test_fat12_write generate_test_images)
    add_dependencies(test_fat16_write generate_test_images)
    add_dependencies(test_fat32_write generate_test_images)
    add_dependencies(test_fat12_directories generate_test_images)
    add_dependencies(test_fat16_directories generate_test_images)
    add_dependencies(test_fat32_directories generate_test_images)
    add_dependencies(test_fat16_advanced generate_test_images)
    add_dependencies(test_fat32_advanced generate_test_images)
    add_dependencies(test_fat12_advanced generate_test_images)
endif()

# Add test targets
add_test(TestCommonOperations test_common)
add_test(TestFAT12ReadOperations test_fat12_read)
add_test(TestFAT16ReadOperations test_fat16_read)
add_test(TestFAT32ReadOperations test_fat32_read)
add_test(TestFAT12WriteOperations test_fat12_write)
add_test(TestFAT16WriteOperations test_fat16_write)
add_test(TestFAT32WriteOperations test_fat32_write)
add_test(TestFAT12DirectoryOperations test_fat12_directories)
add_test(TestFAT16DirectoryOperations test_fat16_directories)
add_test(TestFAT32DirectoryOperations test_fat32_directories)
add_test(TestFAT16AdvancedOperations test_fat16_advanced)
add_test(TestFAT32AdvancedOperations test_fat32_advanced)
add_test(TestFAT12AdvancedOperations test_fat12_advanced)

# Link test libraries
target_link_libraries(test_common ${test_libraries})
target_link_libraries(test_fat12_read ${test_libraries})
target_link_libraries(test_fat16_read ${test_libraries})
target_link_libraries(test_fat32_read ${test_libraries})
target_link_libraries(test_fat12_write ${test_libraries})
target_link_libraries(test_fat16_write ${test_libraries})
target_link_libraries(test_fat32_write ${test_libraries})
target_link_libraries(test_fat12_directories ${test_libraries})
target_link_libraries(test_fat16_directories ${test_libraries})
target_link_libraries(test_fat32_directories ${test_libraries})
target_link_libraries(test_fat16_advanced ${test_libraries})
target_link_libraries(test_fat32_advanced ${test_libraries})
target_link_libraries(test_fat12_advanced ${test_libraries})

