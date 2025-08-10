# Quest Lookout VR Safety Monitor

**Quest Lookout** is a VR safety tool for flight simulation that promotes proper traffic scanning habits during VR flights. It monitors your head movements through your Oculus/Meta Quest headset and provides audio reminders when you haven't performed adequate lookout patterns.

## 🎯 Why Use Quest Lookout?

VR flight simulation can cause **reduced situational awareness** compared to real flying. Quest Lookout helps maintain:
- ✈️ **Proper traffic scanning habits** - Regular left/right/up lookout patterns
- 🚨 **Safety awareness** - Audio alerts when you focus too long in one direction  
- 🎮 **Real-world skills transfer** - Build habits that work in actual aircraft
- 👀 **Visual discipline** - Prevents tunnel vision common in VR

## 🚀 Quick Start

### Step 1: Download & Setup
1. **Download** the [latest release](https://github.com/ryanwoodie/Quest-Lookout/releases)
2. **Extract** to any folder on your PC
3. **Run `setup.bat`** for guided first-time configuration

### Step 2: Configure Your Alarms  
1. **Launch** `settings_manager.bat` to open the configuration GUI
2. **Add/edit alarms** with different sensitivity levels:
   - **Gentle reminder**: 45° lookout every 30 seconds
   - **Comprehensive scan**: 120° lookout every 90 seconds  
3. **Customize audio files** and volume settings
4. **Save settings** and close the GUI

### Step 3: Start Flying Safely
1. **Launch Quest Lookout**: Run `lookout.exe` 
2. **Check system tray**: Look for the Quest Lookout icon
3. **Start Condor**: Launch your flight simulation
4. **Fly with awareness**: Perform lookouts when prompted

## ⚙️ How It Works

- **Automatic Activation**: Only monitors during active Condor flights
- **Head Tracking**: Uses Oculus SDK to monitor your actual head position
- **Smart Alerts**: When you start looking around, alarms pause to let you complete the scan
- **Multiple Sensitivity Levels**: Configure different alarms for various flight scenarios
- **Background Operation**: Runs silently in system tray

## 🔧 Configuration

**Easy GUI Setup** (Recommended):
- Run `settings_manager.bat` for point-and-click configuration
- Hover over any setting for detailed tooltips
- Add/remove/duplicate alarms as needed

**Manual Setup** (Advanced):
- Edit `settings.json` directly for fine-tuned control
- Full documentation included in the file

## 📁 What's Included

| File | Purpose |
|------|---------|
| `lookout.exe` | Main VR safety monitor |
| `settings_gui.exe` | Configuration tool (no Python required) |
| `settings_manager.bat` | Easy launcher for GUI |
| `setup.bat` | First-time setup assistant |
| `settings_default.json` | Default configuration template |
| Audio files (`.ogg`, `.wav`) | Alarm sound files |

## 🆘 Troubleshooting

**Quest Lookout won't start:**
- Ensure Oculus/Meta software is installed and running
- Check that `settings.json` exists (run `setup.bat` if missing)

**No alarms during flight:**
- Verify Condor log path in settings matches your installation
- Make sure Condor is actually running a simulation (not just menu)
- Check alarm requirements aren't too restrictive

**Can't configure settings:**
- Run `settings_manager.bat` to launch the GUI
- For manual editing, ensure `settings.json` is valid JSON format

---

## 🛠️ For Developers

### Building from Source

**Prerequisites:**
- Windows 10/11
- Visual Studio 2019+ with C++17 support
- Oculus SDK
- SFML 3.x
- Python 3.7+ (for GUI tool)

**Core Application (C++):**
```bash
# Build main VR monitor
build_improved.bat
```

**GUI Configuration Tool (Python):**  
```bash
# Build standalone GUI executable
python create_exe.py
```

**Project Structure:**
- `lookout.cpp` - Main VR monitoring application
- `settings_gui.py` - Configuration GUI (builds to .exe)
- `SFML-3.0.0/` - Audio library (include + lib files)
- `json.hpp` - JSON parsing library

### Contributing

1. **Fork** the repository
2. **Create** a feature branch
3. **Test** with actual VR flight sessions  
4. **Submit** a pull request

---

**Questions or bug reports?** Open an issue on GitHub.