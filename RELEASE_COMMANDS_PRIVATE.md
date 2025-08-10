# Private Release Commands for Quest Lookout

These are the CLI commands you need to create a GitHub release from your local machine. This file is ignored by git and is for your personal use only.

---

## 1. Authenticate GitHub CLI (first time only)
gh auth login

## 2. Build the GUI executable first
python create_exe.py

## 3. Package the release files (update filenames/versions as needed)
powershell Compress-Archive -Path lookout.exe,settings_gui.exe,settings.json,settings_default.json,settings_manager.bat,setup.bat,lookout.ogg,notalentassclown.ogg,beep.wav,sfml-audio-3.dll,sfml-system-3.dll,README.md -DestinationPath QuestLookout-v2.0.zip

## 4. Create the GitHub release (creates tag if missing)
gh release create v2.0 QuestLookout-v2.0.zip --title "Quest Lookout v2.0 - GUI Tools & Performance" --notes "Major update with GUI configuration tool, performance improvements, and enhanced safety features"

---

- Omit <commit-sha> unless you want to release a specific commit (not HEAD).
- You can update the version/tag and file list as needed for future releases.
- This file is for your reference and should not be committed to the repo.


call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cl /EHsc /Zi /MD /std:c++17 /Fe:lookout.exe lookout.cpp /I. /I"SFML-3.0.0/include" /I"C:\OculusSDK\LibOVR\Include" /link /SUBSYSTEM:WINDOWS /LIBPATH:"C:\OculusSDK\LibOVR\Lib\Windows\x64\Release\VS2017" SFML-3.0.0/lib/sfml-audio.lib SFML-3.0.0/lib/sfml-system.lib LibOVR.lib user32.lib shell32.lib winmm.lib kernel32.lib gdi32.lib comctl32.lib Shcore.lib