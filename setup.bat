@echo off
title Quest Lookout Setup
echo.
echo =========================================
echo    Quest Lookout VR Safety Monitor
echo           First-Time Setup
echo =========================================
echo.

REM Check if settings.json exists
if exist settings.json (
    echo Found existing settings.json
    echo Would you like to keep your current settings? [Y/N]
    set /p keep_settings=
    if /i "%keep_settings%"=="N" goto copy_defaults
    echo Keeping existing settings.
    goto setup_complete
)

:copy_defaults
echo Creating default settings.json from template...
if exist settings_default.json (
    copy settings_default.json settings.json >nul
    echo ✓ Default settings.json created successfully!
) else (
    echo ✗ Error: settings_default.json template not found!
    echo   Please ensure all Quest Lookout files are present.
    pause
    exit /b 1
)

:setup_complete
echo.
echo Setup complete! You now have:
echo.
echo ✓ settings.json - Main configuration file
echo ✓ settings_manager.bat - GUI configuration tool
echo ✓ lookout.exe - Main VR safety monitor
echo.
echo Next steps:
echo 1. Run settings_manager.bat to customize your alarm settings
echo 2. Start lookout.exe when using VR in Condor
echo 3. Check system tray for the Quest Lookout icon
echo.
echo Would you like to open the settings manager now? [Y/N]
set /p open_gui=
if /i "%open_gui%"=="Y" (
    echo Starting settings manager...
    start settings_manager.bat
)

echo.
echo Setup complete! Enjoy safe VR flying!
pause