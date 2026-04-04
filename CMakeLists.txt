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
    src/SyncController.cpp
    src/ActivityModel.cpp
    src/ExplorerModel.cpp
    src/CloudBackupManager.cpp
    src/CloudFilesProvider.cpp
    qml/resources.qrc
    include/ApiClient.hpp
    include/CloudSyncWorker.hpp
    include/DatabaseManager.hpp
    include/FileSystemScanner.hpp
    include/FilesystemWatcher.hpp
    include/ReconciliationService.hpp
    include/SyncTree.hpp
    include/SyncWorker.hpp
    include/ThreadPool.hpp
    include/types.hpp
    include/SyncController.hpp
    include/MimeTypeDetector.hpp
    include/ActivityModel.hpp
    include/ExplorerModel.hpp
    include/CloudBackupManager.hpp
    include/CloudFilesProvider.hpp
  )

# Find libmagic from vcpkg
#find_package(unofficial-libmagic REQUIRED)
#add_definitions(-DHAVE_LIBMAGIC)
#add_definitions(-DMAGIC_DB_PATH="${unofficial-libmagic_DICTIONARY}")
#message(STATUS "libmagic found - using content-based MIME detection")

find_package(efsw CONFIG REQUIRED)
message(STATUS "EFSW found")

find_package(curl CONFIG REQUIRED)
message(STATUS "curl found")

find_package(OpenSSL REQUIRED)
message(STATUS "openssl found")

find_package(nlohmann_json CONFIG REQUIRED)
message(STATUS "nlohman-json found")

find_path(PICOSHA2_INCLUDES_DIRS "picosha2.h")

find_path(HTTPLIB_INCLUDES_DIRS "httplib.h")

find_package(unofficial-sqlite3 CONFIG REQUIRED) 
message(STATUS "sqlite3 found")

find_package(SqliteOrm CONFIG REQUIRED) 
message(STATUS "Sqliteorm found")

find_path(STB_INCLUDE_DIR "stb_image.h")

#set(LIBMAGIC_INCLUDE_DIR "C:/msys64/mingw64/include")
#set(LIBMAGIC_LIBRARY "C:/msys64/mingw64/lib/libmagic.dll.a")

#add_definitions(-DHAVE_LIBMAGIC)
#add_definitions(-DMAGIC_DB_PATH="C:/msys64/mingw64/share/misc/magic.mgc")

foreach(VAR PICOSHA2_INCLUDES_DIRS STB_INCLUDE_DIR HTTPLIB_INCLUDES_DIRS)
    if(NOT ${VAR}) 
         message(STATUS "${VAR} not found")
    else() 
        message(STATUS "${VAR} found: ${${VAR}}")
    endif()
endforeach()

# Ensure Qt path is searched first if provided in build script
if(DEFINED ENV{QT_PATH})
    set(CMAKE_PREFIX_PATH $ENV{QT_PATH} ${CMAKE_PREFIX_PATH})
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 QuickEffects)
message(STATUS "Qt6 found")

# Link Libraries
if(WIN32)
     # Define the Windows SDK path (Update the version number to match your folder!)
     set(WIN_SDK_INC "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0.0")
     set(WIN_SDK_LIB "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0.0")
      # Add headers to the project
      target_include_directories(sync_client PRIVATE 
      "${WIN_SDK_INC}/um"
      "${WIN_SDK_INC}/shared"
      )

    # Tell CMake where the .lib files are for MinGW to find
    #target_link_directories(sync_client PRIVATE "${WIN_SDK_LIB}/um/x64")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_link_libraries(sync_client PRIVATE
        ws2_32 
        crypt32 
        pthread 
        efsw::efsw 
        nlohmann_json::nlohmann_json 
        CURL::libcurl
        unofficial::sqlite3::sqlite3 
        sqlite_orm::sqlite_orm
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::QuickEffects
        OpenSSL::SSL OpenSSL::Crypto
        cldapi
        #        ${LIBMAGIC_LIBRARY}      
        )
    else()
      target_link_libraries(sync_client PRIVATE 
        ws2_32 
        crypt32 
        efsw::efsw 
        nlohmann_json::nlohmann_json 
        CURL::libcurl
        unofficial::sqlite3::sqlite3 
        sqlite_orm::sqlite_orm
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::QuickEffects
        OpenSSL::SSL OpenSSL::Crypto
        cldapi
        #${LIBMAGIC_LIBRARY}
    )
    endif()
else()
  target_link_libraries(sync_client 
    pthread 
    efsw::efsw 
    nlohmann_json::nlohmann_json 
    CURL::libcurl
    unofficial::sqlite3::sqlite3 
    sqlite_orm::sqlite_orm
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::QuickEffects
    OpenSSL::SSL OpenSSL::Crypto
    cldapi
)
endif()

# picosha2 and stb are header-only, include their directories instead of linking
target_include_directories(sync_client PRIVATE 
    ${PICOSHA2_INCLUDES_DIRS}
    ${STB_INCLUDE_DIR}
)

#target_include_directories(
#  sync_client PRIVATE ${LIBMAGIC_INCLUDE_DIR}
#)

# Installation rules for packaging
install(TARGETS sync_client DESTINATION bin)

# Create qt.conf so Qt can locate QML and plugin dirs relative to the exe
file(WRITE "${CMAKE_BINARY_DIR}/qt.conf"
"[Paths]
Prefix = .
Binaries = .
Libraries = .
Plugins = .
Qml2Imports = qml
")

install(FILES "${CMAKE_BINARY_DIR}/qt.conf" DESTINATION bin)
# Copy runtime dependencies (DLLs on Windows, .so on Linux, .dylib on macOS)
if(WIN32)
    # 1. Copy libmagic DLL
    #install(FILES $<TARGET_FILE:unofficial::libmagic::libmagic> DESTINATION bin)

    
    # 2. Run windeployqt for Qt dependencies
    set(WINDEPLOYQT_EXECUTABLE "C:/Qt/6.10.3/msvc2022_64/bin/windeployqt.exe")
    #get_target_property(_qt_bin_dir Qt6::Core LOCATION)
    #get_filename_component(_qt_bin_dir "${_qt_bin_dir}" DIRECTORY)
    #find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET sync_client POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-compiler-runtime
                    --no-opengl-sw
                    --no-translations
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:sync_client>"
            COMMENT "Deploying Qt dependencies with windeployqt..."
        )
    endif()

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
