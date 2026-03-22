#include "jetcam/capture_stats.hpp"

#include <linux/videodev2.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kTegraSensorModeControlId = 0x009a2008U;

enum class ExitCode : int {
    success = 0,
    usage = 2,
    device_error = 3,
    timeout = 4,
    validation_failed = 5,
};

struct Options {
    std::string device = "/dev/video0";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t pixel_format = V4L2_PIX_FMT_SRGGB10;
    double fps = 60.0;
    std::uint64_t frame_limit = 120;
    double duration_seconds = 0.0;
    int timeout_ms = 2500;
    std::uint32_t buffer_count = 4;
    std::optional<std::int64_t> sensor_mode = 4;
    std::optional<std::filesystem::path> save_path;
    std::uint64_t save_at = 1;
    double report_interval_seconds = 1.0;
    bool list_only = false;
    bool allow_drops = false;
};

struct MappedBuffer {
    void* address = MAP_FAILED;
    std::size_t length = 0;
};

volatile std::sig_atomic_t g_stop_signal = 0;

void handle_signal(int signal_number) {
    g_stop_signal = signal_number;
}

class SystemError final : public std::runtime_error {
public:
    SystemError(std::string operation, int error_number)
        : std::runtime_error(std::move(operation) + ": " +
                             std::strerror(error_number)),
          error_number_(error_number) {}

    [[nodiscard]] int error_number() const noexcept { return error_number_; }

private:
    int error_number_;
};

int xioctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR && g_stop_signal == 0);
    return result;
}

std::string fourcc_to_string(std::uint32_t value) {
    std::string result(4, ' ');
    for (unsigned int index = 0; index < 4; ++index) {
        const char character =
            static_cast<char>((value >> (index * 8U)) & 0xffU);
        result[index] = (character >= 32 && character <= 126) ? character : '?';
    }
    return result;
}

template <std::size_t Size>
std::string fixed_string(const std::uint8_t (&value)[Size]) {
    const auto* begin = reinterpret_cast<const char*>(value);
    const auto* end = std::find(begin, begin + Size, '\0');
    return std::string(begin, end);
}

std::uint32_t parse_fourcc(std::string_view text) {
    if (text.size() != 4U) {
        throw std::invalid_argument("pixel format must contain exactly four characters");
    }
    return v4l2_fourcc(text[0], text[1], text[2], text[3]);
}

std::uint64_t parse_unsigned(std::string_view text, const char* option) {
    if (text.empty() || text.front() == '-') {
        throw std::invalid_argument(std::string(option) + " requires a non-negative integer");
    }
    char* end = nullptr;
    errno = 0;
    const std::string value(text);
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != '\0') {
        throw std::invalid_argument(std::string(option) + " has an invalid integer value");
    }
    return static_cast<std::uint64_t>(parsed);
}

double parse_positive_double(std::string_view text, const char* option,
                             bool allow_zero) {
    char* end = nullptr;
    errno = 0;
    const std::string value(text);
    const double parsed = std::strtod(value.c_str(), &end);
    const bool range_ok = allow_zero ? parsed >= 0.0 : parsed > 0.0;
    if (errno == ERANGE || end == value.c_str() || *end != '\0' ||
        !std::isfinite(parsed) || !range_ok) {
        throw std::invalid_argument(std::string(option) + " has an invalid numeric value");
    }
    return parsed;
}

std::string next_value(int& index, int argc, char** argv, const char* option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string(option) + " requires a value");
    }
    ++index;
    return argv[index];
}

void print_usage(std::ostream& output, const char* program) {
    output
        << "Usage: " << program << " [options]\n\n"
        << "V4L2 capture options:\n"
        << "  --device PATH          Video node (default: /dev/video0)\n"
        << "  --width PIXELS         Requested width (default: 1280)\n"
        << "  --height PIXELS        Requested height (default: 720)\n"
        << "  --pixel-format FOURCC  Four-character format (default: RG10)\n"
        << "  --fps RATE             Requested frames/second (default: 60)\n"
        << "  --sensor-mode INDEX    Tegra sensor mode; 'none' skips it (default: 4)\n"
        << "  --buffers COUNT        MMAP buffers, 2..32 (default: 4)\n"
        << "  --frames COUNT         Stop after COUNT frames; 0 is unlimited\n"
        << "  --duration SECONDS     Stop after duration; 0 disables the limit\n"
        << "  --timeout-ms MS        Per-frame poll timeout (default: 2500)\n"
        << "  --save-frame PATH      Save one dequeued RAW frame\n"
        << "  --save-at NUMBER       1-based frame to save (default: 1)\n"
        << "  --report-interval SEC  Progress report period (default: 1)\n"
        << "  --allow-drops          Return success despite gaps/buffer errors\n"
        << "  --list                 Query capabilities and enumerate modes only\n"
        << "  --help                 Show this help\n\n"
        << "Exit codes: 0=pass, 2=usage, 3=device/I/O, 4=timeout, "
           "5=sequence validation.\n";
}

