find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

set(test_libraries QFATFS Qt${QT_VERSION_MAJOR}::Test)

# Generate test images using the script
set(GENERATE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_images.sh")
set(TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

# Add custom command to generate test images
add_custom_command(
    OUTPUT ${TEST_DATA_DIR}/fat16.img ${TEST_DATA_DIR}/fat32.img
    COMMAND chmod +x ${GENERATE_SCRIPT}
    COMMAND ${GENERATE_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating FAT test images..."
    VERBATIM
)

# Add custom target for image generation
add_custom_target(generate_test_images
    DEPENDS ${TEST_DATA_DIR}/fat16.img ${TEST_DATA_DIR}/fat32.img
)

# Copy generated test files to build directory
add_custom_command(
    TARGET generate_test_images POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy ${TEST_DATA_DIR}/fat16.img ${CMAKE_CURRENT_BINARY_DIR}/data/
    COMMAND ${CMAKE_COMMAND} -E copy ${TEST_DATA_DIR}/fat32.img ${CMAKE_CURRENT_BINARY_DIR}/data/
    COMMENT "Copying test images to build directory..."
)

# Add test executables
add_executable(test_common test_common.cpp)
add_executable(test_fat16_read test_fat16_read.cpp)
add_executable(test_fat32_read test_fat32_read.cpp)
add_executable(test_fat16_write test_fat16_write.cpp)
add_executable(test_fat32_write test_fat32_write.cpp)
add_executable(test_fat16_directories test_fat16_directories.cpp)
add_executable(test_fat32_directories test_fat32_directories.cpp)

# Make tests depend on image generation
add_dependencies(test_common generate_test_images)
add_dependencies(test_fat16_read generate_test_images)
add_dependencies(test_fat32_read generate_test_images)
add_dependencies(test_fat16_write generate_test_images)
add_dependencies(test_fat32_write generate_test_images)
add_dependencies(test_fat16_directories generate_test_images)
add_dependencies(test_fat32_directories generate_test_images)

# Add test targets
add_test(TestCommonOperations test_common)
add_test(TestFAT16ReadOperations test_fat16_read)
add_test(TestFAT32ReadOperations test_fat32_read)
add_test(TestFAT16WriteOperations test_fat16_write)
add_test(TestFAT32WriteOperations test_fat32_write)
add_test(TestFAT16DirectoryOperations test_fat16_directories)
add_test(TestFAT32DirectoryOperations test_fat32_directories)

# Link test libraries
target_link_libraries(test_common ${test_libraries})
target_link_libraries(test_fat16_read ${test_libraries})
target_link_libraries(test_fat32_read ${test_libraries})
target_link_libraries(test_fat16_write ${test_libraries})
target_link_libraries(test_fat32_write ${test_libraries})
target_link_libraries(test_fat16_directories ${test_libraries})
target_link_libraries(test_fat32_directories ${test_libraries})

