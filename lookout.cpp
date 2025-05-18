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
    double min_horizontal_angle = 60.0; // degrees (±30)
    double min_vertical_angle = 20.0;   // degrees (±10)
    int max_time_ms = 2000;             // ms
    std::string audio_file;             // Path to audio file (empty = beep)
    int start_volume = 50;              // 0-100
    int end_volume = 100;               // 0-100
    int volume_ramp_time_ms = 1000;     // ms
    int repeat_interval_ms = 2000;      // ms
    int min_lookout_time_ms = 2000;     // Minimum time to hold lookout (ms)

    // Serialize/deserialize with nlohmann::json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LookoutAlarmConfig,
        min_horizontal_angle, min_vertical_angle, max_time_ms, audio_file,
        start_volume, end_volume, volume_ramp_time_ms, repeat_interval_ms, min_lookout_time_ms)
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

// Load config from file
LookoutAlarmConfig load_config(const std::string& filename) {
    LookoutAlarmConfig cfg;
    std::ifstream f(filename);
    if (f) {
        nlohmann::json j;
        f >> j;
        cfg = j.get<LookoutAlarmConfig>();
    }
    return cfg;
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
    static sf::SoundBuffer buffer;
    static std::string last_file;
    static std::vector<std::unique_ptr<sf::Sound>> active_sounds;

    std::string file_to_play = audio_file.empty() ? "beep.wav" : audio_file;
    if (last_file != file_to_play) {
        if (!buffer.loadFromFile(file_to_play)) {
            std::cerr << "[ERROR] Could not load sound file: " << file_to_play << std::endl;
            return;
        }
        last_file = file_to_play;
    }

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

    // Load config
    LookoutAlarmConfig config = load_config("settings.json");
    double min_horiz = config.min_horizontal_angle / 2.0; // ±half
    double min_vert = config.min_vertical_angle / 2.0;     // ±half
    double noLookTime = 0.0;
    bool primed = false;
    std::cout << "Oculus Lookout Utility started. Press Ctrl+C to quit." << std::endl;

    // Rolling window for median center
    const size_t WINDOW_SIZE = 1000; // ~10 seconds if POLL_INTERVAL=0.1s
    std::deque<double> yaw_window, pitch_window;
    bool warning_triggered = false;
    double repeat_timer = 0.0;
    double warning_start_time = 0.0;
    double elapsed_time = 0.0;

    while (true) {
        // Get tracking state
        double displayTime = ovr_GetPredictedDisplayTime(session, 0);
        ovrTrackingState ts = ovr_GetTrackingState(session, displayTime, ovrTrue);
        ovrPosef pose = ts.HeadPose.ThePose;
        ovrQuatf q = pose.Orientation;
        double yaw_deg, pitch_deg;
        quat_to_yaw_pitch(q, yaw_deg, pitch_deg);

        // Compute current center
        double center_yaw = 0.0, center_pitch = 0.0;
        if (!yaw_window.empty()) {
            std::vector<double> sorted_yaw(yaw_window.begin(), yaw_window.end());
            std::vector<double> sorted_pitch(pitch_window.begin(), pitch_window.end());
            std::nth_element(sorted_yaw.begin(), sorted_yaw.begin() + sorted_yaw.size()/2, sorted_yaw.end());
            std::nth_element(sorted_pitch.begin(), sorted_pitch.begin() + sorted_pitch.size()/2, sorted_pitch.end());
            center_yaw = sorted_yaw[sorted_yaw.size()/2];
            center_pitch = sorted_pitch[sorted_pitch.size()/2];
        }

        double dyaw = clamp_angle(yaw_deg - center_yaw);
        double dpitch = clamp_angle(pitch_deg - center_pitch);

        // Only update rolling window with 'normal' (non-lookout) samples
        if (std::abs(dyaw) < min_horiz && std::abs(dpitch) < min_vert) {
            yaw_window.push_back(yaw_deg);
            pitch_window.push_back(pitch_deg);
            if (yaw_window.size() > WINDOW_SIZE) yaw_window.pop_front();
            if (pitch_window.size() > WINDOW_SIZE) pitch_window.pop_front();
            // User is centered: prime the system and reset noLookTime
            primed = true;
            noLookTime = 0.0;
        }

        // Yaw-only side-to-side lookout logic
        enum class YawLookoutState { None, Left, Right };
        static YawLookoutState lookout_state = YawLookoutState::None;
        static double lookout_timer = 0.0;
        static double lookout_start_time = 0.0;

        // Detect crossing left/right thresholds
        if (lookout_state == YawLookoutState::None) {
            if (dyaw < -min_horiz) {
                lookout_state = YawLookoutState::Left;
                lookout_timer = 0.0;
                lookout_start_time = elapsed_time;
                // std::cout << "[DEBUG] Started lookout to left\n";
            } else if (dyaw > min_horiz) {
                lookout_state = YawLookoutState::Right;
                lookout_timer = 0.0;
                lookout_start_time = elapsed_time;
                // std::cout << "[DEBUG] Started lookout to right\n";
            }
        } else if (lookout_state == YawLookoutState::Left) {
            // Wait for crossing to right
            if (dyaw > min_horiz) {
                lookout_timer = (elapsed_time - lookout_start_time) * 1000.0;
                if (lookout_timer >= config.min_lookout_time_ms) {
                    std::cout << "[INFO] Lookout (left-to-right) took " << lookout_timer << " ms (min required: " << config.min_lookout_time_ms << ")\n";
                    noLookTime = 0.0;
                    warning_triggered = false;
                    repeat_timer = 0.0;
                    warning_start_time = 0.0;
                } else {
                    // std::cout << "[INFO] Lookout (left-to-right) too quick: " << lookout_timer << " ms\n";
                }
                lookout_state = YawLookoutState::None;
            } else if (std::abs(dyaw) < min_horiz) {
                // Returned to center before reaching other side
                lookout_state = YawLookoutState::None;
                lookout_timer = 0.0;
            }
        } else if (lookout_state == YawLookoutState::Right) {
            // Wait for crossing to left
            if (dyaw < -min_horiz) {
                lookout_timer = (elapsed_time - lookout_start_time) * 1000.0;
                if (lookout_timer >= config.min_lookout_time_ms) {
                    std::cout << "[INFO] Lookout (right-to-left) took " << lookout_timer << " ms (min required: " << config.min_lookout_time_ms << ")\n";
                    noLookTime = 0.0;
                    warning_triggered = false;
                    repeat_timer = 0.0;
                    warning_start_time = 0.0;
                } else {
                    // std::cout << "[INFO] Lookout (right-to-left) too quick: " << lookout_timer << " ms\n";
                }
                lookout_state = YawLookoutState::None;
            } else if (std::abs(dyaw) < min_horiz) {
                // Returned to center before reaching other side
                lookout_state = YawLookoutState::None;
                lookout_timer = 0.0;
            }
        }

            // Only increment noLookTime and allow warnings if primed
            if (primed) {
                noLookTime += POLL_INTERVAL * 1000.0; // ms
                if (!warning_triggered && noLookTime >= config.max_time_ms) {
                    warning_triggered = true;
                    repeat_timer = config.repeat_interval_ms; // force immediate first warning
                    warning_start_time = elapsed_time;
                }
                if (warning_triggered) {
                    repeat_timer += POLL_INTERVAL * 1000.0;
                    double ramp_elapsed = elapsed_time - warning_start_time;
                    int cur_volume = config.start_volume;
                    if (config.volume_ramp_time_ms > 0 && config.end_volume != config.start_volume) {
                        double ramp = (std::min)(1.0, ramp_elapsed / config.volume_ramp_time_ms);
                        cur_volume = (int)(config.start_volume + ramp * (config.end_volume - config.start_volume));
                    }
                    if (repeat_timer >= config.repeat_interval_ms) {
                        std::cout << "[WARNING] Please perform a visual lookout!\n"
                                  << "    Median center (yaw, pitch): (" << center_yaw << ", " << center_pitch << ")\n"
                                  << "    Current (yaw, pitch): (" << yaw_deg << ", " << pitch_deg << ")\n"
                                  << "    Relative (dyaw, dpitch): (" << dyaw << ", " << dpitch << ")\n"
                                  << "    Volume: " << cur_volume << std::endl;
                        play_alarm_sound(config.audio_file, cur_volume);
                        repeat_timer = 0.0;
                    }
                }
            }

        }
        elapsed_time += POLL_INTERVAL * 1000.0;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(POLL_INTERVAL * 1000)));
    
    ovr_Destroy(session);
    ovr_Shutdown();
    return 0;
}