std::optional<Options> parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout, argv[0]);
            return std::nullopt;
        }
        if (argument == "--device") {
            options.device = next_value(index, argc, argv, "--device");
        } else if (argument == "--width") {
            const auto value = parse_unsigned(next_value(index, argc, argv, "--width"),
                                              "--width");
            if (value == 0U || value > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--width is out of range");
            }
            options.width = static_cast<std::uint32_t>(value);
        } else if (argument == "--height") {
            const auto value = parse_unsigned(next_value(index, argc, argv, "--height"),
                                              "--height");
            if (value == 0U || value > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--height is out of range");
            }
            options.height = static_cast<std::uint32_t>(value);
        } else if (argument == "--pixel-format") {
            options.pixel_format =
                parse_fourcc(next_value(index, argc, argv, "--pixel-format"));
        } else if (argument == "--fps") {
            options.fps = parse_positive_double(
                next_value(index, argc, argv, "--fps"), "--fps", false);
        } else if (argument == "--frames") {
            options.frame_limit = parse_unsigned(
                next_value(index, argc, argv, "--frames"), "--frames");
        } else if (argument == "--duration") {
            options.duration_seconds = parse_positive_double(
                next_value(index, argc, argv, "--duration"), "--duration", true);
        } else if (argument == "--timeout-ms") {
            const auto value = parse_unsigned(
                next_value(index, argc, argv, "--timeout-ms"), "--timeout-ms");
            if (value == 0U || value > static_cast<std::uint64_t>(INT_MAX)) {
                throw std::invalid_argument("--timeout-ms is out of range");
            }
            options.timeout_ms = static_cast<int>(value);
        } else if (argument == "--buffers") {
            const auto value = parse_unsigned(
                next_value(index, argc, argv, "--buffers"), "--buffers");
            if (value < 2U || value > 32U) {
                throw std::invalid_argument("--buffers must be between 2 and 32");
            }
            options.buffer_count = static_cast<std::uint32_t>(value);
        } else if (argument == "--sensor-mode") {
            const std::string value = next_value(index, argc, argv, "--sensor-mode");
            if (value == "none") {
                options.sensor_mode.reset();
            } else {
                const auto parsed = parse_unsigned(value, "--sensor-mode");
                if (parsed > static_cast<std::uint64_t>(
                                 std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument("--sensor-mode is out of range");
                }
                options.sensor_mode = static_cast<std::int64_t>(parsed);
            }
        } else if (argument == "--save-frame") {
            options.save_path = next_value(index, argc, argv, "--save-frame");
        } else if (argument == "--save-at") {
            options.save_at = parse_unsigned(
                next_value(index, argc, argv, "--save-at"), "--save-at");
            if (options.save_at == 0U) {
                throw std::invalid_argument("--save-at is 1-based and must be positive");
            }
        } else if (argument == "--report-interval") {
            options.report_interval_seconds = parse_positive_double(
                next_value(index, argc, argv, "--report-interval"),
                "--report-interval", false);
        } else if (argument == "--list") {
            options.list_only = true;
        } else if (argument == "--allow-drops") {
            options.allow_drops = true;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (!options.list_only && options.frame_limit == 0U &&
        options.duration_seconds == 0.0) {
        std::cerr << "NOTICE limits=none stop_with=SIGINT_or_SIGTERM\n";
    }
    if (options.save_path && options.frame_limit != 0U &&
        options.save_at > options.frame_limit) {
        throw std::invalid_argument("--save-at exceeds --frames");
    }
    return options;
}

class V4l2Device {
public:
    explicit V4l2Device(const std::string& path) : path_(path) {
        fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            throw SystemError("open " + path, errno);
        }
    }

    V4l2Device(const V4l2Device&) = delete;
    V4l2Device& operator=(const V4l2Device&) = delete;

    ~V4l2Device() {
        stop_streaming_noexcept();
        for (const auto& buffer : buffers_) {
            if (buffer.address != MAP_FAILED) {
                ::munmap(buffer.address, buffer.length);
            }
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    void query_capabilities() {
        v4l2_capability capability{};
        if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) {
            throw SystemError("VIDIOC_QUERYCAP", errno);
        }
        const std::uint32_t capabilities =
            (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0U
                ? capability.device_caps
                : capability.capabilities;
        if ((capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0U) {
            throw std::runtime_error(path_ + " is not a single-plane capture device");
        }
        if ((capabilities & V4L2_CAP_STREAMING) == 0U) {
            throw std::runtime_error(path_ + " does not support streaming I/O");
        }
        std::cout << "CAPABILITY driver=" << fixed_string(capability.driver)
                  << " card=\"" << fixed_string(capability.card) << "\" bus=\""
                  << fixed_string(capability.bus_info) << "\" version="
                  << ((capability.version >> 16U) & 0xffU) << '.'
                  << ((capability.version >> 8U) & 0xffU) << '.'
                  << (capability.version & 0xffU) << " streaming=true\n";
    }

    void enumerate_formats() const {
        for (std::uint32_t format_index = 0;; ++format_index) {
            v4l2_fmtdesc format{};
            format.index = format_index;
            format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (xioctl(fd_, VIDIOC_ENUM_FMT, &format) < 0) {
                if (errno == EINVAL) {
                    break;
                }
                throw SystemError("VIDIOC_ENUM_FMT", errno);
            }
            std::cout << "FORMAT index=" << format_index
                      << " fourcc=" << fourcc_to_string(format.pixelformat)
                      << " description=\"" << fixed_string(format.description)
                      << "\"\n";
            enumerate_sizes(format.pixelformat);
        }
    }

    void set_sensor_mode(std::int64_t mode) const {
        v4l2_query_ext_ctrl query{};
        query.id = kTegraSensorModeControlId;
        if (xioctl(fd_, VIDIOC_QUERY_EXT_CTRL, &query) < 0) {
            throw SystemError("VIDIOC_QUERY_EXT_CTRL(sensor_mode)", errno);
        }
        if ((query.flags & V4L2_CTRL_FLAG_READ_ONLY) != 0U) {
            throw std::runtime_error("sensor_mode control is read-only");
        }
        if (mode < query.minimum || mode > query.maximum) {
            throw std::invalid_argument("sensor mode is outside driver range " +
                                        std::to_string(query.minimum) + ".." +
                                        std::to_string(query.maximum));
        }

        v4l2_ext_control control{};
        control.id = kTegraSensorModeControlId;
        control.value64 = mode;
        v4l2_ext_controls controls{};
        controls.which = V4L2_CTRL_WHICH_CUR_VAL;
        controls.count = 1;
        controls.controls = &control;
        if (xioctl(fd_, VIDIOC_S_EXT_CTRLS, &controls) < 0) {
            throw SystemError("VIDIOC_S_EXT_CTRLS(sensor_mode)", errno);
        }
        std::cout << "CONTROL sensor_mode=" << control.value64 << '\n';
    }

    v4l2_pix_format set_format(const Options& options) const {
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = options.width;
        format.fmt.pix.height = options.height;
        format.fmt.pix.pixelformat = options.pixel_format;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(fd_, VIDIOC_S_FMT, &format) < 0) {
            throw SystemError("VIDIOC_S_FMT", errno);
        }
        const auto& pixel = format.fmt.pix;
        std::cout << "NEGOTIATED width=" << pixel.width << " height=" << pixel.height
                  << " fourcc=" << fourcc_to_string(pixel.pixelformat)
                  << " bytesperline=" << pixel.bytesperline
                  << " sizeimage=" << pixel.sizeimage << '\n';
        return pixel;
    }

    double set_frame_rate(double requested_fps) const {
        constexpr std::uint32_t scale = 1000;
        const double denominator_value = requested_fps * static_cast<double>(scale);
        if (denominator_value >
            static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::invalid_argument("--fps is too large");
        }
        std::uint32_t numerator = scale;
        std::uint32_t denominator =
            static_cast<std::uint32_t>(std::llround(denominator_value));
        const std::uint32_t divisor = std::gcd(numerator, denominator);
        numerator /= divisor;
        denominator /= divisor;

        v4l2_streamparm parameters{};
        parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parameters.parm.capture.timeperframe.numerator = numerator;
        parameters.parm.capture.timeperframe.denominator = denominator;
        if (xioctl(fd_, VIDIOC_S_PARM, &parameters) < 0) {
            throw SystemError("VIDIOC_S_PARM", errno);
        }
        const auto& interval = parameters.parm.capture.timeperframe;
        const double negotiated = interval.numerator == 0U
                                      ? 0.0
                                      : static_cast<double>(interval.denominator) /
                                            static_cast<double>(interval.numerator);
        std::cout << std::fixed << std::setprecision(3)
                  << "FRAME_RATE requested_fps=" << requested_fps
                  << " negotiated_fps=" << negotiated << '\n';
        return negotiated;
    }

    void allocate_buffers(std::uint32_t requested_count) {
        v4l2_requestbuffers request{};
        request.count = requested_count;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
            throw SystemError("VIDIOC_REQBUFS", errno);
        }
        if (request.count < 2U) {
            throw std::runtime_error("driver allocated fewer than two MMAP buffers");
        }

        buffers_.reserve(request.count);
        for (std::uint32_t index = 0; index < request.count; ++index) {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = index;
            if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
                throw SystemError("VIDIOC_QUERYBUF", errno);
            }
            void* address = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd_, buffer.m.offset);
            if (address == MAP_FAILED) {
                throw SystemError("mmap", errno);
            }
            buffers_.push_back({address, static_cast<std::size_t>(buffer.length)});
        }
        std::cout << "MMAP buffers=" << buffers_.size() << '\n';
    }

    void start_streaming() {
        for (std::uint32_t index = 0; index < buffers_.size(); ++index) {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = index;
            if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
                throw SystemError("VIDIOC_QBUF(initial)", errno);
            }
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
            throw SystemError("VIDIOC_STREAMON", errno);
        }
        streaming_ = true;
        std::cout << "STREAM state=ON\n";
    }

    bool wait_for_frame(int timeout_ms) const {
        pollfd descriptor{};
        descriptor.fd = fd_;
        descriptor.events = POLLIN | POLLPRI;
        int result = 0;
        do {
            result = ::poll(&descriptor, 1, timeout_ms);
        } while (result < 0 && errno == EINTR && g_stop_signal == 0);
        if (result < 0) {
            if (errno == EINTR && g_stop_signal != 0) {
                return true;
            }
            throw SystemError("poll", errno);
        }
        if (result == 0) {
            return false;
        }
        if ((descriptor.revents & (POLLNVAL | POLLHUP)) != 0) {
            throw std::runtime_error("capture device became unavailable");
        }
        return true;
    }

    std::optional<v4l2_buffer> dequeue() const {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
            if (errno == EAGAIN) {
                return std::nullopt;
            }
            throw SystemError("VIDIOC_DQBUF", errno);
        }
        if (buffer.index >= buffers_.size() ||
            static_cast<std::size_t>(buffer.bytesused) > buffers_[buffer.index].length) {
            throw std::runtime_error("driver returned an invalid capture buffer");
        }
        return buffer;
    }

    void requeue(const v4l2_buffer& dequeued) const {
        v4l2_buffer buffer = dequeued;
        if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
            throw SystemError("VIDIOC_QBUF", errno);
        }
    }

    void save_buffer(const v4l2_buffer& buffer,
                     const std::filesystem::path& path) const {
        const int output_fd =
            ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (output_fd < 0) {
            throw SystemError("open RAW output " + path.string(), errno);
        }
        const auto* data = static_cast<const std::uint8_t*>(buffers_[buffer.index].address);
        std::size_t remaining = buffer.bytesused;
        std::size_t offset = 0;
        while (remaining != 0U) {
            const ssize_t written = ::write(output_fd, data + offset, remaining);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                const int saved_errno = written < 0 ? errno : EIO;
                ::close(output_fd);
                throw SystemError("write RAW output " + path.string(), saved_errno);
            }
            const auto amount = static_cast<std::size_t>(written);
            offset += amount;
            remaining -= amount;
        }
        if (::close(output_fd) < 0) {
            throw SystemError("close RAW output " + path.string(), errno);
        }
        std::cout << "SNAPSHOT path=\"" << path.string() << "\" bytes="
                  << buffer.bytesused << " sequence=" << buffer.sequence << '\n';
    }

    void stop_streaming() {
        if (!streaming_) {
            return;
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
            throw SystemError("VIDIOC_STREAMOFF", errno);
        }
        streaming_ = false;
        std::cout << "STREAM state=OFF\n";
    }

