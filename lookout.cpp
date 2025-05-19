// lookout.cpp
// Oculus Lookout Utility (C++)
// Requires Oculus SDK (OVR)

#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>
#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POLL_INTERVAL 0.05 
#include <fstream>
#include "json.hpp" 
#include <SFML/Audio.hpp>
#include <windows.h>
#include <shellapi.h> // For Shell_NotifyIcon
#include <thread>       // For std::thread
#include <cstdio>       // For _wfreopen_s, FILE 
#include <SFML/System/Time.hpp>
#include <SFML/System/FileInputStream.hpp>

#include <unordered_map>
#include <memory>
#include <vector>
#include <deque>
#include <algorithm>
#include <iomanip> 


struct LookoutAlarmConfig {
    double min_horizontal_angle = 120.0; 
    double min_vertical_angle = 20.0;   
    int max_time_ms = 60000;             
    std::string audio_file;             
    int start_volume = 5;              
    int end_volume = 100;               
    int volume_ramp_time_ms = 30000;     
    int repeat_interval_ms = 5000;      
    int min_lookout_time_ms = 2000;     
    int silence_after_look_ms = 5000;   

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LookoutAlarmConfig,
        min_horizontal_angle, min_vertical_angle, max_time_ms, audio_file,
        start_volume, end_volume, volume_ramp_time_ms, repeat_interval_ms, min_lookout_time_ms,
        silence_after_look_ms)
};

inline double clamp_angle(double angle_deg) {
    while (angle_deg > 180.0) angle_deg -= 360.0;
    while (angle_deg < -180.0) angle_deg += 360.0;
    return angle_deg;
}

inline double rad2deg(double rad) { return rad * 180.0 / M_PI; }

void quat_to_yaw_pitch(const ovrQuatf& q, double& yaw_deg, double& pitch_deg) {
    double ys = 2.0 * (q.w * q.y + q.x * q.z);
    double yc = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    double ps = 2.0 * (q.w * q.x - q.z * q.y);
    ps = (std::max)((-1.0), (std::min)(1.0, ps)); 
    yaw_deg = rad2deg(std::atan2(ys, yc));
    pitch_deg = rad2deg(std::asin(ps));
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

sf::Music* get_or_create_sound_player(const std::string& audio_file) {
    std::string file_to_play = audio_file.empty() ? "beep.wav" : audio_file;
    auto music = std::make_unique<sf::Music>();
    if (!music->openFromFile(file_to_play)) {
        std::cerr << "[ERROR] Could not load music file: " << file_to_play << std::endl;
        if (file_to_play != "beep.wav") {
            std::cerr << "[INFO] Attempting to load default beep.wav" << std::endl;
            if (!music->openFromFile("beep.wav")) {
                std::cerr << "[ERROR] Could not load default music file: beep.wav" << std::endl;
                return nullptr;
            }
            file_to_play = "beep.wav";
        } else {
            return nullptr;
        }
    }
    // sf::Music must persist for the duration of playback, so we leak on purpose for alarm lifetime
    return music.release();
}

// Defines for tray icon
#define WM_APP_TRAYMSG (WM_APP + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM 1002
#define ID_TRAY_TOGGLE_CONSOLE_ITEM 1003

const char* const WINDOW_CLASS_NAME = "QuestLookoutWindowClass";
HWND g_hwnd;
NOTIFYICONDATA nidApp;
bool g_is_console_visible = false;

// Forward declaration for our core application logic
int app_core_logic(); // Signature changed for threading

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
                // Handle error - perhaps show a MessageBox
                FreeConsole(); // Clean up if redirection failed
                return;
            }
            // Clear the C++ stream states so they pick up the new C file descriptors
            std::cout.clear();
            std::cerr.clear();
            SetConsoleTitleA("Quest Lookout Status"); // Use A version for ANSI string
            g_is_console_visible = true;
            std::cout << "[INFO] Status window opened." << std::endl; // Initial message to the new console
        }
    }
}

