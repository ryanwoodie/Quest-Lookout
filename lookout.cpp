// lookout.cpp
// Oculus Lookout Utility (C++)
// Requires Oculus SDK (OVR)

#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POLL_INTERVAL 0.05 
#define LOG_CHECK_INTERVAL 1.0 // Check Condor log file every 1 second
#include <fstream>
#include "json.hpp" 
#include <SFML/Audio.hpp>
#include <windows.h>
#include <winuser.h>   // For VK_ constants and hotkey functions
#include <shellapi.h> // For Shell_NotifyIcon
#include <tlhelp32.h>  // For process enumeration
#include <thread>       // For std::thread
#include <cstdio>       // For _wfreopen_s, FILE 
#include <SFML/System/Time.hpp>
#include <SFML/System/FileInputStream.hpp>

#include <unordered_map>
#include <memory>
#include <vector>
// #include <deque> // No longer needed for yaw_window, pitch_window
#include <algorithm>
#include <iomanip>
#include <cctype> 

// Forward declarations for startup management
bool is_startup_enabled_in_registry();
bool enable_startup_in_registry();
bool disable_startup_in_registry();
void sync_startup_setting_from_json();

// Hotkey message ID
#define WM_HOTKEY_RECENTER 1004

// Forward declarations for hotkey management  
bool parse_hotkey(const std::string& hotkey_str, UINT& modifiers, UINT& vk_code);
bool register_recenter_hotkey(HWND hwnd);
void unregister_recenter_hotkey(HWND hwnd);
void load_hotkey_from_settings();
bool is_condor_simulation_window_active();
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// Hotkey globals (declared early for function access)
std::string g_recenter_hotkey = "Num5";
bool g_hotkey_registered = false;
HHOOK g_keyboard_hook = nullptr;
UINT g_target_vk_code = 0;
UINT g_target_modifiers = 0;

// Oculus session global (for hotkey access)
ovrSession g_ovr_session = nullptr;

// Software recenter flag and offset
bool g_request_software_recenter = false;
bool g_request_baseline_reset = false; // Reset baseline reference to current head position
ovrQuatf g_recenter_offset = {0, 0, 0, 1}; // Identity quaternion
ovrQuatf g_baseline_reference = {0, 0, 0, 1}; // Baseline reference orientation
bool g_has_manual_recenter_offset = false; // Track if user has manually set offset
bool g_has_baseline_reference = false; // Track if we have a custom baseline

struct LookoutAlarmConfig {
    double min_horizontal_angle = 120.0; 
    double min_vertical_angle_up = 20.0;   
    double min_vertical_angle_down = 5.0; 
    int max_time_ms = 60000;             
    std::string audio_file;             
    int start_volume = 5;              
    int end_volume = 100;               
    int volume_ramp_time_ms = 30000;     
    int repeat_interval_ms = 5000;      
    int min_lookout_time_ms = 2000;     
    int silence_after_look_ms = 5000;   

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LookoutAlarmConfig,
        min_horizontal_angle, min_vertical_angle_up, min_vertical_angle_down, max_time_ms, audio_file,
        start_volume, end_volume, volume_ramp_time_ms, repeat_interval_ms, min_lookout_time_ms,
        silence_after_look_ms)
};

inline double rad2deg(double rad) { return rad * 180.0 / M_PI; }

// Quaternion multiplication for applying software recenter offset
ovrQuatf quat_multiply(const ovrQuatf& q1, const ovrQuatf& q2) {
    ovrQuatf result;
    result.w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z;
    result.x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y;
    result.y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x;
    result.z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w;
    return result;
}

// Clear software recenter offset (reset to identity)  
void clear_software_recenter() {
    g_recenter_offset = {0, 0, 0, 1}; // Identity quaternion
    g_has_manual_recenter_offset = false;
    std::cout << "[INFO] Software recenter offset cleared" << std::endl;
}

// Reset baseline reference to current head position
void reset_baseline_reference() {
    g_baseline_reference = {0, 0, 0, 1}; // Will be set in main loop
    g_has_baseline_reference = false; // Will be set to true when captured
    g_request_baseline_reset = true;
    clear_software_recenter(); // Also clear any manual offset
    std::cout << "[INFO] Baseline reference reset requested" << std::endl;
}