private:
    void enumerate_sizes(std::uint32_t pixel_format) const {
        for (std::uint32_t size_index = 0;; ++size_index) {
            v4l2_frmsizeenum size{};
            size.index = size_index;
            size.pixel_format = pixel_format;
            if (xioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &size) < 0) {
                if (errno == EINVAL) {
                    break;
                }
                throw SystemError("VIDIOC_ENUM_FRAMESIZES", errno);
            }
            if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                std::cout << "SIZE fourcc=" << fourcc_to_string(pixel_format)
                          << " width=" << size.discrete.width
                          << " height=" << size.discrete.height << '\n';
                enumerate_intervals(pixel_format, size.discrete.width,
                                    size.discrete.height);
            } else {
                std::cout << "SIZE_RANGE fourcc=" << fourcc_to_string(pixel_format)
                          << " min_width=" << size.stepwise.min_width
                          << " max_width=" << size.stepwise.max_width
                          << " step_width=" << size.stepwise.step_width
                          << " min_height=" << size.stepwise.min_height
                          << " max_height=" << size.stepwise.max_height
                          << " step_height=" << size.stepwise.step_height << '\n';
            }
        }
    }

    void enumerate_intervals(std::uint32_t pixel_format, std::uint32_t width,
                             std::uint32_t height) const {
        for (std::uint32_t interval_index = 0;; ++interval_index) {
            v4l2_frmivalenum interval{};
            interval.index = interval_index;
            interval.pixel_format = pixel_format;
            interval.width = width;
            interval.height = height;
            if (xioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0) {
                if (errno == EINVAL) {
                    break;
                }
                throw SystemError("VIDIOC_ENUM_FRAMEINTERVALS", errno);
            }
            if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                const double fps = interval.discrete.numerator == 0U
                                       ? 0.0
                                       : static_cast<double>(interval.discrete.denominator) /
                                             static_cast<double>(interval.discrete.numerator);
                std::cout << std::fixed << std::setprecision(3)
                          << "INTERVAL fourcc=" << fourcc_to_string(pixel_format)
                          << " width=" << width << " height=" << height
                          << " numerator=" << interval.discrete.numerator
                          << " denominator=" << interval.discrete.denominator
                          << " fps=" << fps << '\n';
            } else {
                std::cout << "INTERVAL_RANGE fourcc="
                          << fourcc_to_string(pixel_format) << " width=" << width
                          << " height=" << height << '\n';
            }
        }
    }

    void stop_streaming_noexcept() noexcept {
        if (!streaming_) {
            return;
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }

    std::string path_;
    int fd_ = -1;
    std::vector<MappedBuffer> buffers_;
    bool streaming_ = false;
};

