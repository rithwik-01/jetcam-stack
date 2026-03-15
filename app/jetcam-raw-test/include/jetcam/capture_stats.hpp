#pragma once

#include <cstdint>
#include <limits>

namespace jetcam {

class CaptureStats {
public:
    void add_frame(std::uint32_t sequence, double timestamp_seconds,
                   bool buffer_error) noexcept {
        if (frames_ == 0U) {
            first_sequence_ = sequence;
            first_timestamp_ = timestamp_seconds;
        } else {
            const std::uint32_t delta = sequence - last_sequence_;
            if (delta > 1U && delta < kBackwardThreshold) {
                dropped_frames_ += static_cast<std::uint64_t>(delta - 1U);
            } else if (delta == 0U || delta >= kBackwardThreshold) {
                ++sequence_discontinuities_;
            }
        }

        last_sequence_ = sequence;
        last_timestamp_ = timestamp_seconds;
        ++frames_;
        if (buffer_error) {
            ++buffer_errors_;
        }
    }

    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] std::uint64_t dropped_frames() const noexcept {
        return dropped_frames_;
    }
    [[nodiscard]] std::uint64_t sequence_discontinuities() const noexcept {
        return sequence_discontinuities_;
    }
    [[nodiscard]] std::uint64_t buffer_errors() const noexcept {
        return buffer_errors_;
    }
    [[nodiscard]] std::uint32_t first_sequence() const noexcept {
        return first_sequence_;
    }
    [[nodiscard]] std::uint32_t last_sequence() const noexcept {
        return last_sequence_;
    }
    [[nodiscard]] double elapsed_seconds() const noexcept {
        if (frames_ < 2U || last_timestamp_ <= first_timestamp_) {
            return 0.0;
        }
        return last_timestamp_ - first_timestamp_;
    }
    [[nodiscard]] double fps() const noexcept {
        const double elapsed = elapsed_seconds();
        if (elapsed <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(frames_ - 1U) / elapsed;
    }

private:
    static constexpr std::uint32_t kBackwardThreshold =
        std::numeric_limits<std::uint32_t>::max() / 2U;

    std::uint64_t frames_ = 0;
    std::uint64_t dropped_frames_ = 0;
    std::uint64_t sequence_discontinuities_ = 0;
    std::uint64_t buffer_errors_ = 0;
    std::uint32_t first_sequence_ = 0;
    std::uint32_t last_sequence_ = 0;
    double first_timestamp_ = 0.0;
    double last_timestamp_ = 0.0;
};

}  // namespace jetcam
