@echo off
setlocal
cd /d "%~dp0"

where pythonw >nul 2>&1
if %ERRORLEVEL%==0 (
    start "" pythonw "%~dp0view_serial_data.py" %*
) else (
    where python >nul 2>&1
    if %ERRORLEVEL%==0 (
        start "" python "%~dp0view_serial_data.py" %*
    ) else (
        echo Python was not found on PATH. Install Python 3 or adjust PATH.
        pause
        exit /b 1
    )
)

endlocal