// Quaternion conjugate (inverse rotation)
ovrQuatf quat_conjugate(const ovrQuatf& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

void quat_to_yaw_pitch(const ovrQuatf& q, double& yaw_deg, double& pitch_deg) {
    ovrQuatf working_q = q;
    
    // Apply baseline reference transformation (if we have one)
    if (g_has_baseline_reference) {
        // Transform relative to baseline: q_relative = q_baseline_inverse * q_current
        working_q = quat_multiply(quat_conjugate(g_baseline_reference), q);
    }
    
    // Apply manual recenter offset (only if user has manually set one)
    if (g_has_manual_recenter_offset) {
        working_q = quat_multiply(working_q, g_recenter_offset);
    }
    
    double ys = 2.0 * (working_q.w * working_q.y + working_q.x * working_q.z);
    double yc = 1.0 - 2.0 * (working_q.y * working_q.y + working_q.z * working_q.z);
    double ps = 2.0 * (working_q.w * working_q.x - working_q.z * working_q.y);
    ps = (std::max)((-1.0), (std::min)(1.0, ps)); 
    yaw_deg = rad2deg(std::atan2(ys, yc)); // Output is [-180, 180]
    pitch_deg = rad2deg(std::asin(ps));   // Output is [-90, 90]
}

std::vector<LookoutAlarmConfig> load_configs(const std::string& filename) {
    std::vector<LookoutAlarmConfig> cfgs;
    std::ifstream f(filename);
    if (f) {
        try {
            nlohmann::json j;
            f >> j;
            if (j.contains("alarms")) {
                for (const auto& item : j["alarms"]) {
                    cfgs.push_back(item.get<LookoutAlarmConfig>());
                }
            }
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[ERROR] Failed to parse settings.json: " << e.what() << std::endl;
        } catch (const nlohmann::json::type_error& e) {
            std::cerr << "[ERROR] JSON type error in settings.json: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[ERROR] Could not open settings.json" << std::endl;
    }
    return cfgs;
}

bool is_startup_enabled_in_registry() {
    HKEY hkey = HKEY_CURRENT_USER;
    const char* key_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    HKEY reg_key;
    if (RegOpenKeyExA(hkey, key_path, 0, KEY_READ, &reg_key) != ERROR_SUCCESS) {
        return false;
    }
    
    DWORD value_type;
    DWORD data_size = 0;
    LONG result = RegQueryValueExA(reg_key, "Quest Lookout", nullptr, &value_type, nullptr, &data_size);
    RegCloseKey(reg_key);
    
    return (result == ERROR_SUCCESS);
}

bool enable_startup_in_registry() {
    HKEY hkey = HKEY_CURRENT_USER;
    const char* key_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    HKEY reg_key;
    if (RegOpenKeyExA(hkey, key_path, 0, KEY_WRITE, &reg_key) != ERROR_SUCCESS) {
        return false;
    }
    
    // Get current executable path
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    
    // Create quoted path string
    std::string quoted_path = "\"" + std::string(exe_path) + "\"";
    
    LONG result = RegSetValueExA(reg_key, "Quest Lookout", 0, REG_SZ, 
                                (const BYTE*)quoted_path.c_str(), quoted_path.length() + 1);
    RegCloseKey(reg_key);
    
    return (result == ERROR_SUCCESS);
}

bool disable_startup_in_registry() {
    HKEY hkey = HKEY_CURRENT_USER;
    const char* key_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    HKEY reg_key;
    if (RegOpenKeyExA(hkey, key_path, 0, KEY_WRITE, &reg_key) != ERROR_SUCCESS) {
        return false;
    }
    
    LONG result = RegDeleteValueA(reg_key, "Quest Lookout");
    RegCloseKey(reg_key);
    
    return (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
}

void sync_startup_setting_from_json() {
    std::ifstream f("settings.json");
    if (!f) {
        std::cout << "[INFO] No settings.json found for startup sync." << std::endl;
        return;
    }
    
    try {
        nlohmann::json j;
        f >> j;
        
        bool json_startup_setting = j.value("start_with_windows", false);
        bool registry_startup_enabled = is_startup_enabled_in_registry();
        
        // If JSON and registry are out of sync, make registry match JSON
        if (json_startup_setting != registry_startup_enabled) {
            if (json_startup_setting) {
                if (enable_startup_in_registry()) {
                    std::cout << "[INFO] Enabled Windows startup to match settings.json" << std::endl;
                } else {
                    std::cout << "[WARNING] Failed to enable Windows startup" << std::endl;
                }
            } else {
                if (disable_startup_in_registry()) {
                    std::cout << "[INFO] Disabled Windows startup to match settings.json" << std::endl;
                } else {
                    std::cout << "[WARNING] Failed to disable Windows startup" << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[WARNING] Could not parse startup setting from settings.json: " << e.what() << std::endl;
    }
}

// Hotkey management functions
bool parse_hotkey(const std::string& hotkey_str, UINT& modifiers, UINT& vk_code) {
    modifiers = 0;
    vk_code = 0;
    
    std::string key_str = hotkey_str;
    std::transform(key_str.begin(), key_str.end(), key_str.begin(), ::tolower);
    
    // Parse modifiers
    if (key_str.find("ctrl+") != std::string::npos) {
        modifiers |= MOD_CONTROL;
        key_str.erase(key_str.find("ctrl+"), 5);
    }
    if (key_str.find("shift+") != std::string::npos) {
        modifiers |= MOD_SHIFT;
        key_str.erase(key_str.find("shift+"), 6);
    }
    if (key_str.find("alt+") != std::string::npos) {
        modifiers |= MOD_ALT;
        key_str.erase(key_str.find("alt+"), 4);
    }
    
    // Parse key codes
    if (key_str == "num5") vk_code = 0x65;      // VK_NUMPAD5
    else if (key_str == "num0") vk_code = 0x60; // VK_NUMPAD0
    else if (key_str == "num1") vk_code = 0x61; // VK_NUMPAD1
    else if (key_str == "num2") vk_code = 0x62; // VK_NUMPAD2
    else if (key_str == "num3") vk_code = 0x63; // VK_NUMPAD3
    else if (key_str == "num4") vk_code = 0x64; // VK_NUMPAD4
    else if (key_str == "num6") vk_code = 0x66; // VK_NUMPAD6
    else if (key_str == "num7") vk_code = 0x67; // VK_NUMPAD7
    else if (key_str == "num8") vk_code = 0x68; // VK_NUMPAD8
    else if (key_str == "num9") vk_code = 0x69; // VK_NUMPAD9
    else if (key_str == "f1") vk_code = 0x70;   // VK_F1
    else if (key_str == "f2") vk_code = 0x71;   // VK_F2
    else if (key_str == "f3") vk_code = 0x72;   // VK_F3
    else if (key_str == "f4") vk_code = 0x73;   // VK_F4
    else if (key_str == "f5") vk_code = 0x74;   // VK_F5
    else if (key_str == "f6") vk_code = 0x75;   // VK_F6
    else if (key_str == "f7") vk_code = 0x76;   // VK_F7
    else if (key_str == "f8") vk_code = 0x77;   // VK_F8
    else if (key_str == "f9") vk_code = 0x78;   // VK_F9
    else if (key_str == "f10") vk_code = 0x79;  // VK_F10
    else if (key_str == "f11") vk_code = 0x7A;  // VK_F11
    else if (key_str == "f12") vk_code = 0x7B;  // VK_F12
    else if (key_str.length() == 1) {
        char c = key_str[0];
        if (c >= 'a' && c <= 'z') vk_code = 0x41 + (c - 'a');  // VK_A = 0x41
        else if (c >= '0' && c <= '9') vk_code = 0x30 + (c - '0');  // VK_0 = 0x30
        else return false;
    } else {
        return false;
    }
    
    return vk_code != 0;
}

// Low-level keyboard hook procedure
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        
        // Check if this is our target key
        if (pKeyboard->vkCode == g_target_vk_code) {
            // Check modifiers
            bool ctrl_pressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift_pressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt_pressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            
            UINT current_modifiers = 0;
            if (ctrl_pressed) current_modifiers |= MOD_CONTROL;
            if (shift_pressed) current_modifiers |= MOD_SHIFT;
            if (alt_pressed) current_modifiers |= MOD_ALT;
            
            // Only trigger if modifiers match AND Condor simulation is active
            if (current_modifiers == g_target_modifiers && is_condor_simulation_window_active()) {
                std::cout << "[INFO] Recenter hotkey pressed (non-blocking)" << std::endl;
                if (g_ovr_session) {
                    // Try hardware recenter first
                    ovrResult result = ovr_RecenterTrackingOrigin(g_ovr_session);
                    if (OVR_SUCCESS(result)) {
                        std::cout << "[INFO] Hardware recenter attempted" << std::endl;
                    }
                    
                    // Also reset Quest Lookout's internal tracking reference
                    g_request_software_recenter = true;
                    std::cout << "[INFO] Quest Lookout tracking reference reset requested" << std::endl;
                } else {
                    std::cout << "[WARNING] Cannot recenter: Oculus session not available" << std::endl;
                }
                // Don't block the key - let it pass through to other applications
            }
        }
    }
    
    // Always call next hook (don't block the key)
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
}

bool register_recenter_hotkey(HWND hwnd) {
    if (g_hotkey_registered) {
        unregister_recenter_hotkey(hwnd);
    }
    
    if (!parse_hotkey(g_recenter_hotkey, g_target_modifiers, g_target_vk_code)) {
        std::cerr << "[WARNING] Invalid hotkey format: " << g_recenter_hotkey << std::endl;
        return false;
    }
    
    // Install low-level keyboard hook (non-blocking)
    g_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_keyboard_hook) {
        g_hotkey_registered = true;
        std::cout << "[INFO] Registered non-blocking recenter hotkey: " << g_recenter_hotkey << std::endl;
        return true;
    } else {
        DWORD error = GetLastError();
        std::cerr << "[WARNING] Failed to install keyboard hook for hotkey: " << g_recenter_hotkey 
                  << " (error=" << error << ")" << std::endl;
        return false;
    }
}

void unregister_recenter_hotkey(HWND hwnd) {
    if (g_hotkey_registered) {
        if (g_keyboard_hook) {
            UnhookWindowsHookEx(g_keyboard_hook);
            g_keyboard_hook = nullptr;
        }
        g_hotkey_registered = false;
    }
}

void load_hotkey_from_settings() {
    std::ifstream f("settings.json");
    if (!f) return;
    
    try {
        nlohmann::json j;
        f >> j;
        
        if (j.contains("recenter_hotkey") && j["recenter_hotkey"].is_string()) {
            g_recenter_hotkey = j["recenter_hotkey"].get<std::string>();
            std::cout << "[INFO] Loaded recenter hotkey: " << g_recenter_hotkey << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[WARNING] Could not parse recenter_hotkey from settings.json: " << e.what() << std::endl;
    }
}

bool is_condor_process_running() {
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (!Process32First(hProcessSnap, &pe32)) {
        CloseHandle(hProcessSnap);
        return false;
    }
    
    bool condor_found = false;
    do {
        // Check for Condor.exe (case insensitive)
        std::string processName = pe32.szExeFile;
        std::transform(processName.begin(), processName.end(), processName.begin(), ::tolower);
        
        if (processName == "condor.exe" || processName == "condor3.exe") {
            condor_found = true;
            break;
        }
    } while (Process32Next(hProcessSnap, &pe32));
    
    CloseHandle(hProcessSnap);
    return condor_found;
}

// Structure to pass data to window enumeration callback
struct WindowEnumData {
    DWORD processId;
    bool hasSimWindow;
    std::string simWindowTitle;
};

// Callback function for EnumWindows
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowEnumData* data = reinterpret_cast<WindowEnumData*>(lParam);
    
    DWORD windowProcessId;
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    
    // Only check windows from the Condor process
    if (windowProcessId != data->processId) {
        return TRUE; // Continue enumeration
    }
    
    // Skip if window is not visible
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    
    // Get window title
    char windowTitle[256];
    if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle)) == 0) {
        return TRUE; // Continue if no title
    }
    
    std::string title = windowTitle;
    std::transform(title.begin(), title.end(), title.begin(), ::tolower);
    
    // Look for simulation window characteristics:
    // - Contains "condor" and version info (like "condor version 3.0.8")
    // - Has substantial size (simulation windows are typically large)
    // - Exclude obvious setup/menu windows
    if (title.find("condor") != std::string::npos && 
        title.find("version") != std::string::npos) {
        
        // Check window size to distinguish sim window from small dialogs
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            // Simulation windows are typically large (>= 800x600)
            if (width >= 800 && height >= 600) {
                data->hasSimWindow = true;
                data->simWindowTitle = windowTitle;
                return FALSE; // Stop enumeration, found what we need
            }
        }
    }
    
    return TRUE; // Continue enumeration
}

