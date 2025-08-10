# Quest Lookout VR Safety Monitor - Setup Guide

## Quick Start

1. **First Time Setup**: Run `setup.bat` to create your initial settings
2. **Configure Settings**: Run `settings_manager.bat` to customize alarms  
3. **Start Monitoring**: Run `lookout.exe` when flying in VR

## Files Included

| File | Purpose |
|------|---------|
| `lookout.exe` | Main VR safety monitor program |
| `settings_manager.bat` | GUI configuration tool launcher |
| `settings_gui.py` | Python GUI settings editor |
| `setup.bat` | First-time setup helper |
| `settings_default.json` | Default configuration template |
| `settings.json` | Your active configuration (created on first run) |
| Audio files (`.ogg`, `.wav`) | Alarm sound files |

## How It Works

Quest Lookout monitors your VR headset movements during flight simulation and provides audio warnings when you haven't performed proper traffic scanning patterns. This helps prevent spatial disorientation and promotes safe flying habits in VR.

### Features
- **Multiple Alarm Levels**: Configure different sensitivity levels
- **Automatic Flight Detection**: Only active when Condor is running
- **Customizable Audio**: Use your own alarm sounds
- **Center Reset**: Look forward to reset scan progress
- **System Tray Operation**: Runs quietly in background

## Configuration

### Using the GUI (Recommended)
1. Double-click `settings_manager.bat` 
2. Add/edit alarms with different requirements
3. Adjust center reset sensitivity
4. Set your Condor log file path
5. Save settings and restart Quest Lookout

### Manual Configuration
Edit `settings.json` directly for advanced customization. See the built-in `_instructions` section for parameter details.

## Alarm Parameters Explained

- **Horizontal Angle**: Total left+right scan required (e.g., 45° = 22.5° each way)
- **Vertical Angles**: Up/down scan requirements (0 = disabled)
- **Max Time**: How long before alarm triggers (milliseconds)
- **Audio Settings**: Custom sound files and volume control
- **Timing**: Repeat intervals and silence periods

## Troubleshooting

**Quest Lookout won't start:**
- Ensure Oculus software is installed
- Check that `settings.json` exists and is valid JSON

**No alarms when flying:**
- Verify Condor log path in settings
- Make sure Condor is actively running a simulation
- Check that alarm requirements aren't too strict

**GUI won't open:**
- Ensure Python 3 is installed with tkinter support
- Run `python settings_gui.py` directly to see error messages

## Support

For issues or questions, check the project repository or documentation.

**Happy and safe VR flying! ✈️**