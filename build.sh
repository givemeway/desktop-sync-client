#!/bin/bash

# Create build directory if it doesn't exist
mkdir -p build

# Configure the project only if CMakeCache.txt doesn't exist or CMakeLists.txt is newer
# This mimics the behavior where you don't re-run configuration unless needed.
if [ ! -f build/CMakeCache.txt ] || [ CMakeLists.txt -nt build/CMakeCache.txt ]; then
    echo "Configuring project..."
    cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    # Check if configuration was successful
    if [ $? -ne 0 ]; then
        echo "Configuration failed!"
        exit 1
    fi
fi

# Build the project using all available cores for speed
echo "Building project..."
cmake --build build --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# Check if build was successful
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build Successful!"

# Run the executable
if [ -f ./build/sync_client ]; then
    ./build/sync_client
else
    echo "Executable build/sync_client not found!"
    exit 1
fi
