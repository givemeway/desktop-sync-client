cmake_minimum_required(VERSION 3.10)

# Set compilers before project() call
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

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
)

# Link Libraries
if(WIN32)
    # Required for cpp-httplib on Windows
    target_link_libraries(sync_client sqlite3 efsw-static ws2_32 crypt32)
else()
    target_link_libraries(sync_client sqlite3 efsw-static pthread)
endif()
