@echo off

REM GCC PATH - uncomment ONE line matching your MinGW install
set "PATH=C:\mingw64\bin;%PATH%"

set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"

if exist build rmdir /s /q build
mkdir build

%CMAKE_EXE% -S . -B build ^
  -G "MinGW Makefiles"  ^
  -DCMAKE_MAKE_PROGRAM=C:\mingw64\bin\mingw32-make.exe ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DCMAKE_C_COMPILER=gcc ^
  -DCMAKE_CXX_COMPILER=g++ ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic

if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

REM Second configure not needed anymore
REM %CMAKE_EXE% -S . -B build

%CMAKE_EXE% --build build --config Debug
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\sync_client.exe
