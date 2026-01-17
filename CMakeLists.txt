cmake_minimum_required(VERSION 3.10)

# vcpkg integration - automatically uses vcpkg.json for dependencies
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Removed hardcoded compilers to allow CMake to detect the system default (e.g., clang on Mac, gcc on Linux)

project(SyncClientCpp VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
add_definitions(-DSQLITE_ORM_OMIT_CTE)

# Handle External Dependencies
# All libraries below are header-only and provided in the 'external' folder
include_directories(include)
include_directories(external)
include_directories(external/sqlite_orm_repo/include)
include_directories(external/efsw/include)

# Add efsw subdirectory
add_subdirectory(external/efsw EXCLUDE_FROM_ALL)

# Create a library for SQLite (C file)
add_library(sqlite3 STATIC external/sqlite3.c)

# Main executable (C++ files)
add_executable(sync_client 
    src/main.cpp
    src/DatabaseManager.cpp
    src/ReconciliationService.cpp
    src/ApiClient.cpp
    src/FilesystemWatcher.cpp
    src/FileSystemScanner.cpp
    src/SyncWorker.cpp
    src/CloudSyncWorker.cpp
  )

# Find libmagic from vcpkg
# Find libmagic from vcpkg
find_package(unofficial-libmagic REQUIRED)
add_definitions(-DHAVE_LIBMAGIC)
add_definitions(-DMAGIC_DB_PATH="${unofficial-libmagic_DICTIONARY}")
message(STATUS "libmagic found - using content-based MIME detection")

# Link Libraries
if(WIN32)
    # Required for cpp-httplib on Windows
    target_link_libraries(sync_client PRIVATE sqlite3 efsw-static ws2_32 crypt32 unofficial::libmagic::libmagic)
else()
    target_link_libraries(sync_client sqlite3 efsw-static pthread unofficial::libmagic::libmagic)
endif()

# Installation rules for packaging
install(TARGETS sync_client DESTINATION bin)

# Copy runtime dependencies (DLLs on Windows, .so on Linux, .dylib on macOS)
if(WIN32)
    # Copy libmagic DLL
    install(FILES $<TARGET_FILE:unofficial::libmagic::libmagic> DESTINATION bin)
    
    # Copy magic database files for libmagic
    install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/libmagic/
            DESTINATION bin/magic
            FILES_MATCHING PATTERN "*")
    
    # Copy other vcpkg DLLs if any
    install(DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/ 
            DESTINATION bin 
            FILES_MATCHING PATTERN "*.dll")
elseif(APPLE)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/
            DESTINATION SyncClientCpp.app/Contents/MacOS
            FILES_MATCHING PATTERN "*.dylib")
else()
    install(DIRECTORY ${CMAKE_BINARY_DIR}/
            DESTINATION bin
            FILES_MATCHING PATTERN "*.so*")
endif()