double buffer_timestamp_seconds(const v4l2_buffer& buffer) {
    return static_cast<double>(buffer.timestamp.tv_sec) +
           static_cast<double>(buffer.timestamp.tv_usec) / 1'000'000.0;
}

ExitCode run_capture(const Options& options) {
    V4l2Device device(options.device);
    device.query_capabilities();
    if (options.list_only) {
        device.enumerate_formats();
        std::cout << "RESULT status=PASS operation=list\n";
        return ExitCode::success;
    }

    if (options.sensor_mode) {
        device.set_sensor_mode(*options.sensor_mode);
    }
    const v4l2_pix_format negotiated = device.set_format(options);
    const double negotiated_fps = device.set_frame_rate(options.fps);
    device.allocate_buffers(options.buffer_count);
    device.start_streaming();

    jetcam::CaptureStats stats;
    const auto wall_start = std::chrono::steady_clock::now();
    auto next_report = wall_start +
                       std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                           std::chrono::duration<double>(options.report_interval_seconds));
    bool saved = false;

    while (g_stop_signal == 0) {
        const auto now = std::chrono::steady_clock::now();
        const double wall_seconds = std::chrono::duration<double>(now - wall_start).count();
        if ((options.frame_limit != 0U && stats.frames() >= options.frame_limit) ||
            (options.duration_seconds > 0.0 &&
             wall_seconds >= options.duration_seconds)) {
            break;
        }
        if (!device.wait_for_frame(options.timeout_ms)) {
            std::cerr << "RESULT status=TIMEOUT frames=" << stats.frames()
                      << " timeout_ms=" << options.timeout_ms << '\n';
            return ExitCode::timeout;
        }
        if (g_stop_signal != 0) {
            break;
        }
        const auto maybe_buffer = device.dequeue();
        if (!maybe_buffer) {
            continue;
        }
        const v4l2_buffer buffer = *maybe_buffer;
        const bool buffer_error = (buffer.flags & V4L2_BUF_FLAG_ERROR) != 0U;
        stats.add_frame(buffer.sequence, buffer_timestamp_seconds(buffer), buffer_error);
        if (options.save_path && !saved && stats.frames() == options.save_at) {
            if (buffer_error) {
                device.requeue(buffer);
                throw std::runtime_error("refusing to save a buffer flagged as erroneous");
            }
            device.save_buffer(buffer, *options.save_path);
            saved = true;
        }
        device.requeue(buffer);

        const auto report_now = std::chrono::steady_clock::now();
        if (report_now >= next_report) {
            std::cout << std::fixed << std::setprecision(3)
                      << "PROGRESS frames=" << stats.frames()
                      << " fps=" << stats.fps()
                      << " dropped=" << stats.dropped_frames()
                      << " buffer_errors=" << stats.buffer_errors() << '\n';
            next_report = report_now +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(
                                  options.report_interval_seconds));
        }
    }

    device.stop_streaming();
    const double wall_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - wall_start)
                                    .count();
    if (g_stop_signal != 0) {
        std::cout << "RESULT status=INTERRUPTED signal=" << g_stop_signal
                  << " frames=" << stats.frames() << '\n';
        return static_cast<ExitCode>(128 + g_stop_signal);
    }
    if (options.save_path && !saved) {
        throw std::runtime_error("capture ended before the requested RAW frame was saved");
    }

    const bool sequence_ok = stats.dropped_frames() == 0U &&
                             stats.sequence_discontinuities() == 0U &&
                             stats.buffer_errors() == 0U;
    const char* status = (sequence_ok || options.allow_drops) ? "PASS" : "FAIL";
    std::cout << std::fixed << std::setprecision(3)
              << "RESULT status=" << status << " frames=" << stats.frames()
              << " first_sequence=" << stats.first_sequence()
              << " last_sequence=" << stats.last_sequence()
              << " dropped=" << stats.dropped_frames()
              << " discontinuities=" << stats.sequence_discontinuities()
              << " buffer_errors=" << stats.buffer_errors()
              << " capture_seconds=" << stats.elapsed_seconds()
              << " wall_seconds=" << wall_seconds << " actual_fps=" << stats.fps()
              << " requested_fps=" << options.fps
              << " negotiated_fps=" << negotiated_fps
              << " width=" << negotiated.width << " height=" << negotiated.height
              << " fourcc=" << fourcc_to_string(negotiated.pixelformat)
              << " snapshot_saved=" << (saved ? "true" : "false") << '\n';
    return sequence_ok || options.allow_drops ? ExitCode::success
                                              : ExitCode::validation_failed;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction action {};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    try {
        const auto options = parse_options(argc, argv);
        if (!options) {
            return static_cast<int>(ExitCode::success);
        }
        return static_cast<int>(run_capture(*options));
    } catch (const std::invalid_argument& error) {
        std::cerr << "ERROR category=usage message=\"" << error.what() << "\"\n";
        print_usage(std::cerr, argv[0]);
        return static_cast<int>(ExitCode::usage);
    } catch (const SystemError& error) {
        std::cerr << "ERROR category=system errno=" << error.error_number()
                  << " message=\"" << error.what() << "\"\n";
        return static_cast<int>(ExitCode::device_error);
    } catch (const std::exception& error) {
        std::cerr << "ERROR category=device message=\"" << error.what() << "\"\n";
        return static_cast<int>(ExitCode::device_error);
    }
}
