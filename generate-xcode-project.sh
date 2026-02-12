#!/bin/bash

# Quester Xcode Project Generator Script
# This script generates an Xcode project for Quester

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Quester Xcode Project Generator${NC}"
echo "=================================="

# Check if Homebrew is installed
if ! command -v brew &> /dev/null; then
    echo -e "${RED}Error: Homebrew is not installed.${NC}"
    echo "Please install Homebrew first: https://brew.sh/"
    exit 1
fi

# Check if dependencies are installed
echo -e "${YELLOW}Checking dependencies...${NC}"
if ! command -v cmake &> /dev/null; then
    echo -e "${YELLOW}Installing CMake...${NC}"
    brew install cmake
fi

if ! command -v pkg-config &> /dev/null; then
    echo -e "${YELLOW}Installing pkg-config...${NC}"
    brew install pkg-config
fi

# Check if Qt6 is installed
if ! brew list qt6 &> /dev/null; then
    echo -e "${YELLOW}Installing Qt6...${NC}"
    brew install qt6
fi

# Check if other dependencies are installed
for dep in libmpdclient fftw; do
    if ! brew list $dep &> /dev/null; then
        echo -e "${YELLOW}Installing $dep...${NC}"
        brew install $dep
    fi
done

# Check if submodules are initialized
if [ ! -f "libprojectm/CMakeLists.txt" ]; then
    echo -e "${YELLOW}Initializing submodules...${NC}"
    git submodule update --init --recursive
fi

# Create build directory for Xcode
echo -e "${YELLOW}Creating Xcode build directory...${NC}"
mkdir -p build-xcode
cd build-xcode

# Generate Xcode project
echo -e "${YELLOW}Generating Xcode project...${NC}"
cmake .. -G Xcode \
    -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) \
    -DCMAKE_BUILD_TYPE=Debug

echo -e "${GREEN}Xcode project generated successfully!${NC}"
echo "=================================="
echo "Xcode project location: build-xcode/Quester.xcodeproj"
echo ""
echo "To open the project in Xcode:"
echo "  open build-xcode/Quester.xcodeproj"
echo ""
echo "To build from the command line:"
echo "  xcodebuild -project build-xcode/Quester.xcodeproj -scheme quester -configuration Debug build"