#!/bin/bash
# Quick Test Server for ESP32-S3 Web Flasher
# Run this script to test the web flasher locally

echo "========================================="
echo "ESP32-S3 AI Camera Web Flasher"
echo "Local Test Server"
echo "========================================="
echo ""

# Check if Python is installed
if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
    echo "❌ Error: Python not found!"
    echo "Please install Python to run the test server."
    exit 1
fi

# Determine Python command
if command -v python3 &> /dev/null; then
    PYTHON_CMD="python3"
else
    PYTHON_CMD="python"
fi

echo "✅ Python found: $PYTHON_CMD"
echo ""

# Check if firmware binaries exist
if [ ! -f "firmware/bootloader.bin" ]; then
    echo "⚠️  Warning: firmware/bootloader.bin not found"
    echo "You need to build the binary files first!"
    echo "See BUILD_GUIDE.md for instructions."
    echo ""
fi

# Start server
echo "🚀 Starting test server..."
echo ""
echo "================================================"
echo "  Server running at: http://localhost:8000"
echo "================================================"
echo ""
echo "📝 Instructions:"
echo "  1. Open http://localhost:8000 in Chrome/Edge/Opera"
echo "  2. Connect your ESP32-S3 via USB-C"
echo "  3. Click 'Install AI Camera Firmware'"
echo "  4. Select your device and flash"
echo ""
echo "Press Ctrl+C to stop the server"
echo ""

# Run Python HTTP server
$PYTHON_CMD -m http.server 8000
