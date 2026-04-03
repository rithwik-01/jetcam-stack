#include "jetcam/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace jetcam {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

int parse_int(const std::string& key, const std::string& value, int minimum, int maximum) {
    std::size_t consumed = 0;
    long parsed = 0;
    try {
        parsed = std::stol(value, &consumed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + key + ": " + value);
    }
    if (consumed != value.size() || parsed < minimum || parsed > maximum) {
        throw std::runtime_error("out-of-range integer for " + key + ": " + value);
    }
    return static_cast<int>(parsed);
}

bool parse_bool(const std::string& key, std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "true" || value == "yes" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "0") {
        return false;
    }
    throw std::runtime_error("invalid boolean for " + key + ": " + value);
}

bool safe_token(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isalnum(ch) || ch == '.' || ch == ':' || ch == '-' || ch == '_';
           });
}

}  // namespace

std::string output_mode_name(OutputMode mode) {
    return mode == OutputMode::udp ? "udp" : "rtsp";
}

void apply_assignment(Config& config, const std::string& raw_key, const std::string& raw_value) {
    const std::string key = trim(raw_key);
    const std::string value = trim(raw_value);
    if (key == "sensor_id") {
        config.sensor_id = parse_int(key, value, 0, 255);
    } else if (key == "width") {
        config.width = parse_int(key, value, 16, 8192);
    } else if (key == "height") {
        config.height = parse_int(key, value, 16, 8192);
    } else if (key == "framerate") {
        config.framerate = parse_int(key, value, 1, 240);
    } else if (key == "bitrate_kbps") {
        config.bitrate_kbps = parse_int(key, value, 100, 200000);
    } else if (key == "gop") {
        config.gop = parse_int(key, value, 1, 10000);
    } else if (key == "host") {
        config.host = value;
    } else if (key == "port") {
        config.port = static_cast<std::uint16_t>(parse_int(key, value, 1, 65535));
    } else if (key == "output") {
        if (value == "udp") {
            config.output = OutputMode::udp;
        } else if (value == "rtsp") {
            config.output = OutputMode::rtsp;
        } else {
            throw std::runtime_error("output must be udp or rtsp");
        }
    } else if (key == "rtsp_port") {
        config.rtsp_port = static_cast<std::uint16_t>(parse_int(key, value, 1, 65535));
    } else if (key == "rtsp_mount") {
        config.rtsp_mount = value;
    } else if (key == "snapshot_file") {
        config.snapshot_file = value;
    } else if (key == "snapshot_on_start") {
        config.snapshot_on_start = parse_bool(key, value);
    } else {
        throw std::runtime_error("unknown configuration key: " + key);
    }
}

void validate_config(const Config& config) {
    const bool mode_720p60 = config.width == 1280 && config.height == 720 && config.framerate == 60;
    const bool mode_1080p30 = config.width == 1920 && config.height == 1080 && config.framerate == 30;
    if (!mode_720p60 && !mode_1080p30) {
        throw std::runtime_error("supported modes are 1280x720@60 and 1920x1080@30");
    }
    if (!safe_token(config.host)) {
        throw std::runtime_error("host contains unsupported characters");
    }
    if (config.rtsp_mount.empty() || config.rtsp_mount.front() != '/' ||
        !safe_token(config.rtsp_mount.substr(1))) {
        throw std::runtime_error("rtsp_mount must be a simple path such as /jetcam");
    }
    if (config.snapshot_file.empty() || config.snapshot_file.find('\n') != std::string::npos) {
        throw std::runtime_error("snapshot_file must not be empty or contain a newline");
    }
}

Config load_config(const std::string& path) {
    Config config;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open configuration file: " + path);
    }

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": expected key=value");
        }
        try {
            apply_assignment(config, line.substr(0, separator), line.substr(separator + 1));
        } catch (const std::exception& error) {
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": " + error.what());
        }
    }
    validate_config(config);
    return config;
}

void print_config(const Config& config, std::ostream& output) {
    output << "sensor_id=" << config.sensor_id << '\n'
           << "mode=" << config.width << 'x' << config.height << '@' << config.framerate << '\n'
           << "encoder=x264-software\n"
           << "bitrate_kbps=" << config.bitrate_kbps << '\n'
           << "gop=" << config.gop << '\n'
           << "output=" << output_mode_name(config.output) << '\n';
    if (config.output == OutputMode::udp) {
        output << "destination=udp://" << config.host << ':' << config.port << '\n';
    } else {
        output << "destination=rtsp://0.0.0.0:" << config.rtsp_port << config.rtsp_mount << '\n';
    }
}

}  // namespace jetcam