void HideConsoleWindow()
{
    if (g_is_console_visible)
    {
        FILE* fp_nul_stdout, *fp_nul_stderr, *fp_nul_stdin;
        // Redirect streams to NUL before freeing console to prevent issues
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
            switch (lParam) // The lParam holds the mouse event for tray icons
            {
                case WM_RBUTTONUP: // Right-click
                {
                    POINT curPoint;
                    GetCursorPos(&curPoint);
                    HMENU hPopupMenu = CreatePopupMenu();
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, 
                               ID_TRAY_TOGGLE_CONSOLE_ITEM, // Argument 4: UINT_PTR uIDNewItem (Menu Item ID)
                               g_is_console_visible ? "Hide Status Window" : "Show Status Window"); // Argument 5: LPCSTR lpNewItem (Menu Item String)
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_SEPARATOR, 0, NULL); // Separator
                    InsertMenu(hPopupMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, "Exit");
                    
                    // --- Fix for persistent context menu ---
                    SetForegroundWindow(hwnd); // Make our window the foreground window
                    // ----------------------------------------

                    TrackPopupMenu(hPopupMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, curPoint.x, curPoint.y, 0, hwnd, NULL);
                    DestroyMenu(hPopupMenu);

                    // --- Fix for persistent context menu (part 2) ---
                    PostMessage(hwnd, WM_NULL, 0, 0); // Post a dummy message to ensure the menu closes properly
                    // ---------------------------------------------
                }
                return 0;
                // case WM_LBUTTONDBLCLK: // Double-click (for future use, e.g., show settings/log)
                // ShowWindow(hwnd, SW_RESTORE); // Example: if we had a visible window
                // break;
            }
            break;

        case WM_COMMAND: // Sent when a menu item is clicked
            switch (LOWORD(wParam))
            {
                case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
                    DestroyWindow(hwnd); // This will trigger WM_DESTROY
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
            if (g_is_console_visible) HideConsoleWindow(); // Ensure console is closed if app exits
            Shell_NotifyIcon(NIM_DELETE, &nidApp); // Remove icon from tray
            PostQuitMessage(0); // Signals the main message loop to exit
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Windows Subsystem Entry Point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine); // lpCmdLine might be used for app arguments

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION); // Default icon
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    // wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); // Not needed for an invisible window

    if (!RegisterClassEx(&wc))
    {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    g_hwnd = CreateWindowEx(
        0,                              // Optional window styles.
        WINDOW_CLASS_NAME,              // Window class
        "Quest Lookout Hidden Window",  // Window text (not visible)
        0,                              // Window style (not visible)
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, // Size and position (not relevant for hidden)
        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (g_hwnd == NULL)
    {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Setup the tray icon
    nidApp.cbSize = sizeof(NOTIFYICONDATA);
    nidApp.hWnd = g_hwnd;
    nidApp.uID = ID_TRAY_APP_ICON;
    nidApp.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nidApp.uCallbackMessage = WM_APP_TRAYMSG;
    nidApp.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Use a default icon for now
    strcpy_s(nidApp.szTip, "Quest Lookout"); // Tooltip text

    Shell_NotifyIcon(NIM_ADD, &nidApp);

    // Launch the core application logic in a separate thread
    std::thread core_logic_thread(app_core_logic);
    core_logic_thread.detach(); // Detach the thread to run independently

    // Run the message loop.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) // GetMessage returns > 0 for messages other than WM_QUIT
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

// Renamed original main function, now app_core_logic
int app_core_logic() // Signature updated for threading
{
    ovrResult result = ovr_Initialize(nullptr);
    if (OVR_FAILURE(result)) { std::cerr << "OVR Init Fail" << std::endl; return 1; }

    ovrSession session;
    ovrGraphicsLuid luid;
    result = ovr_Create(&session, &luid);
    if (OVR_FAILURE(result)) { std::cerr << "OVR Create Fail" << std::endl; ovr_Shutdown(); return 1; }

    std::vector<LookoutAlarmConfig> alarms = load_configs("settings.json");
    if (alarms.empty()) { std::cerr << "No Alarms Loaded" << std::endl; ovr_Destroy(session); ovr_Shutdown(); return 1; }

    // --- Condor log monitoring setup ---
    std::string condor_log_path = "C:\\Condor3\\Logs\\Logfile.txt";
    {
        std::ifstream f("settings.json");
        if (f) {
            nlohmann::json j;
            try { f >> j; } catch (...) {}
            if (j.contains("condor_log_path")) {
                condor_log_path = j["condor_log_path"].get<std::string>();
            }
        }
    }
    std::cout << "[INFO] Monitoring Condor log at: " << condor_log_path << std::endl;

    auto is_flight_active = [](const std::string& log_path) -> bool {
        std::ifstream log(log_path, std::ios::in | std::ios::binary); // Use binary mode
        if (!log) return false;
        std::string line;
        std::string last_relevant_event_type;
        while (std::getline(log, line)) {
            if (line.find("ENTERED SIMULATION AT") != std::string::npos) {
                last_relevant_event_type = "ENTERED";
            } else if (line.find("LEAVING SIMULATION AT") != std::string::npos) {
                last_relevant_event_type = "LEAVING";
            }
        }
        return last_relevant_event_type == "ENTERED";
    };
    bool condor_flight_active = is_flight_active(condor_log_path);
    std::streampos last_log_pos = 0;
    {
        std::ifstream log_check(condor_log_path, std::ios::in | std::ios::ate | std::ios::binary); // Use binary mode
        if (log_check) last_log_pos = log_check.tellg();
    }
    
    std::cout << "Oculus Lookout Utility started. Press Ctrl+C to quit." << std::endl;
    for (size_t i = 0; i < alarms.size(); ++i) { 
        if (alarms[i].repeat_interval_ms < 100) alarms[i].repeat_interval_ms = 5000;
        if (alarms[i].min_horizontal_angle <= 0 || alarms[i].min_vertical_angle <=0) alarms[i].min_horizontal_angle = -1; 
        if (alarms[i].min_horizontal_angle > 0) { 
            std::cout << "[INFO] Alarm " << i << ": HAngle=" << alarms[i].min_horizontal_angle 
                    << ", VAngle=" << alarms[i].min_vertical_angle 
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
            std::cerr << "[ERROR] No valid alarms configured for widest_alarm_idx logic." << std::endl;
        }
    }

    const size_t WINDOW_SIZE = static_cast<size_t>(30.0 / POLL_INTERVAL); 
    std::deque<double> yaw_window, pitch_window;
    double elapsed_time_ms = 0.0; 

    double center_window_degrees = 20.0;
    double center_hold_time_seconds = 3.0;
    double center_hold_timer_seconds = 0.0; 
    bool center_reset_active = false;
    {
        std::ifstream f("settings.json");
        if (f) {
            nlohmann::json j;
             try { f >> j; } catch (...) {}
            if (j.contains("center_reset")) {
                if (j["center_reset"].contains("window_degrees"))
                    center_window_degrees = j["center_reset"]["window_degrees"].get<double>();
                if (j["center_reset"].contains("hold_time_seconds"))
                    center_hold_time_seconds = j["center_reset"]["hold_time_seconds"].get<double>();
            }
        }
        std::cout << "[INFO] Center reset window: " << center_window_degrees << " deg, hold time: " << center_hold_time_seconds << "s" << std::endl;
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

    while (true) {
        // --- Monitor Condor log for flight status ---
        bool previous_iteration_flight_status = condor_flight_active;

        std::ifstream log_stream(condor_log_path, std::ios::in | std::ios::ate | std::ios::binary);

        if (log_stream) {
            std::streampos current_file_size = log_stream.tellg();

            if (current_file_size < last_log_pos) { // Log was truncated or rotated
                last_log_pos = 0; // Reset to trigger a full re-scan
            }

            std::string latest_event_type_found_in_scan = ""; // "ENTERED" or "LEAVING"

            if (last_log_pos == 0 && current_file_size > 0) { // Need to scan the whole file
                log_stream.clear(); 
                log_stream.seekg(0, std::ios::beg);
                std::string line;
                while (std::getline(log_stream, line)) {
                    if (line.find("ENTERED SIMULATION AT") != std::string::npos) {
                        latest_event_type_found_in_scan = "ENTERED";
                    } else if (line.find("LEAVING SIMULATION AT") != std::string::npos) {
                        latest_event_type_found_in_scan = "LEAVING";
                    }
                }
            } else if (current_file_size > last_log_pos) { // New content added
                log_stream.clear(); 
                log_stream.seekg(last_log_pos, std::ios::beg);
                std::string line;
                while (std::getline(log_stream, line)) { // Read only new lines
                    if (line.find("ENTERED SIMULATION AT") != std::string::npos) {
                        latest_event_type_found_in_scan = "ENTERED";
                    } else if (line.find("LEAVING SIMULATION AT") != std::string::npos) {
                        latest_event_type_found_in_scan = "LEAVING";
                    }
                }
            }

            // Update condor_flight_active based on what was found
            if (!latest_event_type_found_in_scan.empty()) {
                if (latest_event_type_found_in_scan == "ENTERED") {
                    condor_flight_active = true;
                } else if (latest_event_type_found_in_scan == "LEAVING") {
                    condor_flight_active = false;
                }
            } else {
                // If a full scan (last_log_pos was 0) yielded no relevant events, it means inactive.
                // Or if the file became empty.
                if ((last_log_pos == 0 && current_file_size > 0) || current_file_size == 0) {
                     condor_flight_active = false;
                }
                // If only new lines were scanned and they had no relevant events, condor_flight_active remains unchanged from previous state.
            }
            last_log_pos = current_file_size; // Update for the next iteration

        } else { // Log file not accessible
            if (condor_flight_active) { // If it was active and now log is gone
                condor_flight_active = false;
            }
        }

        // After determining the current state, compare with the state from the start of this iteration
        if (condor_flight_active != previous_iteration_flight_status) {
            if (condor_flight_active) {
                std::cout << "[INFO] Detected Condor flight start." << std::endl;
            } else {
                std::cout << "[INFO] Detected Condor flight end." << std::endl;
                // Reset all alarm states as the flight has ended
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


        // --- Only run detection/alarms if flight is active and headset tracked ---
        if (!condor_flight_active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000)));
            elapsed_time_ms += POLL_INTERVAL * 1000.0;
            continue;
        }

        double displayTime = ovr_GetPredictedDisplayTime(session, 0);
        ovrTrackingState ts = ovr_GetTrackingState(session, displayTime, ovrTrue);
        ovrSessionStatus sessionStatus;
        ovr_GetSessionStatus(session, &sessionStatus);

        static bool hmd_status_ok_previously = true; 
        bool hmd_currently_ok = (ts.StatusFlags & ovrStatus_OrientationTracked) &&
                                /* (ts.StatusFlags & ovrStatus_HmdConnected) && // Removed for compatibility with older SDKs */
                                sessionStatus.HmdMounted &&
                                !sessionStatus.DisplayLost;

        if (!hmd_currently_ok) {
            if (hmd_status_ok_previously) { // Only print if status just changed to not OK
                std::cerr << "[WARNING] HMD not ready (not tracked/connected, not mounted, or display lost). Pausing alarms." << std::endl;
            }
            hmd_status_ok_previously = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000)));
            elapsed_time_ms += POLL_INTERVAL * 1000.0;
            continue;
        }
        if (!hmd_status_ok_previously) { // HMD has become OK now
             std::cout << "[INFO] HMD is now ready. Resuming alarms." << std::endl;
        }
        hmd_status_ok_previously = true; 
        ovrPosef pose = ts.HeadPose.ThePose;
        ovrQuatf q = pose.Orientation;
        double yaw_deg, pitch_deg;
        quat_to_yaw_pitch(q, yaw_deg, pitch_deg);

        double center_yaw = yaw_deg, center_pitch = pitch_deg;
        if (!yaw_window.empty()) { 
            std::vector<double> sorted_yaw(yaw_window.begin(), yaw_window.end());
            std::sort(sorted_yaw.begin(), sorted_yaw.end());
            if (!sorted_yaw.empty()) center_yaw = sorted_yaw[sorted_yaw.size() / 2];
            
            std::vector<double> sorted_pitch(pitch_window.begin(), pitch_window.end());
            std::sort(sorted_pitch.begin(), sorted_pitch.end());
            if(!sorted_pitch.empty()) center_pitch = sorted_pitch[sorted_pitch.size() / 2];
        }

        double dyaw = clamp_angle(yaw_deg - center_yaw);
        double dpitch = clamp_angle(pitch_deg - center_pitch);

        if (std::abs(dyaw) < center_window_degrees && std::abs(dpitch) < center_window_degrees) {
            center_hold_timer_seconds += POLL_INTERVAL;
            if (!center_reset_active && center_hold_timer_seconds >= center_hold_time_seconds) {
                for (size_t i_reset = 0; i_reset < alarms.size(); ++i_reset) { 
                    if (alarms[i_reset].min_horizontal_angle <=0) continue; 
                    alarm_states[i_reset].looked_left_ever = false; alarm_states[i_reset].left_ever_time_ms = -1.0;
                    alarm_states[i_reset].looked_right_ever = false; alarm_states[i_reset].right_ever_time_ms = -1.0;
                    alarm_states[i_reset].looked_up_ever = false;
                    alarm_states[i_reset].looked_down_ever = false;
                }
                center_reset_active = true; 
                std::cout << "[DEBUG] Center reset: All lookout flags reset." << std::endl;
            }
        } else {
            center_hold_timer_seconds = 0.0;
            center_reset_active = false; 
        }
        
        yaw_window.push_back(yaw_deg); pitch_window.push_back(pitch_deg);
        if (yaw_window.size() > WINDOW_SIZE) yaw_window.pop_front();
        if (pitch_window.size() > WINDOW_SIZE) pitch_window.pop_front();

        for (size_t i = 0; i < alarms.size(); ++i) {
            AlarmState& state = alarm_states[i];
            const LookoutAlarmConfig& config = alarms[i];
            if (config.min_horizontal_angle <= 0) continue; 

            // This is the L/R logic from the "working" code you provided.
            // dyaw > thresh is Left, dyaw < -thresh is Right.
            bool currently_looking_left = dyaw > (config.min_horizontal_angle / 2.0);
            bool currently_looking_right = dyaw < -(config.min_horizontal_angle / 2.0);
            bool currently_looking_up = dpitch > (config.min_vertical_angle / 2.0);
            bool currently_looking_down = dpitch < -(config.min_vertical_angle / 2.0);

            bool new_lr_look_this_tick = false;
            if (currently_looking_left && !state.looked_left_ever) { 
                state.looked_left_ever = true; state.left_ever_time_ms = elapsed_time_ms; new_lr_look_this_tick = true;
                std::cout << "[DEBUG] Alarm " << i << ": L registered." << std::endl;
            }
            if (currently_looking_right && !state.looked_right_ever) {
                state.looked_right_ever = true; state.right_ever_time_ms = elapsed_time_ms; new_lr_look_this_tick = true;
                std::cout << "[DEBUG] Alarm " << i << ": R registered." << std::endl;
            }
            if (currently_looking_up) state.looked_up_ever = true;
            if (currently_looking_down) state.looked_down_ever = true;
            
            static double last_periodic_state_dump_time_ms = 0.0; 
            if (elapsed_time_ms - last_periodic_state_dump_time_ms >= 1000.0) { 
                 std::cout << std::fixed << std::setprecision(1) 
                           << "[STATE] Alarm " << i 
                           << ": dyaw: " << dyaw << ", dpitch: " << dpitch                 // DEBUG INFO ADDED
                           << " | C(y,p): (" << center_yaw << "," << center_pitch << ")"   // DEBUG INFO ADDED
                           << " | L:" << state.looked_left_ever << "(" << state.left_ever_time_ms/1000.0 << "s)" 
                           << " R:" << state.looked_right_ever << "(" << state.right_ever_time_ms/1000.0 << "s)"
                           << " U:" << state.looked_up_ever << " D:" << state.looked_down_ever
                           << " | noLook: " << state.noLookTime_ms / 1000.0 << "s"
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
                    std::cout << "[DEBUG] Alarm " << i << ": Lookout successful. L/R diff: " << lr_time_diff_ms << " ms. Reset." << std::endl;
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
                                std::cout << "[DEBUG] Alarm " << i << " (widest): Resetting narrower alarm " << j << "." << std::endl;
                            }
                        }
                    }
                    continue; 
                } else { 
                    std::cout << "[DEBUG] Alarm " << i << ": All dirs seen, but L/R diff " << lr_time_diff_ms 
                              << " ms < " << config.min_lookout_time_ms << " ms. Resetting L/R." << std::endl;
                    state.looked_left_ever = false; state.left_ever_time_ms = -1.0;
                    state.looked_right_ever = false; state.right_ever_time_ms = -1.0;
                }
            }
            
            if (new_lr_look_this_tick) { 
                state.alarm_silence_until_ms = elapsed_time_ms + config.silence_after_look_ms;
                std::cout << "[DEBUG] Alarm " << i << ": New L/R look. Silencing for " << config.silence_after_look_ms << " ms." << std::endl;
                state.silence_message_printed_this_period = true; 
            }
            
            if (!state.warning_triggered && state.noLookTime_ms >= config.max_time_ms) {
                if (elapsed_time_ms < state.alarm_silence_until_ms) { 
                    if (!state.silence_message_printed_this_period) { 
                        std::cout << "[DEBUG] Alarm " << i << ": Pre-warning. Max time, but silenced. Skipping." << std::endl;
                        state.silence_message_printed_this_period = true;
                    }
                    if (new_lr_look_this_tick) { 
                        state.noLookTime_ms = 0; 
                        std::cout << "[DEBUG] Alarm " << i << ": Granting noLookTime reprieve." << std::endl;
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
                    std::cout << "[DEBUG] Alarm " << i << ": Lookout flags reset as warning triggers." << std::endl;
                    
                    if(state.sound_player && state.sound_player->getStatus() != sf::SoundSource::Status::Stopped) state.sound_player->stop();
                    if (state.sound_player) { delete state.sound_player; state.sound_player = nullptr; } // Delete old before creating new
                    state.sound_player = get_or_create_sound_player(config.audio_file);

                    if (state.sound_player) {
                        int cur_volume = config.start_volume;
                        state.sound_player->setVolume(static_cast<float>(cur_volume));
                        state.sound_player->play();
                        std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! Vol: " << cur_volume << std::endl;
                    }
                }
            } 
            else if (state.warning_triggered) {
                double ramp_elapsed_ms = elapsed_time_ms - state.warning_start_time_ms;
                int target_volume = config.start_volume;
                if (config.volume_ramp_time_ms > 0 && config.end_volume != config.start_volume) {
                    double ramp_progress = std::min(1.0, ramp_elapsed_ms / static_cast<double>(config.volume_ramp_time_ms));
                    target_volume = static_cast<int>(config.start_volume + ramp_progress * (config.end_volume - config.start_volume));
                }

                if (state.repeat_timer_ms >= config.repeat_interval_ms) {
    if (state.sound_player) {
        state.sound_player->stop();
        delete state.sound_player;
        state.sound_player = nullptr;
    }
    state.sound_player = get_or_create_sound_player(config.audio_file);
    if (state.sound_player) {
        state.sound_player->setVolume(static_cast<float>(target_volume));
        state.sound_player->play();
        std::cout << "[DEBUG] Alarm " << i << ": Sound player force-restarted for active warning (repeat)." << std::endl;
    }
    state.repeat_timer_ms = 0.0;
}

                if (state.sound_player) {
                    if (elapsed_time_ms < state.alarm_silence_until_ms) { 
                        state.sound_player->setVolume(0);
                        if (!state.silence_message_printed_this_period) {
                            std::cout << "[DEBUG] Alarm " << i << ": Warning active. Volume silenced." << std::endl;
                            state.silence_message_printed_this_period = true;
                        }
                    } else { 
                        if (state.silence_message_printed_this_period) { 
                            std::cout << "[DEBUG] Alarm " << i << ": Silence period ended. Restoring volume." << std::endl;
                            state.silence_message_printed_this_period = false;
                        }
                        state.sound_player->setVolume(static_cast<float>(target_volume));
                        
                        if (state.repeat_timer_ms >= config.repeat_interval_ms) {
                            std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! (repeat reminder) Vol: " << target_volume << std::endl;
                            state.repeat_timer_ms = 0.0; 
                            if (state.sound_player->getStatus() != sf::SoundSource::Status::Playing) {
                                state.sound_player->play(); 
                            }
                        }
                    }
                } else { 
                    if (state.repeat_timer_ms >= config.repeat_interval_ms) { 
                        std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! (repeat reminder - NO SOUND PLAYER)" << std::endl;
                        state.repeat_timer_ms = 0.0;
                    }
                }
            } 
        } 

        elapsed_time_ms += POLL_INTERVAL * 1000.0; 
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000)));
    } 

    // Cleanup sf::Music objects
    for(auto& state : alarm_states) {
        if(state.sound_player) {
            state.sound_player->stop();
            delete state.sound_player;
            state.sound_player = nullptr;
        }
    }

    ovr_Destroy(session);
    ovr_Shutdown();
    return 0;
}
// Ensure there's no other 'main' function defined elsewhere by mistake.