@echo off

set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"
set PATH=C:\mingw64\bin;%PATH%

if exist build_release rmdir /s /q build_release
mkdir build_release

%CMAKE_EXE% -S . -B build_release ^
  -G "MinGW Makefiles" ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" ^
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" ^
  -DCMAKE_C_FLAGS="--target=x86_64-w64-windows-gnu -O3 -s" ^
  -DCMAKE_CXX_FLAGS="--target=x86_64-w64-windows-gnu -O3 -s" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static

if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

%CMAKE_EXE% --build build_release --config Release
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Successful!
ls -lh build_release/sync_client.exe
