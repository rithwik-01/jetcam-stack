#include "jetcam/config.hpp"
#include "jetcam/pipeline.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    const auto expect = [](bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            return false;
        }
        return true;
    };

    jetcam::Config config;
    jetcam::apply_assignment(config, "width", "1920");
    jetcam::apply_assignment(config, "height", "1080");
    jetcam::apply_assignment(config, "framerate", "30");
    jetcam::apply_assignment(config, "bitrate_kbps", "8000");
    jetcam::apply_assignment(config, "gop", "30");
    jetcam::validate_config(config);

    const std::string udp = jetcam::build_udp_pipeline(config);
    if (!expect(udp.find("width=1920,height=1080,framerate=30/1") != std::string::npos,
                "1080p30 caps") ||
        !expect(udp.find("x264enc") != std::string::npos, "software encoder") ||
        !expect(udp.find("nvv4l2h264enc") == std::string::npos, "no unavailable NVENC") ||
        !expect(udp.find("mpegtsmux") != std::string::npos, "MPEG-TS mux") ||
        !expect(udp.find("h264parse config-interval=1") != std::string::npos,
                "periodic SPS/PPS for late receivers") ||
        !expect(udp.find("snapshot_valve drop=true") != std::string::npos,
                "normally closed snapshot valve")) {
        return 1;
    }

    jetcam::apply_assignment(config, "output", "rtsp");
    const std::string rtsp_capture = jetcam::build_rtsp_capture_pipeline(config);
    const std::string rtsp_factory = jetcam::build_rtsp_factory_pipeline();
    if (!expect(rtsp_capture.find("udpsink host=127.0.0.1 port=5400") != std::string::npos,
                "managed RTSP capture relay") ||
        !expect(rtsp_factory.find("rtph264pay name=pay0") != std::string::npos,
                "RTSP factory payloader")) {
        return 1;
    }

    bool rejected = false;
    try {
        jetcam::apply_assignment(config, "framerate", "sixty");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    return expect(rejected, "invalid integer rejected") ? 0 : 1;
}
