@echo off

set CMAKE_EXE="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if exist build rmdir /s /q build
mkdir build

%CMAKE_EXE% -S . -B build ^
  -G "MinGW Makefiles" ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DCMAKE_BUILD_TYPE=Debug

if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

%CMAKE_EXE% --build build --config Debug
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\Debug\sync_client.exe