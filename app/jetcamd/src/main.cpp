#include "jetcam/config.hpp"
#include "jetcam/pipeline.hpp"
#include "jetcam/rtsp_server.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <glib-unix.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

struct Runtime {
    GMainLoop* loop = nullptr;
    GstElement* pipeline = nullptr;
    GstElement* snapshot_valve = nullptr;
    std::string snapshot_file;
    std::atomic<std::uint64_t> frames{0};
    std::uint64_t last_frames = 0;
    std::chrono::steady_clock::time_point last_report = std::chrono::steady_clock::now();
    int exit_code = 0;
};

gboolean quit_loop(gpointer data) {
    auto* runtime = static_cast<Runtime*>(data);
    g_main_loop_quit(runtime->loop);
    return G_SOURCE_REMOVE;
}

gboolean request_snapshot(gpointer data) {
    auto* runtime = static_cast<Runtime*>(data);
    if (runtime->snapshot_valve != nullptr) {
        g_object_set(runtime->snapshot_valve, "drop", FALSE, nullptr);
        std::cout << "snapshot=requested file=" << runtime->snapshot_file << std::endl;
    }
    return G_SOURCE_CONTINUE;
}

void on_frame(GstElement*, GstBuffer*, gpointer data) {
    auto* runtime = static_cast<Runtime*>(data);
    runtime->frames.fetch_add(1, std::memory_order_relaxed);
}

GstFlowReturn on_snapshot(GstAppSink* sink, gpointer data) {
    auto* runtime = static_cast<Runtime*>(data);
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (sample == nullptr) {
        return GST_FLOW_ERROR;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map{};
    bool saved = false;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        std::ofstream output(runtime->snapshot_file, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(map.data), static_cast<std::streamsize>(map.size));
        saved = output.good();
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    g_object_set(runtime->snapshot_valve, "drop", TRUE, nullptr);

    if (saved) {
        std::cout << "snapshot=saved file=" << runtime->snapshot_file << std::endl;
        return GST_FLOW_OK;
    }
    std::cerr << "snapshot=failed file=" << runtime->snapshot_file << std::endl;
    return GST_FLOW_ERROR;
}

gboolean report_fps(gpointer data) {
    auto* runtime = static_cast<Runtime*>(data);
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - runtime->last_report).count();
    const std::uint64_t frames = runtime->frames.load(std::memory_order_relaxed);
    const double fps = static_cast<double>(frames - runtime->last_frames) / seconds;
    std::cout << "metric=frames frames=" << frames << " fps=" << fps << std::endl;
    runtime->last_frames = frames;
    runtime->last_report = now;
    return G_SOURCE_CONTINUE;
}

gboolean on_bus_message(GstBus*, GstMessage* message, gpointer data) {
    auto* runtime = static_cast<Runtime*>(data);
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            std::cerr << "state=ERROR source=" << GST_OBJECT_NAME(message->src)
                      << " message=" << (error != nullptr ? error->message : "unknown") << '\n';
            if (debug != nullptr) {
                std::cerr << "debug=" << debug << '\n';
            }
            g_clear_error(&error);
            g_free(debug);
            runtime->exit_code = 1;
            g_main_loop_quit(runtime->loop);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cerr << "state=EOS\n";
            g_main_loop_quit(runtime->loop);
            break;
        default:
            break;
    }
    return G_SOURCE_CONTINUE;
}

