@echo off

REM GCC PATH - uncomment ONE line matching your MinGW install
REM set "PATH=C:\mingw64\bin;%PATH%"

set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"

if exist build rmdir /s /q build
mkdir build

%CMAKE_EXE% -S . -B build ^
  -G "MinGW Makefiles"  ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DCMAKE_C_COMPILER=gcc ^
  -DCMAKE_CXX_COMPILER=g++

if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

REM Second configure not needed anymore
REM %CMAKE_EXE% -S . -B build

%CMAKE_EXE% --build build
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\sync_client.exe
