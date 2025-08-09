# Quest Lookout

Quest Lookout is a utility for Oculus/Meta Quest users and Condor sim pilots to reinforce proper visual lookout habits during VR flight simulation. It provides customizable audio alarms to prompt users to perform lookout scans, ensuring good situational awareness and head movement habits.

## Features
- **Customizable visual lookout alarms**: Configure horizontal and vertical sweep angles, alarm intervals, and minimum lookout times via `settings.json`.
- **Audio prompts**: Plays configurable sound files (e.g., `lookout.ogg`, `beep.wav`) as reminders.
- **Automatic detection of VR session**: Monitors your Condor log file to activate only during flights.
- **Volume ramping and silence periods**: Prevents alarm fatigue by ramping volume and silencing after successful lookouts.
- **Easy configuration**: All settings are in a single JSON file; sample audio files are included.

## How to Use
1. **Install**: [Download the latest release ZIP here](https://github.com/ryanwoodie/Quest-Lookout/releases) and extract it to a folder on your PC. Make sure your Condor log file path is set up correctly in `settings.json`.
2. **Configure**: Edit `settings.json` to adjust alarm angles, intervals, and audio files as you like. See the sample settings.json for details.
3. **Run**: Launch `lookout.exe` before starting your VR flight sim session. The tray icon will indicate the app is running.
4. **Respond to prompts**: When you hear an alarm, perform a lookout scan to reset the alarm and build good lookout habits.

## Developer Notes: Compiling/Building
### Files
- `lookout.exe` – Main application (prebuilt, if available)
- `lookout.cpp`, `lookout.py` – Source code (C++ and Python versions)
- `settings.json` – User-editable configuration
- `lookout.ogg`, `notalentassclown.ogg` (use this if you really want to torture pilots into never missing a lookout), `beep.wav` – Sample audio files
- `sfml-audio-3.dll`, `sfml-system-3.dll` – Required SFML libraries
- `json.hpp` – Header for JSON parsing

### Prerequisites
- **Windows** (tested on Windows 10/11)
- **Oculus SDK** (OVR)
- **SFML 3.x** (for audio)
- **nlohmann/json** (`json.hpp` included)
- **Visual Studio** (recommended) or any C++17-compatible compiler

### Building
1. **Clone the repo** and ensure all dependencies are present.
2. **Install SFML** and ensure `sfml-audio-3.dll`, `sfml-system-3.dll` are in the build output or project root.
3. **Build the project** using Visual Studio or your preferred toolchain. Link against SFML and Oculus SDK as needed.
4. **Copy required DLLs and audio files** to the output directory if not handled by your build system.


---

For questions, bug reports, or contributions, please open an issue or pull request on GitHub.