// Debug callback to log all Condor-related windows
BOOL CALLBACK FindCondorWindowProc(HWND hwnd, LPARAM lParam) {
    char windowTitle[256];
    char className[256];
    wchar_t displayName[256];
    
    // Get window title and class name
    GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
    GetClassNameA(hwnd, className, sizeof(className));
    
    // Try to get the display name (what taskbar shows)
    std::string display_title;
    if (GetWindowTextW(hwnd, displayName, sizeof(displayName)/sizeof(wchar_t))) {
        // Convert wide string to regular string
        int size = WideCharToMultiByte(CP_UTF8, 0, displayName, -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            std::vector<char> buffer(size);
            WideCharToMultiByte(CP_UTF8, 0, displayName, -1, buffer.data(), size, nullptr, nullptr);
            display_title = buffer.data();
        }
    }
    
    std::string title = windowTitle;
    std::string class_name = className;
    std::string title_lower = title;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::tolower);
    
    // Log any window that contains "condor" for debugging (even empty titles if class matches)
    bool isCondorRelated = (title_lower.find("condor") != std::string::npos) || 
                          (class_name.find("Condor") != std::string::npos);
    
    if (isCondorRelated && IsWindowVisible(hwnd)) {
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            // Look for simulation window - use class name for reliable detection
            if (title_lower.find("condor") != std::string::npos && 
                title_lower.find("version") != std::string::npos) {
                // Simulation window has main window class (not TGUIForm or TApplication)
                if (class_name != "TGUIForm" && class_name != "TApplication" && width > 100 && height > 100) {
                    *reinterpret_cast<bool*>(lParam) = true;
                    return FALSE; // Stop enumeration
                }
            }
        }
    }
    return TRUE; // Continue enumeration
}

