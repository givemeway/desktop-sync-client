@echo off

set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"

set "VCPKG_ROOT=F:\vcpkg"
set "QT_PATH=F:\Qt\6.10.2\llvm-mingw_64"

:: Add the Qt provided LLVM/MinGW toolchain to path
set "LLVM_PATH=C:\Program Files\LLVM\bin"
set PATH=%LLVM_PATH%;%PATH%

if exist build rmdir /s /q build
mkdir build

%CMAKE_EXE% -S . -B build ^
  -G "MinGW Makefiles" ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_PREFIX_PATH="%QT_PATH%" ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DCMAKE_C_COMPILER="%LLVM_PATH%\clang.exe" ^
  -DCMAKE_CXX_COMPILER="%LLVM_PATH%\clang++.exe" ^
  -DCMAKE_C_FLAGS="--target=x86_64-w64-windows-gnu" ^
  -DCMAKE_CXX_FLAGS="--target=x86_64-w64-windows-gnu" ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static

if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

%CMAKE_EXE% --build build --config Debug
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
.\build\sync_client.exe
