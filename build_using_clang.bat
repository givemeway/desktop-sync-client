@echo off

set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"

if exist build rmdir /s /q build
mkdir build

%CMAKE_EXE% -S . -B build ^
  -G "MinGW Makefiles" ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DCMAKE_C_COMPILER=clang ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_C_FLAGS="--target=x86_64-w64-windows-gnu" ^
  -DCMAKE_CXX_FLAGS="--target=x86_64-w64-windows-gnu" ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static

if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

%CMAKE_EXE% --build build --config Debug
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\sync_client.exe
