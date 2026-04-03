#pragma once

#include "jetcam/config.hpp"

#include <string>

namespace jetcam {

std::string build_udp_pipeline(const Config& config);
std::string build_rtsp_capture_pipeline(const Config& config);
std::string build_rtsp_factory_pipeline();

}  // namespace jetcam
