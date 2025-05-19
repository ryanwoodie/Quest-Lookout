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

#define LOOK_THRESHOLD (20.0 * M_PI / 180.0) // 20 degrees in radians
#define WARNING_TIME 2.0 // seconds
#define POLL_INTERVAL 0.1 // seconds
#include <fstream>
#include "json.hpp" // For JSON config (requires nlohmann/json single header)
#include <SFML/Audio.hpp> // For audio playback with volume control

struct LookoutAlarmConfig {
    double min_horizontal_angle = 120.0; // degrees (±30)
    double min_vertical_angle = 20.0;   // degrees (±10)
    int max_time_ms = 60000;             // ms
    std::string audio_file;             // Path to audio file (empty = beep)
    int start_volume = 5;              // 0-100
    int end_volume = 100;               // 0-100
    int volume_ramp_time_ms = 30000;     // ms
    int repeat_interval_ms = 5000;      // ms
    int min_lookout_time_ms = 2000;     // Minimum time to hold lookout (ms)
    int silence_after_look_ms = 5000;   // ms to silence alarm after L or R cleared

    // Serialize/deserialize with nlohmann::json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LookoutAlarmConfig,
        min_horizontal_angle, min_vertical_angle, max_time_ms, audio_file,
        start_volume, end_volume, volume_ramp_time_ms, repeat_interval_ms, min_lookout_time_ms,
        silence_after_look_ms)
};

// Helper: Clamp angle to [-180, 180] degrees
inline double clamp_angle(double angle_deg) {
    while (angle_deg > 180.0) angle_deg -= 360.0;
    while (angle_deg < -180.0) angle_deg += 360.0;
    return angle_deg;
}

// Helper: Convert radians to degrees
inline double rad2deg(double rad) { return rad * 180.0 / M_PI; }

// Helper: Compute yaw/pitch from quaternion
void quat_to_yaw_pitch(const ovrQuatf& q, double& yaw_deg, double& pitch_deg) {
    // Yaw (Y axis), Pitch (X axis)
    double ys = 2.0 * (q.w * q.y + q.x * q.z);
    double yc = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    double ps = 2.0 * (q.w * q.x - q.z * q.y);
    // Clamp pitch to [-1, 1] to avoid NaN
    ps = (std::max)((-1.0), (std::min)(1.0, ps));
    yaw_deg = rad2deg(std::atan2(ys, yc));
    pitch_deg = rad2deg(std::asin(ps));
}

// Load config from file (multiple alarms)
std::vector<LookoutAlarmConfig> load_configs(const std::string& filename) {
    std::vector<LookoutAlarmConfig> cfgs;
    std::ifstream f(filename);
    if (f) {
        nlohmann::json j;
        f >> j;
        if (j.contains("alarms")) {
            for (const auto& item : j["alarms"]) {
                cfgs.push_back(item.get<LookoutAlarmConfig>());
            }
        }
    }
    return cfgs;
}

// Save config to file
void save_config(const std::string& filename, const LookoutAlarmConfig& cfg) {
    nlohmann::json j = cfg;
    std::ofstream f(filename);
    f << j.dump(4);
}

// Play alarm sound: WAV file if specified, else beep.wav using SFML
#include <memory>
#include <vector>

void play_alarm_sound(const std::string& audio_file, int volume) {
    static std::unordered_map<std::string, sf::SoundBuffer> buffer_map;
    static std::vector<std::unique_ptr<sf::Sound>> active_sounds;

    std::string file_to_play = audio_file.empty() ? "beep.wav" : audio_file;
    auto it = buffer_map.find(file_to_play);
    if (it == buffer_map.end()) {
        sf::SoundBuffer buf;
        if (!buf.loadFromFile(file_to_play)) {
            std::cerr << "[ERROR] Could not load sound file: " << file_to_play << std::endl;
            return;
        }
        it = buffer_map.emplace(file_to_play, std::move(buf)).first;
    }
    sf::SoundBuffer& buffer = it->second;

    // Clean up finished sounds
    active_sounds.erase(
        std::remove_if(active_sounds.begin(), active_sounds.end(),
            [](const std::unique_ptr<sf::Sound>& snd) {
                return snd->getStatus() != sf::Sound::Status::Playing;
            }),
        active_sounds.end()
    );

    // Play new sound
    auto sound = std::make_unique<sf::Sound>(buffer);
    sound->setVolume(static_cast<float>(volume));
    sound->play();
    active_sounds.push_back(std::move(sound));
}


