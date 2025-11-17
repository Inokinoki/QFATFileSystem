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

# Add test executable
add_executable(test_qfatfilesystem_read_image test_qfatfilesystem_read_image.cpp)

# Make test depend on image generation
add_dependencies(test_qfatfilesystem_read_image generate_test_images)

# Add test target
add_test(TestQFATFileSystemReadImage test_qfatfilesystem_read_image)
target_link_libraries(test_qfatfilesystem_read_image ${test_libraries})

