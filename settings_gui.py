#!/usr/bin/env python3
"""
Quest Lookout Settings GUI
Simple GUI configuration tool for Quest Lookout VR Safety Monitor
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import json
import os

class ToolTip:
    """Simple tooltip implementation for tkinter widgets"""
    def __init__(self, widget, text='widget info'):
        self.widget = widget
        self.text = text
        self.widget.bind("<Enter>", self.enter)
        self.widget.bind("<Leave>", self.leave)
        self.tipwindow = None
        
    def enter(self, event=None):
        self.showtip()
        
    def leave(self, event=None):
        self.hidetip()
        
    def showtip(self):
        if self.tipwindow or not self.text:
            return
        x, y, cx, cy = self.widget.bbox("insert") if hasattr(self.widget, 'bbox') else (0, 0, 0, 0)
        x = x + self.widget.winfo_rootx() + 25
        y = y + cy + self.widget.winfo_rooty() + 25
        self.tipwindow = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(1)
        tw.wm_geometry("+%d+%d" % (x, y))
        label = tk.Label(tw, text=self.text, justify=tk.LEFT,
                        background="#ffffe0", relief=tk.SOLID, borderwidth=1,
                        font=("Arial", "9", "normal"), wraplength=300)
        label.pack(ipadx=1)
        
    def hidetip(self):
        tw = self.tipwindow
        self.tipwindow = None
        if tw:
            tw.destroy()

def add_tooltip(widget, text):
    """Helper function to add tooltip to a widget"""
    ToolTip(widget, text)

class AlarmConfig:
    def __init__(self, data=None):
        if data is None:
            data = {}
        self.min_horizontal_angle = data.get('min_horizontal_angle', 45.0)
        self.min_vertical_angle_up = data.get('min_vertical_angle_up', 7.5)
        self.min_vertical_angle_down = data.get('min_vertical_angle_down', 0.0)
        self.max_time_ms = data.get('max_time_ms', 30000)
        self.audio_file = data.get('audio_file', 'lookout.ogg')
        self.start_volume = data.get('start_volume', 50)
        self.end_volume = data.get('end_volume', 100)
        self.volume_ramp_time_ms = data.get('volume_ramp_time_ms', 30000)
        self.repeat_interval_ms = data.get('repeat_interval_ms', 5000)
        self.silence_after_look_ms = data.get('silence_after_look_ms', 5000)
        self.min_lookout_time_ms = data.get('min_lookout_time_ms', 2000)
    
    def to_dict(self):
        return {
            'min_horizontal_angle': self.min_horizontal_angle,
            'min_vertical_angle_up': self.min_vertical_angle_up,
            'min_vertical_angle_down': self.min_vertical_angle_down,
            'max_time_ms': self.max_time_ms,
            'audio_file': self.audio_file,
            'start_volume': self.start_volume,
            'end_volume': self.end_volume,
            'volume_ramp_time_ms': self.volume_ramp_time_ms,
            'repeat_interval_ms': self.repeat_interval_ms,
            'silence_after_look_ms': self.silence_after_look_ms,
            'min_lookout_time_ms': self.min_lookout_time_ms
        }
    
    def __str__(self):
        return f"{self.min_horizontal_angle}°H, {self.min_vertical_angle_up}°U, {self.min_vertical_angle_down}°D, {self.max_time_ms//1000}s, {self.audio_file}"

class AlarmEditDialog:
    def __init__(self, parent, alarm=None, title="Edit Alarm"):
        self.result = None
        self.dialog = tk.Toplevel(parent)
        self.dialog.title(title)
        self.dialog.transient(parent)
        self.dialog.grab_set()
        
        self.alarm = alarm if alarm else AlarmConfig()
        self.create_widgets()
        self.center_dialog()
        
    def create_widgets(self):
        # Title
        ttk.Label(self.dialog, text="Alarm Configuration", font=('Arial', 14, 'bold')).pack(pady=10)
        
        # Main frame
        main_frame = ttk.Frame(self.dialog)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        # Movement Requirements
        movement_frame = ttk.LabelFrame(main_frame, text="Movement Requirements", padding=10)
        movement_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(movement_frame, text="Horizontal Angle (degrees):").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.horizontal_var = tk.StringVar(value=str(self.alarm.min_horizontal_angle))
        horizontal_entry = ttk.Entry(movement_frame, textvariable=self.horizontal_var, width=10)
        horizontal_entry.grid(row=0, column=1, padx=10)
        add_tooltip(horizontal_entry, "Total horizontal scan angle required (left + right combined).\nExample: 45° means you need to look 22.5° left AND 22.5° right from VR center.\nSet to 0 to disable horizontal scanning requirement.")
        
        ttk.Label(movement_frame, text="Vertical Up Angle (degrees):").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.vertical_up_var = tk.StringVar(value=str(self.alarm.min_vertical_angle_up))
        vertical_up_entry = ttk.Entry(movement_frame, textvariable=self.vertical_up_var, width=10)
        vertical_up_entry.grid(row=1, column=1, padx=10)
        add_tooltip(vertical_up_entry, "Minimum upward angle from VR center to register an 'up' look.\nExample: 7.5° means you need to look up at least 7.5° from center.\nSet to 0 to disable upward scanning requirement.")
        
        ttk.Label(movement_frame, text="Vertical Down Angle (degrees):").grid(row=2, column=0, sticky=tk.W, pady=2)
        self.vertical_down_var = tk.StringVar(value=str(self.alarm.min_vertical_angle_down))
        vertical_down_entry = ttk.Entry(movement_frame, textvariable=self.vertical_down_var, width=10)
        vertical_down_entry.grid(row=2, column=1, padx=10)
        add_tooltip(vertical_down_entry, "Minimum downward angle from VR center to register a 'down' look.\nExample: 5.0° means you need to look down at least 5° from center.\nSet to 0 to disable downward scanning requirement.")
        
        ttk.Label(movement_frame, text="Max Time (milliseconds):").grid(row=3, column=0, sticky=tk.W, pady=2)
        self.max_time_var = tk.StringVar(value=str(self.alarm.max_time_ms))
        max_time_entry = ttk.Entry(movement_frame, textvariable=self.max_time_var, width=10)
        max_time_entry.grid(row=3, column=1, padx=10)
        add_tooltip(max_time_entry, "Maximum time allowed without completing ALL required lookout directions before alarm triggers.\nExample: 30000 = 30 seconds, 90000 = 90 seconds.\nRecommended range: 30000-90000 milliseconds.")
        
        ttk.Label(movement_frame, text="Min Lookout Time (ms):").grid(row=4, column=0, sticky=tk.W, pady=2)
        self.min_lookout_var = tk.StringVar(value=str(self.alarm.min_lookout_time_ms))
        min_lookout_entry = ttk.Entry(movement_frame, textvariable=self.min_lookout_var, width=10)
        min_lookout_entry.grid(row=4, column=1, padx=10)
        add_tooltip(min_lookout_entry, "Minimum time required between completing left and right scans for a valid horizontal lookout.\nPrevents quick head flicks from counting as proper traffic scanning.\nRecommended: 1000-3000 milliseconds.")
        
        # Audio Settings
        audio_frame = ttk.LabelFrame(main_frame, text="Audio Settings", padding=10)
        audio_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(audio_frame, text="Audio File:").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.audio_var = tk.StringVar(value=self.alarm.audio_file)
        audio_entry = ttk.Entry(audio_frame, textvariable=self.audio_var, width=30)
        audio_entry.grid(row=0, column=1, padx=10)
        ttk.Button(audio_frame, text="Browse...", command=self.browse_audio).grid(row=0, column=2)
        add_tooltip(audio_entry, "Path to audio file (.ogg, .wav, .mp3) for this alarm.\nUses 'beep.wav' as fallback if file not found.\nCan be absolute path or relative to Quest Lookout folder.")
        
        ttk.Label(audio_frame, text="Start Volume (0-100):").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.start_volume_var = tk.StringVar(value=str(self.alarm.start_volume))
        start_volume_entry = ttk.Entry(audio_frame, textvariable=self.start_volume_var, width=10)
        start_volume_entry.grid(row=1, column=1, padx=10, sticky=tk.W)
        add_tooltip(start_volume_entry, "Initial volume when alarm first sounds (0-100).\nAllows gentle start before ramping up to full volume.\nRecommended: 30-50 to avoid startling the pilot.")
        
        ttk.Label(audio_frame, text="End Volume (0-100):").grid(row=2, column=0, sticky=tk.W, pady=2)
        self.end_volume_var = tk.StringVar(value=str(self.alarm.end_volume))
        end_volume_entry = ttk.Entry(audio_frame, textvariable=self.end_volume_var, width=10)
        end_volume_entry.grid(row=2, column=1, padx=10, sticky=tk.W)
        add_tooltip(end_volume_entry, "Maximum volume the alarm reaches after ramping (0-100).\nShould be loud enough to hear over flight simulator audio.\nRecommended: 80-100 for safety-critical alerts.")
        
        ttk.Label(audio_frame, text="Volume Ramp Time (ms):").grid(row=3, column=0, sticky=tk.W, pady=2)
        self.ramp_time_var = tk.StringVar(value=str(self.alarm.volume_ramp_time_ms))
        ramp_time_entry = ttk.Entry(audio_frame, textvariable=self.ramp_time_var, width=10)
        ramp_time_entry.grid(row=3, column=1, padx=10, sticky=tk.W)
        add_tooltip(ramp_time_entry, "Time for volume to increase from start to end volume (milliseconds).\nSet to 0 for immediate full volume.\nGradual ramp (15000-30000ms) is less jarring than sudden loud alarms.")
        
        # Timing Settings
        timing_frame = ttk.LabelFrame(main_frame, text="Timing Settings", padding=10)
        timing_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(timing_frame, text="Repeat Interval (ms):").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.repeat_var = tk.StringVar(value=str(self.alarm.repeat_interval_ms))
        repeat_entry = ttk.Entry(timing_frame, textvariable=self.repeat_var, width=10)
        repeat_entry.grid(row=0, column=1, padx=10)
        add_tooltip(repeat_entry, "How often the alarm repeats if lookout still incomplete (milliseconds).\nShorter intervals = more frequent reminders.\nRecommended: 5000-30000ms (5-30 seconds) to avoid being too annoying.")
        
        ttk.Label(timing_frame, text="Silence After Look (ms):").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.silence_var = tk.StringVar(value=str(self.alarm.silence_after_look_ms))
        silence_entry = ttk.Entry(timing_frame, textvariable=self.silence_var, width=10)
        silence_entry.grid(row=1, column=1, padx=10)
        add_tooltip(silence_entry, "IMPORTANT: When you start a new lookout (move your head significantly), the alarm temporarily stops for this duration to give you time to complete the full scan pattern.\n\nThis prevents annoying audio while you're actively looking around.\nRecommended: 3000-8000ms (3-8 seconds) - enough time to complete L+R+Up scanning.")
        
        # Buttons
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X, pady=(15, 10), side=tk.BOTTOM)
        
        ttk.Button(button_frame, text="OK", command=self.ok_clicked).pack(side=tk.RIGHT, padx=5)
        ttk.Button(button_frame, text="Cancel", command=self.cancel_clicked).pack(side=tk.RIGHT, padx=5)
    
    def center_dialog(self):
        """Center the dialog on screen and size it to fit content"""
        # Force update to get actual size requirements
        self.dialog.update_idletasks()
        
        # Get the required size based on content
        req_width = self.dialog.winfo_reqwidth()
        req_height = self.dialog.winfo_reqheight()
        
        # Add some padding for safety
        width = max(req_width + 40, 500)  # Minimum 500px width
        height = max(req_height + 40, 350)  # Minimum 350px height
        
        # Get screen dimensions
        screen_width = self.dialog.winfo_screenwidth()
        screen_height = self.dialog.winfo_screenheight()
        
        # Calculate center position
        x = (screen_width - width) // 2
        y = (screen_height - height) // 2
        
        # Set geometry
        self.dialog.geometry(f"{width}x{height}+{x}+{y}")
        
        # Set minimum size but allow resizing if needed
        self.dialog.minsize(500, 350)
    
    def browse_audio(self):
        filename = filedialog.askopenfilename(
            title="Select Audio File",
            filetypes=[("Audio files", "*.ogg *.wav *.mp3"), ("All files", "*.*")]
        )
        if filename:
            # Convert to relative path if possible
            if os.path.commonpath([filename, os.getcwd()]):
                filename = os.path.relpath(filename)
            self.audio_var.set(filename)
    
    def ok_clicked(self):
        try:
            self.alarm.min_horizontal_angle = float(self.horizontal_var.get())
            self.alarm.min_vertical_angle_up = float(self.vertical_up_var.get())
            self.alarm.min_vertical_angle_down = float(self.vertical_down_var.get())
            self.alarm.max_time_ms = int(self.max_time_var.get())
            self.alarm.audio_file = self.audio_var.get()
            self.alarm.start_volume = int(self.start_volume_var.get())
            self.alarm.end_volume = int(self.end_volume_var.get())
            self.alarm.volume_ramp_time_ms = int(self.ramp_time_var.get())
            self.alarm.repeat_interval_ms = int(self.repeat_var.get())
            self.alarm.silence_after_look_ms = int(self.silence_var.get())
            self.alarm.min_lookout_time_ms = int(self.min_lookout_var.get())
            
            self.result = self.alarm
            self.dialog.destroy()
        except ValueError as e:
            messagebox.showerror("Invalid Input", f"Please enter valid numbers for all fields.\nError: {e}")
    
    def cancel_clicked(self):
        self.dialog.destroy()

class QuestLookoutGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Quest Lookout Settings Manager v1.0")
        self.root.geometry("800x700")
        
        # Center window
        self.root.update_idletasks()
        x = (self.root.winfo_screenwidth() // 2) - (800 // 2)
        y = (self.root.winfo_screenheight() // 2) - (700 // 2)
        self.root.geometry(f"800x700+{x}+{y}")
        
        self.settings = {
            'alarms': [],
            'center_reset': {'window_degrees': 10.0, 'hold_time_seconds': 4.0},
            'condor_log_path': 'C:\\Condor3\\Logs\\Logfile.txt'
        }
        
        self.create_widgets()
        self.load_settings()
        
    def create_widgets(self):
        # Title
        title_frame = ttk.Frame(self.root)
        title_frame.pack(fill=tk.X, padx=10, pady=10)
        ttk.Label(title_frame, text="Quest Lookout VR Safety Monitor", font=('Arial', 16, 'bold')).pack()
        ttk.Label(title_frame, text="Configuration Tool", font=('Arial', 12)).pack()
        
        # Main content frame
        content_frame = ttk.Frame(self.root)
        content_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Left panel - Alarms
        left_frame = ttk.Frame(content_frame)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
        
        alarms_frame = ttk.LabelFrame(left_frame, text="Alarm Configurations", padding=10)
        alarms_frame.pack(fill=tk.BOTH, expand=True)
        
        # Alarms listbox
        list_frame = ttk.Frame(alarms_frame)
        list_frame.pack(fill=tk.BOTH, expand=True)
        
        self.alarms_listbox = tk.Listbox(list_frame, font=('Consolas', 9))
        scrollbar = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.alarms_listbox.yview)
        self.alarms_listbox.config(yscrollcommand=scrollbar.set)
        
        self.alarms_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # Alarm buttons
        alarm_buttons = ttk.Frame(alarms_frame)
        alarm_buttons.pack(fill=tk.X, pady=5)
        
        ttk.Button(alarm_buttons, text="Add Alarm", command=self.add_alarm).pack(side=tk.LEFT, padx=2)
        ttk.Button(alarm_buttons, text="Edit Selected", command=self.edit_alarm).pack(side=tk.LEFT, padx=2)
        ttk.Button(alarm_buttons, text="Remove Selected", command=self.remove_alarm).pack(side=tk.LEFT, padx=2)
        ttk.Button(alarm_buttons, text="Duplicate", command=self.duplicate_alarm).pack(side=tk.LEFT, padx=2)
        
        # Right panel - Other settings
        right_frame = ttk.Frame(content_frame)
        right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=5)
        
        # Center reset settings
        reset_frame = ttk.LabelFrame(right_frame, text="Center Reset", padding=10)
        reset_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(reset_frame, text="Window (degrees):").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.reset_window_var = tk.StringVar()
        ttk.Entry(reset_frame, textvariable=self.reset_window_var, width=8).grid(row=0, column=1, padx=5)
        
        ttk.Label(reset_frame, text="Hold Time (seconds):").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.reset_hold_var = tk.StringVar()
        ttk.Entry(reset_frame, textvariable=self.reset_hold_var, width=8).grid(row=1, column=1, padx=5)
        
        # Condor integration
        condor_frame = ttk.LabelFrame(right_frame, text="Condor Integration", padding=10)
        condor_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(condor_frame, text="Log File Path:").pack(anchor=tk.W)
        path_frame = ttk.Frame(condor_frame)
        path_frame.pack(fill=tk.X)
        
        self.condor_path_var = tk.StringVar()
        ttk.Entry(path_frame, textvariable=self.condor_path_var, width=25).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(path_frame, text="Browse...", command=self.browse_condor_log).pack(side=tk.RIGHT, padx=(5,0))
        
        # Action buttons
        action_frame = ttk.LabelFrame(right_frame, text="Actions", padding=10)
        action_frame.pack(fill=tk.X, pady=5)
        
        ttk.Button(action_frame, text="Load Settings", command=self.load_settings).pack(fill=tk.X, pady=2)
        ttk.Button(action_frame, text="Save Settings", command=self.save_settings).pack(fill=tk.X, pady=2)
        ttk.Button(action_frame, text="Reset to Defaults", command=self.reset_defaults).pack(fill=tk.X, pady=2)
        
        # Info
        info_frame = ttk.LabelFrame(right_frame, text="Information", padding=10)
        info_frame.pack(fill=tk.X, pady=5)
        
        info_text = ("Quest Lookout monitors VR\n"
                    "head movements during flight\n"
                    "and provides safety alerts\n"
                    "when proper lookout\n"
                    "patterns aren't followed.\n\n"
                    "Each alarm has different\n"
                    "requirements and sensitivity.\n"
                    "When you start looking around,\n"
                    "alarms pause temporarily to\n"
                    "let you complete the scan.\n\n"
                    "Save settings and restart\n"
                    "Quest Lookout to apply changes.")
        ttk.Label(info_frame, text=info_text, font=('Arial', 8), justify=tk.LEFT).pack()
        
        # Exit button
        ttk.Button(right_frame, text="Exit", command=self.root.quit).pack(fill=tk.X, pady=10)
    
    def update_alarms_list(self):
        self.alarms_listbox.delete(0, tk.END)
        for i, alarm in enumerate(self.settings['alarms']):
            alarm_obj = AlarmConfig(alarm)
            self.alarms_listbox.insert(tk.END, f"Alarm {i+1}: {alarm_obj}")
    
    def add_alarm(self):
        dialog = AlarmEditDialog(self.root, title="Add New Alarm")
        self.root.wait_window(dialog.dialog)
        if dialog.result:
            self.settings['alarms'].append(dialog.result.to_dict())
            self.update_alarms_list()
    
    def edit_alarm(self):
        selection = self.alarms_listbox.curselection()
        if not selection:
            messagebox.showwarning("No Selection", "Please select an alarm to edit.")
            return
        
        index = selection[0]
        alarm = AlarmConfig(self.settings['alarms'][index])
        dialog = AlarmEditDialog(self.root, alarm, "Edit Alarm")
        self.root.wait_window(dialog.dialog)
        if dialog.result:
            self.settings['alarms'][index] = dialog.result.to_dict()
            self.update_alarms_list()
    
    def remove_alarm(self):
        selection = self.alarms_listbox.curselection()
        if not selection:
            messagebox.showwarning("No Selection", "Please select an alarm to remove.")
            return
        
        if messagebox.askyesno("Confirm Delete", "Remove the selected alarm configuration?"):
            index = selection[0]
            del self.settings['alarms'][index]
            self.update_alarms_list()
    
    def duplicate_alarm(self):
        selection = self.alarms_listbox.curselection()
        if not selection:
            messagebox.showwarning("No Selection", "Please select an alarm to duplicate.")
            return
        
        index = selection[0]
        alarm_copy = self.settings['alarms'][index].copy()
        self.settings['alarms'].append(alarm_copy)
        self.update_alarms_list()
        messagebox.showinfo("Success", "Alarm duplicated successfully!")
    
    def browse_condor_log(self):
        filename = filedialog.askopenfilename(
            title="Select Condor Log File",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            initialdir="C:/Condor3/Logs" if os.path.exists("C:/Condor3/Logs") else "C:/"
        )
        if filename:
            self.condor_path_var.set(filename)
    
    def load_settings(self):
        try:
            if os.path.exists('settings.json'):
                with open('settings.json', 'r') as f:
                    data = json.load(f)
                    # Skip _instructions if present
                    if '_instructions' in data:
                        del data['_instructions']
                    self.settings.update(data)
                
                self.update_alarms_list()
                self.reset_window_var.set(str(self.settings['center_reset']['window_degrees']))
                self.reset_hold_var.set(str(self.settings['center_reset']['hold_time_seconds']))
                self.condor_path_var.set(self.settings['condor_log_path'])
                
                messagebox.showinfo("Success", "Settings loaded successfully!")
            else:
                # Check if default template exists
                if os.path.exists('settings_default.json'):
                    if messagebox.askyesno("No Settings Found", 
                                         "settings.json not found.\n\n"
                                         "Would you like to create it from the default template?\n"
                                         "This will set up recommended alarm configurations."):
                        try:
                            # Copy default template
                            import shutil
                            shutil.copy('settings_default.json', 'settings.json')
                            messagebox.showinfo("Success", 
                                              "Default settings.json created!\n"
                                              "Loading default configuration...")
                            # Recursively call to load the new file
                            self.load_settings()
                            return
                        except Exception as e:
                            messagebox.showerror("Error", f"Failed to create default settings:\n{e}")
                
                # Fallback to built-in defaults
                messagebox.showwarning("File Not Found", 
                                     "settings.json not found.\n"
                                     "Using built-in defaults.\n\n"
                                     "Save your settings to create the file.")
                self.reset_defaults()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load settings:\n{e}")
    
    def save_settings(self):
        try:
            # Update settings from UI
            self.settings['center_reset']['window_degrees'] = float(self.reset_window_var.get())
            self.settings['center_reset']['hold_time_seconds'] = float(self.reset_hold_var.get())
            self.settings['condor_log_path'] = self.condor_path_var.get()
            
            with open('settings.json', 'w') as f:
                json.dump(self.settings, f, indent=2)
            
            messagebox.showinfo("Success", "Settings saved to settings.json!\n\nRestart Quest Lookout to apply changes.")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save settings:\n{e}")
    
    def reset_defaults(self):
        if messagebox.askyesno("Confirm Reset", "Reset all settings to defaults? This will clear all current configurations."):
            self.settings = {
                'alarms': [
                    AlarmConfig({'min_horizontal_angle': 45.0, 'max_time_ms': 30000}).to_dict(),
                    AlarmConfig({'min_horizontal_angle': 120.0, 'max_time_ms': 90000, 'audio_file': 'notalentassclown.ogg'}).to_dict()
                ],
                'center_reset': {'window_degrees': 10.0, 'hold_time_seconds': 4.0},
                'condor_log_path': 'C:\\Condor3\\Logs\\Logfile.txt'
            }
            
            self.update_alarms_list()
            self.reset_window_var.set('10.0')
            self.reset_hold_var.set('4.0')
            self.condor_path_var.set('C:\\Condor3\\Logs\\Logfile.txt')
            
            messagebox.showinfo("Reset Complete", "Settings reset to defaults.")
    
    def run(self):
        self.root.mainloop()

if __name__ == "__main__":
    app = QuestLookoutGUI()
    app.run()