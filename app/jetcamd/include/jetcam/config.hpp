#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace jetcam {

enum class OutputMode { udp, rtsp };

struct Config {
    int sensor_id = 0;
    int width = 1280;
    int height = 720;
    int framerate = 60;
    int bitrate_kbps = 6000;
    int gop = 60;
    std::string host = "127.0.0.1";
    std::uint16_t port = 5000;
    OutputMode output = OutputMode::udp;
    std::uint16_t rtsp_port = 8554;
    std::string rtsp_mount = "/jetcam";
    std::string snapshot_file = "jetcam-snapshot.jpg";
    bool snapshot_on_start = false;
};

Config load_config(const std::string& path);
void apply_assignment(Config& config, const std::string& key, const std::string& value);
void validate_config(const Config& config);
void print_config(const Config& config, std::ostream& output);
std::string output_mode_name(OutputMode mode);

}  // namespace jetcam
