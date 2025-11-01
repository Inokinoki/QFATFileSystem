find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

set(test_libraries QFATFS Qt${QT_VERSION_MAJOR}::Test)

# Copy test files to build directory
file(COPY data/fat16.img DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY data/fat32.img DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

# Add test executable
add_executable(test_qfatfilesystem_read_image test_qfatfilesystem_read_image.cpp)

# Add test target
add_test(TestQFATFileSystemReadImage test_qfatfilesystem_read_image)
target_link_libraries(test_qfatfilesystem_read_image ${test_libraries})

