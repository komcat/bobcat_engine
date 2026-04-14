@echo off
rem Launch the SIPHOG UAA Watchdog tray app (no console window).
rem Use siphog_watchdog_console.bat if you need to see Python errors.

cd /d "%~dp0"
start "" pythonw.exe siphog_watchdog.py
