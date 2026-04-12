#pragma once

#include "jetcam/config.hpp"

typedef struct _GMainLoop GMainLoop;

namespace jetcam {

int run_rtsp_server(const Config& config, GMainLoop* loop);

}  // namespace jetcam
