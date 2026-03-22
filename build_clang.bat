@echo off
if exist build rmdir /s /q build
mkdir build
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"

set "VCPKG_ROOT=F:\vcpkg"
set "PATH=%VCPKG_ROOT%;%PATH%"

if not exist "%VCPKG_ROOT%" (
    echo ERROR: VCPKG_ROOT is not set correctly: "%VCPKG_ROOT%"
    exit /b 1
)

set "VCPKG_DOWNLOADS=%VCPKG_ROOT%\downloads"
if not exist "%VCPKG_DOWNLOADS%" mkdir "%VCPKG_DOWNLOADS%"

echo Debug: Using VCPKG_ROOT: %VCPKG_ROOT%
echo Debug: Using VCPKG_DOWNLOADS: %VCPKG_DOWNLOADS%

set "QT_PATH=F:\Qt\6.10.2\msvc2022_64"

%CMAKE_EXE% -S . -B build ^
    -T ClangCL ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DCMAKE_PREFIX_PATH="%QT_PATH%" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

%CMAKE_EXE% --build build
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\Debug\sync_client.exe