void usage(const char* program) {
    std::cout << "Usage: " << program << " [--config FILE] [--set KEY=VALUE] [--print-pipeline]\n"
              << "       Send SIGUSR1 to save one JPEG to snapshot_file.\n";
}

}  // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    jetcam::Config config;
    bool print_pipeline = false;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--config" && index + 1 < argc) {
                config = jetcam::load_config(argv[++index]);
            } else if (argument == "--set" && index + 1 < argc) {
                const std::string assignment = argv[++index];
                const auto separator = assignment.find('=');
                if (separator == std::string::npos) {
                    throw std::runtime_error("--set expects KEY=VALUE");
                }
                jetcam::apply_assignment(config, assignment.substr(0, separator),
                                         assignment.substr(separator + 1));
            } else if (argument == "--print-pipeline") {
                print_pipeline = true;
            } else if (argument == "--help") {
                usage(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("unknown or incomplete argument: " + argument);
            }
        }
        jetcam::validate_config(config);
    } catch (const std::exception& error) {
        std::cerr << "configuration_error=" << error.what() << '\n';
        usage(argv[0]);
        return 2;
    }

    jetcam::print_config(config, std::cout);
    const std::string pipeline_text = config.output == jetcam::OutputMode::udp
                                          ? jetcam::build_udp_pipeline(config)
                                          : jetcam::build_rtsp_capture_pipeline(config);
    if (print_pipeline) {
        std::cout << pipeline_text << '\n';
        if (config.output == jetcam::OutputMode::rtsp) {
            std::cout << jetcam::build_rtsp_factory_pipeline() << '\n';
        }
        return 0;
    }

    Runtime runtime;
    runtime.loop = g_main_loop_new(nullptr, FALSE);
    runtime.snapshot_file = config.snapshot_file;
    g_unix_signal_add(SIGINT, quit_loop, &runtime);
    g_unix_signal_add(SIGTERM, quit_loop, &runtime);

    if (config.output == jetcam::OutputMode::rtsp) {
#ifdef JETCAM_HAVE_RTSP_SERVER
        const int result = jetcam::run_rtsp_server(config, runtime.loop);
        g_main_loop_unref(runtime.loop);
        return result;
#else
        std::cerr << "error=rtsp_not_built install=libgstrtspserver-1.0-dev\n";
        g_main_loop_unref(runtime.loop);
        return 3;
#endif
    }

    GError* parse_error = nullptr;
    runtime.pipeline = gst_parse_launch(pipeline_text.c_str(), &parse_error);
    if (runtime.pipeline == nullptr || parse_error != nullptr) {
        std::cerr << "pipeline_error="
                  << (parse_error != nullptr ? parse_error->message : "unknown parse failure") << '\n';
        g_clear_error(&parse_error);
        if (runtime.pipeline != nullptr) {
            gst_object_unref(runtime.pipeline);
        }
        g_main_loop_unref(runtime.loop);
        return 1;
    }

    GstElement* frame_probe = gst_bin_get_by_name(GST_BIN(runtime.pipeline), "frame_probe");
    runtime.snapshot_valve = gst_bin_get_by_name(GST_BIN(runtime.pipeline), "snapshot_valve");
    GstElement* snapshot_sink = gst_bin_get_by_name(GST_BIN(runtime.pipeline), "snapshot_sink");
    if (frame_probe == nullptr || runtime.snapshot_valve == nullptr || snapshot_sink == nullptr) {
        std::cerr << "pipeline_error=required named element missing\n";
        runtime.exit_code = 1;
        goto cleanup;
    }

    g_signal_connect(frame_probe, "handoff", G_CALLBACK(on_frame), &runtime);
    g_signal_connect(snapshot_sink, "new-sample", G_CALLBACK(on_snapshot), &runtime);
    g_unix_signal_add(SIGUSR1, request_snapshot, &runtime);

    {
        GstBus* bus = gst_element_get_bus(runtime.pipeline);
        gst_bus_add_watch(bus, on_bus_message, &runtime);
        gst_object_unref(bus);
    }
    g_timeout_add_seconds(5, report_fps, &runtime);

    if (gst_element_set_state(runtime.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "state=ERROR message=failed_to_enter_playing\n";
        runtime.exit_code = 1;
        goto cleanup;
    }
    std::cout << "state=STREAMING url=udp://" << config.host << ':' << config.port << std::endl;
    if (config.snapshot_on_start) {
        request_snapshot(&runtime);
    }
    g_main_loop_run(runtime.loop);

cleanup:
    gst_element_set_state(runtime.pipeline, GST_STATE_NULL);
    if (snapshot_sink != nullptr) {
        gst_object_unref(snapshot_sink);
    }
    if (runtime.snapshot_valve != nullptr) {
        gst_object_unref(runtime.snapshot_valve);
    }
    if (frame_probe != nullptr) {
        gst_object_unref(frame_probe);
    }
    gst_object_unref(runtime.pipeline);
    g_main_loop_unref(runtime.loop);
    std::cout << "state=STOPPED frames=" << runtime.frames.load() << std::endl;
    return runtime.exit_code;
}
