@echo off
:: Bobcat Engine Firewall Setup
:: This script must be run as Administrator

echo ========================================
echo  Bobcat Engine Firewall Configuration
echo ========================================
echo.

:: Check for admin privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo Please right-click and select "Run as administrator"
    echo.
    pause
    exit /b 1
)

echo Running firewall configuration...
echo.

powershell -ExecutionPolicy Bypass -File "%~dp0bobcat_firewall.ps1"

echo.
echo ========================================
echo  Done!
echo ========================================
pause
