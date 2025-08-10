@echo off
title Quest Lookout Settings Manager
echo Starting Quest Lookout Settings Manager...
python settings_gui.py
if errorlevel 1 (
    echo.
    echo Error: Python not found or GUI failed to start
    echo Make sure Python 3 is installed and tkinter is available
    pause
)