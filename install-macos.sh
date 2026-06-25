#!/bin/bash

# Quester macOS Installation Script
# This script installs dependencies using Homebrew and builds the project

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Quester macOS Installation Script${NC}"
echo "=================================="

# Check if Homebrew is installed
if ! command -v brew &> /dev/null; then
    echo -e "${RED}Error: Homebrew is not installed.${NC}"
    echo "Please install Homebrew first: https://brew.sh/"
    exit 1
fi

# Update Homebrew
echo -e "${YELLOW}Updating Homebrew...${NC}"
brew update

# Install dependencies
echo -e "${YELLOW}Installing dependencies...${NC}"
brew install cmake qt6 libmpdclient fftw pkg-config

# Check if submodules are initialized
if [ ! -f "libprojectm/CMakeLists.txt" ]; then
    echo -e "${YELLOW}Initializing submodules...${NC}"
    git submodule update --init --recursive
fi

# Create build directory
echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure CMake
echo -e "${YELLOW}Configuring CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) \
    -DCMAKE_INSTALL_PREFIX=./install

# Build the project
echo -e "${YELLOW}Building Quester...${NC}"
cmake --build . --config Release -j$(sysctl -n hw.ncpu)

# Install the application
echo -e "${YELLOW}Installing Quester...${NC}"
cmake --install .

# Create a DMG (optional)
echo -e "${YELLOW}Creating DMG installer (optional)...${NC}"
if command -v create-dmg &> /dev/null; then
    create-dmg \
        --volname "Quester Installer" \
        --window-pos 200 120 \
        --window-size 800 400 \
        --icon-size 100 \
        --icon "Quester.app" 200 190 \
        --hide-extension "Quester.app" \
        --app-drop-link 600 185 \
        "Quester.dmg" \
        "install/"
else
    echo -e "${YELLOW}create-dmg not installed. Skipping DMG creation.${NC}"
    echo "To install create-dmg: brew install create-dmg"
fi

cd ..

echo -e "${GREEN}Installation complete!${NC}"
echo "=================================="
echo "Quester is installed in: build/install/Quester.app"
echo ""
echo "To run Quester:"
echo "  open build/install/Quester.app"
echo ""
echo "To install Quester to /Applications:"
echo "  cp -R build/install/Quester.app /Applications/"