#include <deque>
#include <algorithm>

int main() {
    // Initialize Oculus SDK
    ovrResult result = ovr_Initialize(nullptr);
    if (OVR_FAILURE(result)) {
        std::cerr << "Failed to initialize Oculus SDK!" << std::endl;
        return 1;
    }

    ovrSession session;
    ovrGraphicsLuid luid;
    result = ovr_Create(&session, &luid);
    if (OVR_FAILURE(result)) {
        std::cerr << "Failed to create Oculus session!" << std::endl;
        ovr_Shutdown();
        return 1;
    }

    // Load all alarm configs
    std::vector<LookoutAlarmConfig> alarms = load_configs("settings.json");
    if (alarms.empty()) {
        std::cerr << "[ERROR] No alarms loaded from settings.json! Exiting." << std::endl;
        return 1;
    }
    std::cout << "Oculus Lookout Utility started. Press Ctrl+C to quit." << std::endl;
    for (size_t i = 0; i < alarms.size(); ++i) {
        if (alarms[i].repeat_interval_ms < 100) {
            std::cerr << "[WARNING] [Alarm " << i << "] repeat_interval_ms invalid or missing, defaulting to 2000 ms\n";
            alarms[i].repeat_interval_ms = 2000;
        }
    }
    for (const auto& alarm : alarms) {
        std::cout << "[INFO] Warning repeat interval: " << alarm.repeat_interval_ms << " ms\n";
    }

    // Find the alarm with the widest horizontal angle
    size_t widest_alarm_idx = 0;
    double widest_angle = alarms[0].min_horizontal_angle;
    for (size_t i = 1; i < alarms.size(); ++i) {
        if (alarms[i].min_horizontal_angle > widest_angle) {
            widest_angle = alarms[i].min_horizontal_angle;
            widest_alarm_idx = i;
        }
    }

    // Rolling window for median center (shared for all alarms)
    const size_t WINDOW_SIZE = 600; // 2 minutes if POLL_INTERVAL=0.1s
    std::deque<double> yaw_window, pitch_window;
    double elapsed_time = 0.0;

    // --- Parse center reset parameters from settings.json ---
    double center_window_degrees = 20.0;
    double center_hold_time_seconds = 3.0;
    double center_hold_timer = 0.0;
    bool center_reset_active = false;
    {
        std::ifstream f("settings.json");
        if (f) {
            nlohmann::json j;
            f >> j;
            if (j.contains("center_reset")) {
                if (j["center_reset"].contains("window_degrees"))
                    center_window_degrees = j["center_reset"]["window_degrees"].get<double>();
                if (j["center_reset"].contains("hold_time_seconds"))
                    center_hold_time_seconds = j["center_reset"]["hold_time_seconds"].get<double>();
            }
        }
        std::cout << "[INFO] Center reset window: " << center_window_degrees << "°, hold time: " << center_hold_time_seconds << "s" << std::endl;
    }

    // Per-alarm state structs
    struct AlarmState {
        bool warning_triggered = false;
        double noLookTime = 0.0, repeat_timer = 0.0, warning_start_time = 0.0;
        bool looked_left = false, looked_right = false, looked_up = false, looked_down = false;
        bool prev_looked_left = false, prev_looked_right = false;
        double alarm_silence_until = 0.0;
        double left_look_time = -1.0, right_look_time = -1.0;
    };
    std::vector<AlarmState> alarm_states(alarms.size());

    while (true) {
        // Get tracking state
        double displayTime = ovr_GetPredictedDisplayTime(session, 0);
        ovrTrackingState ts = ovr_GetTrackingState(session, displayTime, ovrTrue);
        ovrPosef pose = ts.HeadPose.ThePose;
        ovrQuatf q = pose.Orientation;
        double yaw_deg, pitch_deg;
        quat_to_yaw_pitch(q, yaw_deg, pitch_deg);

        // Compute current center using IQR (interquartile range) median
        double center_yaw = 0.0, center_pitch = 0.0;
        if (!yaw_window.empty()) {
            // Yaw: sort, get Q1, Q3, filter to [Q1, Q3], get median
            std::vector<double> sorted_yaw(yaw_window.begin(), yaw_window.end());
            std::sort(sorted_yaw.begin(), sorted_yaw.end());
            size_t n = sorted_yaw.size();
            size_t q1_idx = n / 4;
            size_t q3_idx = (3 * n) / 4;
            std::vector<double> iqr_yaw(sorted_yaw.begin() + q1_idx, sorted_yaw.begin() + q3_idx + 1);
            std::nth_element(iqr_yaw.begin(), iqr_yaw.begin() + iqr_yaw.size()/2, iqr_yaw.end());
            center_yaw = iqr_yaw[iqr_yaw.size()/2];

            // Pitch: same as yaw
            std::vector<double> sorted_pitch(pitch_window.begin(), pitch_window.end());
            std::sort(sorted_pitch.begin(), sorted_pitch.end());
            n = sorted_pitch.size();
            q1_idx = n / 4;
            q3_idx = (3 * n) / 4;
            std::vector<double> iqr_pitch(sorted_pitch.begin() + q1_idx, sorted_pitch.begin() + q3_idx + 1);
            std::nth_element(iqr_pitch.begin(), iqr_pitch.begin() + iqr_pitch.size()/2, iqr_pitch.end());
            center_pitch = iqr_pitch[iqr_pitch.size()/2];
        }

        double dyaw = clamp_angle(yaw_deg - center_yaw);
        double dpitch = clamp_angle(pitch_deg - center_pitch);

        // --- Lookout scan logic: must look past both sides (horiz & vert) before timer resets ---
        // --- Center Reset Logic ---
        // Check if headset is within center window for all axes
        if (std::abs(dyaw) < center_window_degrees && std::abs(dpitch) < center_window_degrees) {
            center_hold_timer += POLL_INTERVAL;
            if (!center_reset_active && center_hold_timer >= center_hold_time_seconds) {
                // Reset lookout flags for all alarms
                for (size_t i = 0; i < alarms.size(); ++i) {
                    alarm_states[i].looked_left = false;
                    alarm_states[i].looked_right = false;
                    alarm_states[i].looked_up = false;
                    alarm_states[i].looked_down = false;
                    alarm_states[i].left_look_time = -1.0;
                    alarm_states[i].right_look_time = -1.0;
                }
                center_reset_active = true;
                std::cout << "[DEBUG] Center reset: All lookout flags reset after holding within " << center_window_degrees << "° for " << center_hold_time_seconds << " seconds." << std::endl;
            }
        } else {
            center_hold_timer = 0.0;
            center_reset_active = false;
        }

        for (size_t i = 0; i < alarms.size(); ++i) {
            AlarmState& state = alarm_states[i];
            const LookoutAlarmConfig& config = alarms[i];
            double min_horiz = config.min_horizontal_angle / 2.0;
            double min_vert = config.min_vertical_angle / 2.0;

            // Always update rolling window for center with current head orientation
            yaw_window.push_back(yaw_deg);
            pitch_window.push_back(pitch_deg);
            if (yaw_window.size() > WINDOW_SIZE) yaw_window.pop_front();
            if (pitch_window.size() > WINDOW_SIZE) pitch_window.pop_front();

            // Save previous L/R before updating
            bool prev_looked_left_tmp = state.looked_left;
            bool prev_looked_right_tmp = state.looked_right;

            // Track lookouts
            // Set flags and record the first time they're set
            if (dyaw < -config.min_horizontal_angle && !state.looked_left) {
                state.looked_left = true;
                state.left_look_time = elapsed_time;
                // Silence alarm for silence_after_look_ms from NOW
                state.alarm_silence_until = elapsed_time + config.silence_after_look_ms;
                std::cout << "[DEBUG] Alarm " << i << ": Alarm silenced for " << config.silence_after_look_ms << " ms after L set (0->1)." << std::endl;
            }
            if (dyaw >  config.min_horizontal_angle && !state.looked_right) {
                state.looked_right = true;
                state.right_look_time = elapsed_time;
                // Silence alarm for silence_after_look_ms from NOW
                state.alarm_silence_until = elapsed_time + config.silence_after_look_ms;
                std::cout << "[DEBUG] Alarm " << i << ": Alarm silenced for " << config.silence_after_look_ms << " ms after R set (0->1)." << std::endl;
            }
            if (dpitch < -config.min_vertical_angle) state.looked_down = true;
            if (dpitch >  config.min_vertical_angle) state.looked_up = true;

            // Update previous L/R for next iteration
            state.prev_looked_left = prev_looked_left_tmp;
            state.prev_looked_right = prev_looked_right_tmp;

            // Throttled debug output (print once per second)
            static double last_debug_time = 0.0;
            if (elapsed_time - last_debug_time >= 1000.0) {
                std::cout << "[DEBUG] Alarm " << i << ": dyaw: " << dyaw << ", dpitch: " << dpitch
                          << " | center_yaw: " << center_yaw << ", center_pitch: " << center_pitch
                          << " | noLookTime: " << state.noLookTime
                          << " | L:" << state.looked_left << " R:" << state.looked_right
                          << " U:" << state.looked_up << " D:" << state.looked_down
                          << " | warning_triggered: " << state.warning_triggered
                          << " | repeat_timer: " << state.repeat_timer << std::endl;
                last_debug_time = elapsed_time;
            }

            // Always increment timer
            state.noLookTime += POLL_INTERVAL * 1000.0; // ms

            // --- ALARM SILENCE BASED ON MOST RECENT L/R EVENT ---
            // Do not trigger alarm if we're still in the silence window after last L/R
            if (!state.warning_triggered && state.noLookTime >= config.max_time_ms) {
                if (elapsed_time < state.alarm_silence_until) {
                    std::cout << "[DEBUG] Alarm " << i << ": Alarm silenced due to recent L/R, skipping first warning." << std::endl;
                    continue;
                }
            }

            // If all lookouts completed, check min_lookout_time_ms between L/R
            if (state.looked_left && state.looked_right && state.looked_up && state.looked_down) {
                double lr_time_diff = std::abs(state.left_look_time - state.right_look_time);
                if (lr_time_diff >= config.min_lookout_time_ms) {
                    state.noLookTime = 0.0;
                    state.warning_triggered = false;
                    state.repeat_timer = 0.0;
                    state.warning_start_time = 0.0;
                    state.looked_left = state.looked_right = state.looked_up = state.looked_down = false;
                    state.left_look_time = state.right_look_time = -1.0;
                    std::cout << "[DEBUG] Alarm " << i << ": Lookout flags reset after successful lookout. L/R time diff: " << lr_time_diff << " ms" << std::endl;
                    // --- WIDER ANGLE ALARM RESETS NARROWER ALARMS ---
                    if (i == widest_alarm_idx) {
                        for (size_t j = 0; j < alarms.size(); ++j) {
                            if (j != i && alarms[j].min_horizontal_angle < config.min_horizontal_angle) {
                                alarm_states[j].noLookTime = 0.0;
                                alarm_states[j].warning_triggered = false;
                                alarm_states[j].repeat_timer = 0.0;
                                alarm_states[j].warning_start_time = 0.0;
                                alarm_states[j].looked_left = alarm_states[j].looked_right = alarm_states[j].looked_up = alarm_states[j].looked_down = false;
                                alarm_states[j].left_look_time = alarm_states[j].right_look_time = -1.0;
                                std::cout << "[DEBUG] Alarm " << i << ": Resetting narrower alarm " << j << " due to wider angle lookout." << std::endl;
                            }
                        }
                    }
                } else {
                    // Only print once per failed attempt, then reset flags/timestamps
                    std::cout << "[DEBUG] Alarm " << i << ": Lookout not counted: L/R time diff too short (" << lr_time_diff << " ms < " << config.min_lookout_time_ms << " ms)" << std::endl;
                    state.looked_left = state.looked_right = false;
                    state.left_look_time = state.right_look_time = -1.0;
                    // Keep up/down flags so user doesn't have to repeat those
                }
            } else {
                // Check for L/R flag set after alarm is repeating (0 -> 1)
                if (state.warning_triggered && ((!state.prev_looked_left && state.looked_left) || (!state.prev_looked_right && state.looked_right))) {
                    state.alarm_silence_until = elapsed_time + config.silence_after_look_ms;
                    std::cout << "[DEBUG] Alarm " << i << ": Alarm silenced for " << config.silence_after_look_ms << " ms after L or R set (0->1)." << std::endl;
                }

                if (!state.warning_triggered && state.noLookTime >= config.max_time_ms) {
                    // Only trigger alarm if not silenced
                    if (elapsed_time < state.alarm_silence_until) {
                        std::cout << "[DEBUG] Alarm " << i << ": Alarm pre-silenced, skipping first warning." << std::endl;
                        continue;
                    }
                    state.left_look_time = state.right_look_time = -1.0; // reset if warning triggers
                    state.warning_triggered = true;
                    state.repeat_timer = 0.0;
                    state.warning_start_time = elapsed_time;
                    int cur_volume = config.start_volume;
                    // Reset lookout flags after warning so user can start a fresh cycle
                    state.looked_left = state.looked_right = state.looked_up = state.looked_down = false;
                    std::cout << "[DEBUG] Alarm " << i << ": Lookout flags reset after warning triggered." << std::endl;
                    // Optionally reset timer to avoid immediate repeat (more forgiving UX)
                    state.noLookTime = 0.0;
                    std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout!\n"
                              << "    Median center (yaw, pitch): (" << center_yaw << ", " << center_pitch << ")\n"
                              << "    Current (yaw, pitch): (" << yaw_deg << ", " << pitch_deg << ")\n"
                              << "    Relative (dyaw, dpitch): (" << dyaw << ", " << dpitch << ")\n"
                              << "    Volume: " << cur_volume << std::endl;
                    std::cout << "[DEBUG] Alarm " << i << ": Warning triggered!" << std::endl;
                    play_alarm_sound(config.audio_file, cur_volume);
                } else if (state.warning_triggered) {
                    state.repeat_timer += POLL_INTERVAL * 1000.0;
                    double ramp_elapsed = elapsed_time - state.warning_start_time;
                    int cur_volume = config.start_volume;
                    if (config.volume_ramp_time_ms > 0 && config.end_volume != config.start_volume) {
                        double ramp = (std::min)(1.0, ramp_elapsed / config.volume_ramp_time_ms);
                        cur_volume = (int)(config.start_volume + ramp * (config.end_volume - config.start_volume));
                    }
                    if (state.repeat_timer >= config.repeat_interval_ms) {
                        if (elapsed_time < state.alarm_silence_until) {
                            // Alarm is silenced
                            std::cout << "[DEBUG] Alarm " << i << ": Alarm silenced, skipping repeat warning." << std::endl;
                        } else {
                            std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! (repeat)\n"
                                      << "    Median center (yaw, pitch): (" << center_yaw << ", " << center_pitch << ")\n"
                                      << "    Current (yaw, pitch): (" << yaw_deg << ", " << pitch_deg << ")\n"
                                      << "    Relative (dyaw, dpitch): (" << dyaw << ", " << dpitch << ")\n"
                                      << "    Volume: " << cur_volume << std::endl;
                            std::cout << "[DEBUG] Alarm " << i << ": Repeat warning triggered!" << std::endl;
                            play_alarm_sound(config.audio_file, cur_volume);
                        }
                        state.repeat_timer = 0.0;
                    }
                }
            }
        }

        elapsed_time += POLL_INTERVAL * 1000.0;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(POLL_INTERVAL * 1000)));
    }
    ovr_Destroy(session);
    ovr_Shutdown();
    return 0;
}
