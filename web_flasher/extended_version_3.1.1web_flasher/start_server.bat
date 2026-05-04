@echo off
REM Quick Test Server for ESP32-S3 Web Flasher (Windows)
REM Run this script to test the web flasher locally

echo =========================================
echo ESP32-S3 AI Camera Web Flasher
echo Local Test Server
echo =========================================
echo.

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo Error: Python not found!
    echo Please install Python from python.org
    pause
    exit /b 1
)

echo Python found
echo.

REM Check if firmware binaries exist
if not exist "firmware\bootloader.bin" (
    echo Warning: firmware\bootloader.bin not found
    echo You need to build the binary files first!
    echo See BUILD_GUIDE.md for instructions.
    echo.
)

REM Start server
echo Starting test server...
echo.
echo ================================================
echo   Server running at: http://localhost:8000
echo ================================================
echo.
echo Instructions:
echo   1. Open http://localhost:8000 in Chrome/Edge/Opera
echo   2. Connect your ESP32-S3 via USB-C
echo   3. Click 'Install AI Camera Firmware'
echo   4. Select your device and flash
echo.
echo Press Ctrl+C to stop the server
echo.

python -m http.server 8000
