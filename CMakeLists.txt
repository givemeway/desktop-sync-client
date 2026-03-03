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
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
add_definitions(-DSQLITE_ORM_OMIT_CTE)


if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options("-Wa,-mbig-obj")
endif()

# Handle External Dependencies
# All libraries below are header-only and provided in the 'external' folder
include_directories(include)
include_directories(external)
#include_directories(external/sqlite_orm_repo/include)
#include_directories(external/efsw/include)

# Add efsw subdirectory
#add_subdirectory(external/efsw EXCLUDE_FROM_ALL)

# Create a library for SQLite (C file)
#add_library(sqlite3 STATIC external/sqlite3.c)

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
    src/Utility.cpp
    src/SyncTree.cpp
    include/SyncController.hpp
    src/SyncController.cpp
    src/resources.qrc
  )

# Find libmagic from vcpkg
find_package(unofficial-libmagic REQUIRED)
add_definitions(-DHAVE_LIBMAGIC)
add_definitions(-DMAGIC_DB_PATH="${unofficial-libmagic_DICTIONARY}")
message(STATUS "libmagic found - using content-based MIME detection")

#Find  OpenCV from VCPKG 
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV ${OpenCV_VERSION} found")

#Find efsw
find_package(efsw CONFIG REQUIRED)
message(STATUS "EFSW found")

find_package(nlohmann_json CONFIG REQUIRED)
message(STATUS "nlohman-json found")

find_path(PICOSHA2_INCLUDES_DIRS "picosha2.h")

find_package(unofficial-sqlite3 CONFIG REQUIRED) 
message(STATUS "sqlite3 found")

find_package(SqliteOrm CONFIG REQUIRED) 
message(STATUS "Sqliteorm found")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Controls2)
message(STATUS "Qt6 found")


# Link Libraries
if(WIN32)
    # Required for cpp-httplib on Windows
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_link_libraries(sync_client PRIVATE
        ws2_32 
        crypt32 
        unofficial::libmagic::libmagic 
        ${OpenCV_LIBS} 
        pthread 
        efsw::efsw 
        nlohmann_json::nlohmann_json 
        unofficial::sqlite3::sqlite3 
        sqlite_orm::sqlite_orm
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
      )
    else()
      target_link_libraries(sync_client PRIVATE 
        ws2_32 
        crypt32 
        unofficial::libmagic::libmagic 
        ${OpenCV_LIBS} 
        efsw::efsw 
        nlohmann_json::nlohmann_json 
        unofficial::sqlite3::sqlite3 
        sqlite_orm::sqlite_orm
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
      )
    endif()
else()
  target_link_libraries(sync_client 
    pthread 
    unofficial::libmagic::libmagic 
    ${OpenCV_LIBS} 
    efsw::efsw 
    nlohmann_json::nlohmann_json 
    unofficial::sqlite3::sqlite3 
    sqlite_orm::sqlite_orm
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
  )
endif()

# picosha2 is header-only, include its directory instead of linking
target_include_directories(sync_client PRIVATE ${PICOSHA2_INCLUDES_DIRS})

# Installation rules for packaging
install(TARGETS sync_client DESTINATION bin)

# Copy runtime dependencies (DLLs on Windows, .so on Linux, .dylib on macOS)
if(WIN32)
    # Copy libmagic DLL
    install(FILES $<TARGET_FILE:unofficial::libmagic::libmagic> DESTINATION bin)
   
    # Copy OpenCV DLLs 
    if(OpenCV_RUNTIME_LIBRARIES)
        install(FILES ${OpenCV_RUNTIME_LIBRARIES} DESTINATION bin)
    endif()

    # Copy magic database files for libmagic
    install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/libmagic/
            DESTINATION bin/magic
            FILES_MATCHING PATTERN "*")
    
    # Copy MinGW runtime DLLs
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        
        set(MINGW_DLLS 
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
        )
        
        foreach(DLL ${MINGW_DLLS})
            if(EXISTS ${DLL})
                # Copy for development (build folder)
                add_custom_command(TARGET sync_client POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${DLL}
                    $<TARGET_FILE_DIR:sync_client>
                )
                # Mark for installation
                install(FILES ${DLL} DESTINATION bin)
            endif()
        endforeach()
    endif()

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