bool is_condor_simulation_window_active() {
    bool found = false;
    EnumWindows(FindCondorWindowProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

sf::Music* get_or_create_sound_player(const std::string& audio_file) {
    std::string file_to_play = audio_file.empty() ? "beep.wav" : audio_file;
    
    // Try to create the music object with proper error handling
    auto music = std::make_unique<sf::Music>();
    
    try {
        // First try the specified file
        if (!music->openFromFile(file_to_play)) {
            std::cerr << "[ERROR] Could not load music file: " << file_to_play << std::endl;
            
            // If not the default, try beep.wav as fallback
            if (file_to_play != "beep.wav") {
                std::cerr << "[INFO] Attempting to load default beep.wav" << std::endl;
                if (!music->openFromFile("beep.wav")) {
                    std::cerr << "[ERROR] Could not load default music file: beep.wav" << std::endl;
                    return nullptr;
                }
            } else {
                return nullptr;
            }
        }
        
        // Test if we can actually play the audio (this will catch driver issues)
        music->setVolume(0); // Silent test
        music->play();
        music->stop();
        
        std::cout << "[INFO] Successfully loaded and tested audio file: " << file_to_play << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in audio system: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "[ERROR] Unknown exception in audio system" << std::endl;
        return nullptr;
    }
    
    // Return properly managed resource - no intentional leak
    return music.release();
}

// Defines for tray icon
#define WM_APP_TRAYMSG (WM_APP + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM 1002
#define ID_TRAY_TOGGLE_CONSOLE_ITEM 1003
#define ID_TRAY_SETTINGS_ITEM 1004

const char* const WINDOW_CLASS_NAME = "QuestLookoutWindowClass";
HWND g_hwnd;
NOTIFYICONDATA nidApp;
bool g_is_console_visible = false;

// Forward declaration for our core application logic
int app_core_logic(); 

// Console Management Functions
void ShowConsoleWindow()
{
    if (!g_is_console_visible)
    {
        if (AllocConsole())
        {
            FILE* fp_stdout, *fp_stderr, *fp_stdin;
            if (_wfreopen_s(&fp_stdout, L"CONOUT$", L"w", stdout) != 0 ||
                _wfreopen_s(&fp_stderr, L"CONOUT$", L"w", stderr) != 0 ||
                _wfreopen_s(&fp_stdin, L"CONIN$", L"r", stdin) != 0)
            {
                FreeConsole(); 
                return;
            }
            std::cout.clear();
            std::cerr.clear();
            std::cin.clear(); 
            SetConsoleTitleA("Quest Lookout Status"); 
            g_is_console_visible = true;
            std::cout << "[INFO] Status window opened." << std::endl; 
        }
    }
}

void HideConsoleWindow()
{
    if (g_is_console_visible)
    {
        std::cin.clear();
        
        FILE* fp_nul_stdout, *fp_nul_stderr, *fp_nul_stdin;
        _wfreopen_s(&fp_nul_stdout, L"NUL", L"w", stdout);
        _wfreopen_s(&fp_nul_stderr, L"NUL", L"w", stderr);
        _wfreopen_s(&fp_nul_stdin, L"NUL", L"r", stdin);

        if (FreeConsole())
        {
            g_is_console_visible = false;
        }
    }
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_APP_TRAYMSG:
            switch (lParam) 
            {
                case WM_RBUTTONUP: 
                {
                    POINT curPoint;
                    GetCursorPos(&curPoint);
                    HMENU hPopupMenu = CreatePopupMenu();
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, ID_TRAY_SETTINGS_ITEM, "Settings");
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_SEPARATOR, 0, NULL); 
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, 
                               ID_TRAY_TOGGLE_CONSOLE_ITEM, 
                               g_is_console_visible ? "Hide Status Window" : "Show Status Window"); 
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_SEPARATOR, 0, NULL); 
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, "Exit");
                    
                    SetForegroundWindow(hwnd); 

                    TrackPopupMenu(hPopupMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, curPoint.x, curPoint.y, 0, hwnd, NULL);
                    DestroyMenu(hPopupMenu);

                    PostMessage(hwnd, WM_NULL, 0, 0); 
                }
                return 0;
            }
            break;

        case WM_COMMAND: 
            switch (LOWORD(wParam))
            {
                case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
                    std::cout << "[INFO] Exit requested from tray menu. Shutting down." << std::endl;
                    DestroyWindow(hwnd); 
                    break;
                case ID_TRAY_SETTINGS_ITEM:
                    std::cout << "[INFO] Opening settings from tray menu." << std::endl;
                    ShellExecute(NULL, "open", "settings_gui.exe", NULL, NULL, SW_SHOWNORMAL);
                    break;
                case ID_TRAY_TOGGLE_CONSOLE_ITEM:
                    if (g_is_console_visible)
                    {
                        HideConsoleWindow();
                    }
                    else
                    {
                        ShowConsoleWindow();
                    }
                    break;
            }
            break;


        case WM_DESTROY:
            if (g_is_console_visible) HideConsoleWindow(); 
            unregister_recenter_hotkey(hwnd);
            Shell_NotifyIcon(NIM_DELETE, &nidApp); 
            PostQuitMessage(0); 
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance); 
    UNREFERENCED_PARAMETER(lpCmdLine); 
    UNREFERENCED_PARAMETER(nCmdShow);

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION); 
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassEx(&wc))
    {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    g_hwnd = CreateWindowEx(
        0,                              
        WINDOW_CLASS_NAME,              
        "Quest Lookout Hidden Window",  
        0,                              
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
        NULL,       
        NULL,       
        hInstance,  
        NULL        
    );

    if (g_hwnd == NULL)
    {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    nidApp.cbSize = sizeof(NOTIFYICONDATA);
    nidApp.hWnd = g_hwnd;
    nidApp.uID = ID_TRAY_APP_ICON;
    nidApp.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nidApp.uCallbackMessage = WM_APP_TRAYMSG;
    nidApp.hIcon = LoadIcon(NULL, IDI_APPLICATION); 
    strncpy_s(nidApp.szTip, "Quest Lookout", _TRUNCATE); 


    if (!Shell_NotifyIcon(NIM_ADD, &nidApp)) {
        MessageBox(NULL, "Failed to add tray icon!", "Error!", MB_ICONEXCLAMATION | MB_OK);
    }
    
    // Load and register hotkey for recentering (must be in main thread)
    load_hotkey_from_settings();
    register_recenter_hotkey(g_hwnd);
    
    std::thread core_logic_thread(app_core_logic);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) 
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (core_logic_thread.joinable()) {
        core_logic_thread.join(); 
    }

    return (int)msg.wParam;
}

