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

int main() {
    ovrResult result = ovr_Initialize(nullptr);
    if (OVR_FAILURE(result)) { std::cerr << "OVR Init Fail" << std::endl; return 1; }

    ovrSession session;
    ovrGraphicsLuid luid;
    result = ovr_Create(&session, &luid);
    if (OVR_FAILURE(result)) { std::cerr << "OVR Create Fail" << std::endl; ovr_Shutdown(); return 1; }

    std::vector<LookoutAlarmConfig> alarms = load_configs("settings.json");
    if (alarms.empty()) { std::cerr << "No Alarms Loaded" << std::endl; ovr_Destroy(session); ovr_Shutdown(); return 1; }
    
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
        double displayTime = ovr_GetPredictedDisplayTime(session, 0);
        ovrTrackingState ts = ovr_GetTrackingState(session, displayTime, ovrTrue);
        if (!(ts.StatusFlags & ovrStatus_OrientationTracked)) { 
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(POLL_INTERVAL * 1000)));
            elapsed_time_ms += POLL_INTERVAL * 1000.0; 
            // Basic tracking warning, can be expanded
            static double last_no_track_warn = 0;
            if(elapsed_time_ms - last_no_track_warn > 5000) {
                std::cerr << "[WARNING] No HMD tracking!" << std::endl;
                last_no_track_warn = elapsed_time_ms;
            }
            continue; 
        }
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

            bool prev_looked_left_ever = state.looked_left_ever; // For detecting new L/R this tick
            bool prev_looked_right_ever = state.looked_right_ever;

            bool currently_looking_left = dyaw < -(config.min_horizontal_angle / 2.0);
            bool currently_looking_right = dyaw > (config.min_horizontal_angle / 2.0);
            bool currently_looking_up = dpitch > (config.min_vertical_angle / 2.0);
            bool currently_looking_down = dpitch < -(config.min_vertical_angle / 2.0);

            bool new_lr_look_this_tick = false;
            if (currently_looking_left && !state.looked_left_ever) { // Only update if it's a new "ever" detection
                state.looked_left_ever = true; state.left_ever_time_ms = elapsed_time_ms; new_lr_look_this_tick = true;
                std::cout << "[DEBUG] Alarm " << i << ": L registered." << std::endl;
            }
            if (currently_looking_right && !state.looked_right_ever) {
                state.looked_right_ever = true; state.right_ever_time_ms = elapsed_time_ms; new_lr_look_this_tick = true;
                std::cout << "[DEBUG] Alarm " << i << ": R registered." << std::endl;
            }
            // U/D flags become true if ever looked in that direction since last reset
            if (currently_looking_up) state.looked_up_ever = true;
            if (currently_looking_down) state.looked_down_ever = true;
            
            static double last_periodic_state_dump_time_ms = 0.0; 
            if (elapsed_time_ms - last_periodic_state_dump_time_ms >= 1000.0) { 
                 std::cout << std::fixed << std::setprecision(1) 
                           << "[STATE] Alarm " << i 
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
                    if (state.sound_player) { state.sound_player->stop(); /* sound_player will be nullptr or new next time */ }
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
                                if (alarm_states[j].sound_player) { alarm_states[j].sound_player->stop(); /* and null it? handled by get_or_create */}
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
                    
                    // Stop any previous sound for this alarm before starting new one
                    if(state.sound_player && state.sound_player->getStatus() != sf::SoundSource::Status::Stopped) state.sound_player->stop();
                    state.sound_player = get_or_create_sound_player(config.audio_file);

                    if (state.sound_player) {
                        int cur_volume = config.start_volume;
                        state.sound_player->setLooping(true); // CORRECTED API
                        state.sound_player->setVolume(static_cast<float>(cur_volume));
                        state.sound_player->play();
                        std::cout << "[WARNING] Alarm " << i << ": Please perform a visual lookout! Vol: " << cur_volume << std::endl;
                    }
                }
            } 
            else if (state.warning_triggered) { 
                if (!state.sound_player || state.sound_player->getStatus() == sf::SoundSource::Status::Stopped) {
                     if(state.sound_player && state.sound_player->getStatus() != sf::SoundSource::Status::Stopped) state.sound_player->stop(); // Stop if not null but somehow stopped by other means
                    state.sound_player = get_or_create_sound_player(config.audio_file);
                    if (state.sound_player && state.sound_player->getStatus() != sf::SoundSource::Status::Playing) { // CORRECTED API
                         state.sound_player->setLooping(true); // CORRECTED API
                         state.sound_player->play(); 
                         std::cout << "[DEBUG] Alarm " << i << ": Sound player (re)started for active warning." << std::endl;
                    }
                }

                if (state.sound_player) {
                    double ramp_elapsed_ms = elapsed_time_ms - state.warning_start_time_ms;
                    int target_volume = config.start_volume;
                    if (config.volume_ramp_time_ms > 0 && config.end_volume != config.start_volume) {
                        double ramp_progress = std::min(1.0, ramp_elapsed_ms / static_cast<double>(config.volume_ramp_time_ms));
                        target_volume = static_cast<int>(config.start_volume + ramp_progress * (config.end_volume - config.start_volume));
                    }

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
                            if (state.sound_player->getStatus() != sf::SoundSource::Status::Playing) { // CORRECTED API
                                state.sound_player->play(); // Ensure it's playing if it was stopped for any reason and should be looping
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

    active_sound_players.clear(); 
    ovr_Destroy(session);
    ovr_Shutdown();
    return 0;
}