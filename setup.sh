#!/usr/bin/env bash
# setup.sh — one-time setup for Nightshade JUCE plugin
# Run this from the Nightshade/ project root.

set -euo pipefail

echo "==> Checking for JUCE submodule..."

if [ ! -f "JUCE/CMakeLists.txt" ]; then
    echo "    JUCE not found. Adding as git submodule..."
    git init 2>/dev/null || true
    git submodule add https://github.com/juce-framework/JUCE.git JUCE
    git submodule update --init --recursive
else
    echo "    JUCE already present."
fi

echo "==> Configuring CMake (Xcode generator)..."
cmake -B build -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

echo ""
echo "Done. Open the Xcode project with:"
echo "  open build/Nightshade.xcodeproj"
echo ""
echo "Or build from the command line:"
echo "  cmake --build build --config Release"