int app_core_logic() 
{
    std::cout << "[INFO] Quest Lookout starting - waiting for Oculus HMD connection..." << std::endl;
    
    ovrSession session = nullptr;
    bool ovr_initialized = false;
    int retry_count = 0;
    const int max_retries = -1; // Infinite retries
    const int retry_delay_ms = 3000; // 3 seconds between retries
    
    // Keep trying to initialize until successful or window closed
    while (IsWindow(g_hwnd)) {
        if (!ovr_initialized) {
            ovrInitParams initParams = {0};
            initParams.Flags = ovrInit_Invisible; 
            initParams.RequestedMinorVersion = OVR_MINOR_VERSION; 

            ovrResult result = ovr_Initialize(&initParams);
            if (OVR_FAILURE(result)) {
                retry_count++;
                ovrErrorInfo errorInfo;
                ovr_GetLastErrorInfo(&errorInfo);
                if (retry_count == 1) {
                    std::cout << "[INFO] Waiting for Oculus service to start..." << std::endl;
                } else if (retry_count % 10 == 0) { // Every 30 seconds
                    std::cout << "[INFO] Still waiting for Oculus HMD (attempt " << retry_count << ")..." << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                continue;
            }
            
            ovr_initialized = true;
            std::cout << "[INFO] OVR Initialized with ovrInit_Invisible flag." << std::endl;
        }
        
        // Try to create session
        ovrGraphicsLuid luid;
        ovrResult result = ovr_Create(&session, &luid);
        if (OVR_FAILURE(result)) {
            retry_count++;
            ovrErrorInfo errorInfo;
            ovr_GetLastErrorInfo(&errorInfo);
            
            if (retry_count == 1) {
                std::cout << "[INFO] Waiting for Oculus HMD to be connected and ready..." << std::endl;
            } else if (retry_count % 10 == 0) { // Every 30 seconds
                std::cout << "[INFO] Still waiting for HMD connection (attempt " << retry_count << ")..." << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
            continue;
        }
        
        // Success!
        std::cout << "[INFO] OVR Session Created - HMD connected and ready!" << std::endl;
        g_ovr_session = session;  // Store in global for hotkey access
        break;
    }
    
    // Check if we exited because window was closed
    if (!IsWindow(g_hwnd)) {
        std::cout << "[INFO] Application closing during HMD initialization." << std::endl;
        if (ovr_initialized) {
            ovr_Shutdown();
        }
        return 0;
    }


    std::vector<LookoutAlarmConfig> alarms = load_configs("settings.json");
    
    // Sync Windows startup setting with settings.json
    sync_startup_setting_from_json();
    
    if (alarms.empty()) { 
        std::cerr << "[ERROR] No Alarms Loaded from settings.json. Exiting." << std::endl; 
        if (IsWindow(g_hwnd)) PostMessage(g_hwnd, WM_COMMAND, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, 0); // Try to exit cleanly
        ovr_Destroy(session); 
        ovr_Shutdown(); 
        return 1; 
    }

    std::cout << "[INFO] Monitoring Condor simulation windows for flight detection" << std::endl;
    
    // Check initial Condor flight status using window detection only
    bool condor_flight_active = is_condor_simulation_window_active();
    
    if (condor_flight_active) {
        std::cout << "[INFO] Condor simulation window detected - flight active." << std::endl;
    } else {
        std::cout << "[INFO] No Condor simulation window detected - flight inactive." << std::endl;
    }
    std::cout << "[INFO] Initial Condor flight status: " << (condor_flight_active ? "Active." : "Inactive.") << std::endl;
    
    std::cout << "Oculus Lookout Utility core logic started." << std::endl;
    for (size_t i = 0; i < alarms.size(); ++i) { 
        if (alarms[i].repeat_interval_ms < 100) alarms[i].repeat_interval_ms = 5000; 
        if (alarms[i].min_horizontal_angle <= 0 || (alarms[i].min_vertical_angle_up <= 0 && alarms[i].min_vertical_angle_down <= 0)) {
            std::cout << "[INFO] Alarm " << i << " is disabled (min_horizontal_angle <= 0 or both min_vertical_angle_up/down <= 0)." << std::endl;
            alarms[i].min_horizontal_angle = -1; // Mark as disabled for later checks
        }
        if (alarms[i].min_horizontal_angle > 0) { 
            std::cout << "[INFO] Alarm " << i << ": HAngle=" << alarms[i].min_horizontal_angle 
                    << ", VAngleUp=" << alarms[i].min_vertical_angle_up 
                    << ", VAngleDown=" << alarms[i].min_vertical_angle_down 
                    << ", MaxTime=" << alarms[i].max_time_ms/1000.0 << "s"
                    << ", Repeat=" << alarms[i].repeat_interval_ms/1000.0 << "s"
                    << ", MinLookout=" << alarms[i].min_lookout_time_ms/1000.0 << "s (Min L/R diff)" 
                    << ", SilenceAfterLook=" << alarms[i].silence_after_look_ms/1000.0 << "s"
                    << std::endl;
        }
    }
    
    size_t widest_alarm_idx = 0;
    bool widest_alarm_valid = false;
    if (!alarms.empty()) {
        double widest_angle = 0.0; 
        for (size_t i = 0; i < alarms.size(); ++i) {
            if (alarms[i].min_horizontal_angle > 0) { 
                if (!widest_alarm_valid || alarms[i].min_horizontal_angle > widest_angle) {
                    widest_angle = alarms[i].min_horizontal_angle;
                    widest_alarm_idx = i;
                    widest_alarm_valid = true;
                }
            }
        }
        if (!widest_alarm_valid) { 
            std::cerr << "[WARNING] No valid (enabled) alarms configured for widest_alarm_idx logic." << std::endl;
        }
    }

    double elapsed_time_ms = 0.0;
    double log_check_timer_ms = 0.0; // Timer for checking Condor log file

    double center_reset_window_degrees = 20.0; 
    double center_reset_hold_time_seconds = 3.0; 
    double center_hold_timer_seconds = 0.0; 
    bool center_reset_active = false; 
    {
        std::ifstream f("settings.json");
        if (f) {
            nlohmann::json j;
             try { 
                f >> j; 
                if (j.contains("center_reset")) {
                    if (j["center_reset"].contains("window_degrees") && j["center_reset"]["window_degrees"].is_number())
                        center_reset_window_degrees = j["center_reset"]["window_degrees"].get<double>();
                    if (j["center_reset"].contains("hold_time_seconds") && j["center_reset"]["hold_time_seconds"].is_number())
                        center_reset_hold_time_seconds = j["center_reset"]["hold_time_seconds"].get<double>();
                }
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "[WARNING] Could not parse center_reset from settings.json: " << e.what() << std::endl;
            } catch (const nlohmann::json::type_error& e) {
                std::cerr << "[WARNING] Type error for center_reset in settings.json: " << e.what() << std::endl;
            }
        }
        std::cout << "[INFO] Center reset: window " << center_reset_window_degrees 
                  << " deg, hold time " << center_reset_hold_time_seconds << "s (relative to Oculus origin)" << std::endl;
    }

    struct AlarmState {
        bool warning_triggered = false;
        double noLookTime_ms = 0.0, repeat_timer_ms = 0.0, warning_start_time_ms = 0.0;
        bool looked_left_ever = false, looked_right_ever = false, looked_up_ever = false, looked_down_ever = false;
        double left_ever_time_ms = -1.0, right_ever_time_ms = -1.0;
        double alarm_silence_until_ms = 0.0; 
        sf::Music* sound_player = nullptr; 
        bool silence_message_printed_this_period = false; 
    };
    std::vector<AlarmState> alarm_states(alarms.size());

    while (IsWindow(g_hwnd)) {
        // Only check Condor log every LOG_CHECK_INTERVAL seconds
        log_check_timer_ms += POLL_INTERVAL * 1000.0;
        bool check_log_this_iteration = (log_check_timer_ms >= LOG_CHECK_INTERVAL * 1000.0);
        
        if (check_log_this_iteration) {
            log_check_timer_ms = 0.0; // Reset the flight check timer
            
            // --- Monitor Condor simulation window for flight status ---
            bool previous_iteration_flight_status = condor_flight_active;
            bool condor_sim_window_active = is_condor_simulation_window_active();
            
            // Simple window-based flight detection
            condor_flight_active = condor_sim_window_active;

        if (condor_flight_active != previous_iteration_flight_status) {
            if (condor_flight_active) {
                std::cout << "[INFO] Detected Condor flight start." << std::endl;
                // Automatically apply software recenter on flight start (capture current head position as forward)
                g_request_baseline_reset = true;
                std::cout << "[INFO] Auto-recentering to current head position for flight start" << std::endl;
            } else {
                std::cout << "[INFO] Detected Condor flight end. Resetting alarms." << std::endl;
                for (AlarmState& s : alarm_states) {
                    if (s.sound_player && s.sound_player->getStatus() == sf::SoundSource::Status::Playing) {
                        s.sound_player->stop();
                    }
                    s.warning_triggered = false;
                    s.noLookTime_ms = 0.0;
                    s.repeat_timer_ms = 0.0;
                    s.warning_start_time_ms = 0.0;
                    s.looked_left_ever = false; s.looked_right_ever = false; s.looked_up_ever = false; s.looked_down_ever = false;
                    s.left_ever_time_ms = -1.0; s.right_ever_time_ms = -1.0;
                    s.alarm_silence_until_ms = 0.0;
                    s.silence_message_printed_this_period = false;
                }
            }
        }
        } // End of log check block


        if (!condor_flight_active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000 * 5))); 
            elapsed_time_ms += POLL_INTERVAL * 1000.0 * 5; 
            continue;
        }

        double displayTime = ovr_GetPredictedDisplayTime(session, 0);
        ovrTrackingState ts = ovr_GetTrackingState(session, displayTime, ovrTrue);
        ovrSessionStatus sessionStatus;
        ovrResult session_status_result = ovr_GetSessionStatus(session, &sessionStatus);
        
        // Check for Oculus recenter trigger through ShouldRecenter flag
        static bool last_should_recenter = false;
        if (sessionStatus.ShouldRecenter && !last_should_recenter) {
            std::cout << "[INFO] Oculus recenter detected - triggering software recenter" << std::endl;
            g_request_baseline_reset = true;
        }
        last_should_recenter = sessionStatus.ShouldRecenter;

        // Handle baseline reference reset request
        if (g_request_baseline_reset && (ts.StatusFlags & ovrStatus_OrientationTracked)) {
            g_baseline_reference = ts.HeadPose.ThePose.Orientation;
            g_has_baseline_reference = true;
            g_request_baseline_reset = false;
            std::cout << "[INFO] Baseline reference captured - new forward direction set" << std::endl;
        }
        
        // Handle software recenter request
        if (g_request_software_recenter && (ts.StatusFlags & ovrStatus_OrientationTracked)) {
            // Calculate current yaw (rotation around Y axis)
            ovrQuatf currentOrientation = ts.HeadPose.ThePose.Orientation;
            
            // Extract yaw from quaternion (simplified for Y-axis rotation)
            float currentYaw = atan2(2.0f * (currentOrientation.w * currentOrientation.y + currentOrientation.x * currentOrientation.z),
                                   1.0f - 2.0f * (currentOrientation.y * currentOrientation.y + currentOrientation.z * currentOrientation.z));
            
            // Create offset quaternion to counter current yaw
            g_recenter_offset.x = 0;
            g_recenter_offset.y = sin(-currentYaw / 2.0f);
            g_recenter_offset.z = 0;
            g_recenter_offset.w = cos(-currentYaw / 2.0f);
            
            std::cout << "[INFO] Manual software recenter applied - yaw offset: " << (-currentYaw * 180.0f / M_PI) << " degrees" << std::endl;
            g_has_manual_recenter_offset = true;
            g_request_software_recenter = false;
        }
        
        
        // Check if session became invalid (actual API failure)
        if (OVR_FAILURE(session_status_result)) {
            std::cout << "[WARNING] HMD session lost. Attempting to reconnect..." << std::endl;
            
            // Try to recreate the session
            ovr_Destroy(session);
            g_ovr_session = nullptr;  // Clear global since session is destroyed
            ovrGraphicsLuid luid;
            ovrResult recreate_result = ovr_Create(&session, &luid);
            
            if (OVR_FAILURE(recreate_result)) {
                std::cout << "[INFO] HMD disconnected. Waiting for reconnection..." << std::endl;
                // Reset state and wait for HMD to come back
                for (AlarmState& s : alarm_states) {
                    if (s.sound_player && s.sound_player->getStatus() == sf::SoundSource::Status::Playing) {
                        s.sound_player->stop();
                    }
                    s.warning_triggered = false;
                    s.noLookTime_ms = 0.0;
                    s.repeat_timer_ms = 0.0;
                }
                
                // Wait and retry session creation
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                continue;
            } else {
                std::cout << "[INFO] HMD session restored successfully!" << std::endl;
                g_ovr_session = session;  // Update global for hotkey access
                // Re-get the tracking data with new session
                displayTime = ovr_GetPredictedDisplayTime(session, 0);
                ts = ovr_GetTrackingState(session, displayTime, ovrTrue);
                ovr_GetSessionStatus(session, &sessionStatus);
            }
        }

        static bool hmd_status_ok_previously = true; 
        bool hmd_currently_ok = (ts.StatusFlags & ovrStatus_OrientationTracked) &&
                                sessionStatus.HmdMounted &&
                                !sessionStatus.DisplayLost;

        if (!hmd_currently_ok) {
            if (hmd_status_ok_previously) { 
                std::cerr << "[WARNING] HMD not ready (Not tracked, not mounted, or display lost). Pausing alarms." << std::endl;
            }
            hmd_status_ok_previously = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000)));
            elapsed_time_ms += POLL_INTERVAL * 1000.0;
            continue;
        }
        if (!hmd_status_ok_previously) { 
             std::cout << "[INFO] HMD is now ready. Resuming alarms." << std::endl;
        }
        hmd_status_ok_previously = true; 

        ovrPosef pose = ts.HeadPose.ThePose;
        ovrQuatf q = pose.Orientation;
        double current_yaw_deg, current_pitch_deg;
        quat_to_yaw_pitch(q, current_yaw_deg, current_pitch_deg);

        double dyaw = current_yaw_deg;   
        double dpitch = current_pitch_deg;

        if (std::abs(dyaw) < center_reset_window_degrees && std::abs(dpitch) < center_reset_window_degrees) {
            center_hold_timer_seconds += POLL_INTERVAL;
            if (!center_reset_active && center_hold_timer_seconds >= center_reset_hold_time_seconds) {
                for (size_t i_reset = 0; i_reset < alarms.size(); ++i_reset) { 
                    if (alarms[i_reset].min_horizontal_angle <=0) continue; 
                    alarm_states[i_reset].looked_left_ever = false; alarm_states[i_reset].left_ever_time_ms = -1.0;
                    alarm_states[i_reset].looked_right_ever = false; alarm_states[i_reset].right_ever_time_ms = -1.0;
                    alarm_states[i_reset].looked_up_ever = false;
                    alarm_states[i_reset].looked_down_ever = false;
                }
                center_reset_active = true; 
                std::cout << "[INFO] Center Reset Triggered: All lookout direction flags reset (due to looking forward)." << std::endl;
            }
        } else {
            center_hold_timer_seconds = 0.0;
            center_reset_active = false; 
        }
        
        for (size_t i = 0; i < alarms.size(); ++i) {
            AlarmState& state = alarm_states[i];
            const LookoutAlarmConfig& config = alarms[i];
            if (config.min_horizontal_angle <= 0) continue; 

            bool currently_looking_left = dyaw > (config.min_horizontal_angle / 2.0);
            bool currently_looking_right = dyaw < -(config.min_horizontal_angle / 2.0);
            bool currently_looking_up = dpitch > config.min_vertical_angle_up;
            bool currently_looking_down = dpitch < -config.min_vertical_angle_down;

            bool new_lr_look_this_tick = false;
            if (currently_looking_left && !state.looked_left_ever) { 
                state.looked_left_ever = true; state.left_ever_time_ms = elapsed_time_ms; new_lr_look_this_tick = true;
                std::cout << "[DEBUG] Alarm " << i << ": L registered." << std::endl;
            }
            if (currently_looking_right && !state.looked_right_ever) {
                state.looked_right_ever = true; state.right_ever_time_ms = elapsed_time_ms; new_lr_look_this_tick = true;
                std::cout << "[DEBUG] Alarm " << i << ": R registered." << std::endl;
            }
            if (currently_looking_up && !state.looked_up_ever) { 
                state.looked_up_ever = true;
                 std::cout << "[DEBUG] Alarm " << i << ": U registered." << std::endl;
            }
            if (currently_looking_down && !state.looked_down_ever) { 
                state.looked_down_ever = true;
                std::cout << "[DEBUG] Alarm " << i << ": D registered." << std::endl;
            }
            
            static double last_periodic_state_dump_time_ms = 0.0; 
            if (elapsed_time_ms - last_periodic_state_dump_time_ms >= 5000.0) { 
                 std::cout << std::fixed << std::setprecision(1) 
                           << "[STATE] Alarm " << i 
                           << ": HMD_Yaw: " << dyaw << ", HMD_Pitch: " << dpitch
                           << " | L:" << state.looked_left_ever << "(" << state.left_ever_time_ms/1000.0 << "s)" 
                           << " R:" << state.looked_right_ever << "(" << state.right_ever_time_ms/1000.0 << "s)"
                           << " U:" << state.looked_up_ever << " D:" << state.looked_down_ever
                           << " | noLook: " << state.noLookTime_ms / 1000.0 << "s / " << config.max_time_ms / 1000.0 << "s"
                           << " | warn: " << state.warning_triggered
                           << " | rptTmr: " << state.repeat_timer_ms/1000.0 << "s/" << config.repeat_interval_ms/1000.0 << "s"
                           << " | silenceRem: " << std::max(0.0, (state.alarm_silence_until_ms - elapsed_time_ms)/1000.0) << "s"
                           << std::endl;
                if (i == alarms.size() - 1) last_periodic_state_dump_time_ms = elapsed_time_ms; 
            }

            state.noLookTime_ms += POLL_INTERVAL * 1000.0;
            if(state.warning_triggered) {
                state.repeat_timer_ms += POLL_INTERVAL * 1000.0;
            }
            
            if (state.looked_left_ever && state.looked_right_ever && state.looked_up_ever && state.looked_down_ever) {
                double lr_time_diff_ms = std::abs(state.left_ever_time_ms - state.right_ever_time_ms); 
                if (lr_time_diff_ms >= config.min_lookout_time_ms) { 
                    state.noLookTime_ms = 0.0; state.warning_triggered = false; state.repeat_timer_ms = 0.0;
                    state.looked_left_ever = false; state.left_ever_time_ms = -1.0;
                    state.looked_right_ever = false; state.right_ever_time_ms = -1.0;
                    state.looked_up_ever = false; state.looked_down_ever = false;
                    state.silence_message_printed_this_period = false; state.alarm_silence_until_ms = 0; 
                    if (state.sound_player) { state.sound_player->stop(); }
                    std::cout << "[INFO] Alarm " << i << ": Lookout successful. L/R diff: " << lr_time_diff_ms << " ms. Reset." << std::endl;
                    
                    if (widest_alarm_valid && i == widest_alarm_idx) { 
                        for (size_t j = 0; j < alarms.size(); ++j) {
                             if (alarms[j].min_horizontal_angle <= 0) continue; 
                            if (j != i && alarms[j].min_horizontal_angle < config.min_horizontal_angle) { 
                                alarm_states[j].noLookTime_ms = 0.0; alarm_states[j].warning_triggered = false; alarm_states[j].repeat_timer_ms = 0.0;
                                alarm_states[j].looked_left_ever = false; alarm_states[j].left_ever_time_ms = -1.0;
                                alarm_states[j].looked_right_ever = false; alarm_states[j].right_ever_time_ms = -1.0;
                                alarm_states[j].looked_up_ever = false; alarm_states[j].looked_down_ever = false;
                                alarm_states[j].silence_message_printed_this_period = false; alarm_states[j].alarm_silence_until_ms = 0;
                                if (alarm_states[j].sound_player) { alarm_states[j].sound_player->stop(); }
                                std::cout << "[INFO] Alarm " << i << " (widest) success: Resetting narrower alarm " << j << "." << std::endl;
                            }
                        }
                    }
                    continue; 
                } else { 
                    std::cout << "[DEBUG] Alarm " << i << ": All dirs seen, but L/R diff " << lr_time_diff_ms 
                              << " ms < " << config.min_lookout_time_ms << " ms. Resetting L/R flags only." << std::endl;
                    state.looked_left_ever = false; state.left_ever_time_ms = -1.0;
                    state.looked_right_ever = false; state.right_ever_time_ms = -1.0;
                }
            }
            
            if (new_lr_look_this_tick) { 
                state.alarm_silence_until_ms = elapsed_time_ms + config.silence_after_look_ms;
                std::cout << "[DEBUG] Alarm " << i << ": New L/R look. Silencing warnings for " << config.silence_after_look_ms << " ms." << std::endl;
                if (state.warning_triggered && state.sound_player) { 
                     state.sound_player->setVolume(0);
                     if (!state.silence_message_printed_this_period) {
                        std::cout << "[DEBUG] Alarm " << i << ": Warning active, volume immediately silenced due to new L/R look." << std::endl;
                        state.silence_message_printed_this_period = true;
                     }
                }
            }
            
            if (!state.warning_triggered && state.noLookTime_ms >= config.max_time_ms) {
                if (elapsed_time_ms < state.alarm_silence_until_ms) { 
                    if (!state.silence_message_printed_this_period) { 
                        std::cout << "[DEBUG] Alarm " << i << ": Max no-look time reached, but alarm is silenced. Skipping warning." << std::endl;
                        state.silence_message_printed_this_period = true; 
                    }
                    continue; 
                } else { 
                    state.silence_message_printed_this_period = false; 
                    state.warning_triggered = true;
                    state.repeat_timer_ms = 0.0; 
                    state.warning_start_time_ms = elapsed_time_ms;

                    state.looked_left_ever = false; state.left_ever_time_ms = -1.0; 
                    state.looked_right_ever = false; state.right_ever_time_ms = -1.0;
                    state.looked_up_ever = false; state.looked_down_ever = false;
                    std::cout << "[DEBUG] Alarm " << i << ": Lookout direction flags reset as warning triggers." << std::endl;
                    
                    if(state.sound_player) { 
                        try {
                            state.sound_player->stop();
                        } catch (...) {
                            std::cerr << "[WARNING] Exception stopping previous sound player" << std::endl;
                        }
                        delete state.sound_player; 
                        state.sound_player = nullptr;
                    }
                    
                    state.sound_player = get_or_create_sound_player(config.audio_file);

                    if (state.sound_player) {
                        try {
                            int cur_volume = config.start_volume;
                            state.sound_player->setVolume(static_cast<float>(cur_volume));
                            state.sound_player->play();
                            std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! Vol: " << cur_volume << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "[ERROR] Alarm " << i << ": Exception playing audio: " << e.what() << std::endl;
                            delete state.sound_player;
                            state.sound_player = nullptr;
                        } catch (...) {
                            std::cerr << "[ERROR] Alarm " << i << ": Unknown exception playing audio" << std::endl;
                            delete state.sound_player;
                            state.sound_player = nullptr;
                        }
                    } else {
                        std::cerr << "[ERROR] Alarm " << i << ": Failed to create sound player for warning." << std::endl;
                    }
                }
            } 
            else if (state.warning_triggered) { 
                double ramp_elapsed_ms = elapsed_time_ms - state.warning_start_time_ms;
                int target_volume = config.start_volume;
                if (config.volume_ramp_time_ms > 0 && config.end_volume != config.start_volume) {
                    double ramp_progress = std::min(1.0, ramp_elapsed_ms / static_cast<double>(config.volume_ramp_time_ms));
                    target_volume = static_cast<int>(config.start_volume + ramp_progress * (config.end_volume - config.start_volume));
                } else {
                    target_volume = config.end_volume; 
                }

                if (state.sound_player) {
                    if (elapsed_time_ms < state.alarm_silence_until_ms) { 
                        state.sound_player->setVolume(0);
                        if (!state.silence_message_printed_this_period) {
                            std::cout << "[DEBUG] Alarm " << i << ": Warning active. Volume silenced due to recent look." << std::endl;
                            state.silence_message_printed_this_period = true;
                        }
                    } else { 
                        if (state.silence_message_printed_this_period) { 
                            std::cout << "[DEBUG] Alarm " << i << ": Silence period ended for active warning. Restoring volume." << std::endl;
                            state.silence_message_printed_this_period = false; 
                        }
                        state.sound_player->setVolume(static_cast<float>(target_volume));
                        
                        if (state.repeat_timer_ms >= config.repeat_interval_ms) {
                            state.sound_player->stop(); 
                            state.sound_player->play(); 
                            std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! (Repeat sound) Vol: " << target_volume << std::endl;
                            state.repeat_timer_ms = 0.0; 
                        }
                    }
                } else { 
                    if (state.repeat_timer_ms >= config.repeat_interval_ms) { 
                        std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! (Repeat reminder - NO SOUND PLAYER)" << std::endl;
                        state.repeat_timer_ms = 0.0;
                    }
                }
            } 
        } 

        elapsed_time_ms += POLL_INTERVAL * 1000.0; 
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000)));
    } 

    std::cout << "[INFO] Main loop in app_core_logic exited (window closed)." << std::endl;

    for(auto& state : alarm_states) {
        if(state.sound_player) {
            state.sound_player->stop();
            delete state.sound_player;
            state.sound_player = nullptr;
        }
    }

    ovr_Destroy(session);
    ovr_Shutdown();
    std::cout << "[INFO] Oculus SDK shutdown. app_core_logic finished." << std::endl;
    return 0;
}