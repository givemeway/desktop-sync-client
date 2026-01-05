@echo off
if not exist build mkdir build
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"
set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"

%CMAKE_EXE% -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

%CMAKE_EXE% --build build
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\Debug\sync_client.exe
