#!/usr/bin/env python3
"""
Script to create a standalone executable from the GUI using PyInstaller
Run this to build settings_gui.exe for distribution
"""

import os
import subprocess
import sys

def create_executable():
    """Create standalone executable using PyInstaller"""
    
    print("Creating standalone executable for Quest Lookout Settings GUI...")
    print("This will bundle Python and all dependencies into a single .exe file")
    print()
    
    # Check if PyInstaller is available
    try:
        import PyInstaller
        print("* PyInstaller found")
    except ImportError:
        print("Installing PyInstaller...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pyinstaller"])
        print("* PyInstaller installed")
    
    # PyInstaller command
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile",  # Single executable file
        "--windowed",  # No console window
        "--name=settings_gui",  # Output name
        "--icon=NONE",  # No icon (could add later)
        "--add-data=settings_default.json;.",  # Include default settings template
        "--distpath=.",  # Output to current directory
        "settings_gui.py"
    ]
    
    print("Running PyInstaller...")
    print(" ".join(cmd))
    print()
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print("* Executable created successfully!")
        print()
        
        if os.path.exists("settings_gui.exe"):
            size = os.path.getsize("settings_gui.exe") / (1024*1024)  # MB
            print(f"* settings_gui.exe created ({size:.1f} MB)")
            print()
            print("The executable includes:")
            print("- Python interpreter")
            print("- All required libraries (tkinter, json)")
            print("- settings_default.json template")
            print()
            print("Users can now run settings_gui.exe without installing Python!")
        else:
            print("✗ Executable not found after build")
            
    except subprocess.CalledProcessError as e:
        print(f"✗ Build failed: {e}")
        print("STDOUT:", e.stdout)
        print("STDERR:", e.stderr)
        return False
    
    # Cleanup build files
    print("Cleaning up build files...")
    import shutil
    if os.path.exists("build"):
        shutil.rmtree("build")
        print("✓ Removed build directory")
    if os.path.exists("settings_gui.spec"):
        os.remove("settings_gui.spec")
        print("✓ Removed spec file")
    
    return True

if __name__ == "__main__":
    success = create_executable()
    if success:
        print("\n" + "="*50)
        print("SUCCESS! Standalone executable ready for distribution")
        print("Users no longer need Python installed")
        print("="*50)
    else:
        print("\n" + "="*50)
        print("BUILD FAILED - check errors above")
        print("="*50)
    
    input("Press Enter to continue...")