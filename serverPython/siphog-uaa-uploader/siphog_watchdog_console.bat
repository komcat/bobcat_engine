@echo off
rem Launch the SIPHOG UAA Watchdog with a console window attached
rem so Python stack traces are visible. Use for troubleshooting.

cd /d "%~dp0"
python siphog_watchdog.py
pause
