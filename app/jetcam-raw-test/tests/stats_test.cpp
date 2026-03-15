#include "jetcam/capture_stats.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    jetcam::CaptureStats continuous;
    continuous.add_frame(0, 10.0, false);
    continuous.add_frame(1, 10.02, false);
    continuous.add_frame(2, 10.04, false);
    require(continuous.frames() == 3, "continuous frame count");
    require(continuous.dropped_frames() == 0, "continuous sequence");
    require(std::abs(continuous.fps() - 50.0) < 0.001, "FPS calculation");

    jetcam::CaptureStats gap;
    gap.add_frame(100, 1.0, false);
    gap.add_frame(104, 2.0, true);
    require(gap.dropped_frames() == 3, "sequence gap count");
    require(gap.buffer_errors() == 1, "buffer error count");

    jetcam::CaptureStats wrap;
    wrap.add_frame(std::numeric_limits<std::uint32_t>::max(), 1.0, false);
    wrap.add_frame(0, 2.0, false);
    require(wrap.dropped_frames() == 0, "sequence wrap is continuous");
    require(wrap.sequence_discontinuities() == 0,
            "sequence wrap is not a discontinuity");

    jetcam::CaptureStats duplicate;
    duplicate.add_frame(7, 1.0, false);
    duplicate.add_frame(7, 2.0, false);
    require(duplicate.sequence_discontinuities() == 1,
            "duplicate sequence detection");

    std::cout << "capture stats tests passed\n";
    return EXIT_SUCCESS;
}
