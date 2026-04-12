#include "jetcam/rtsp_server.hpp"

#include "jetcam/pipeline.hpp"

#include <gst/rtsp-server/rtsp-server.h>

#include <iostream>
#include <string>

namespace jetcam {
namespace {

struct RtspRuntime {
    GMainLoop* loop;
    int exit_code = 0;
};

gboolean on_capture_bus_message(GstBus*, GstMessage* message, gpointer data) {
    auto* runtime = static_cast<RtspRuntime*>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
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
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        std::cerr << "state=EOS source=rtsp_capture\n";
        g_main_loop_quit(runtime->loop);
    }
    return G_SOURCE_CONTINUE;
}

}  // namespace

int run_rtsp_server(const Config& config, GMainLoop* loop) {
    GError* parse_error = nullptr;
    const std::string capture_text = build_rtsp_capture_pipeline(config);
    GstElement* capture = gst_parse_launch(capture_text.c_str(), &parse_error);
    if (capture == nullptr || parse_error != nullptr) {
        std::cerr << "pipeline_error="
                  << (parse_error != nullptr ? parse_error->message : "RTSP capture parse failure")
                  << '\n';
        g_clear_error(&parse_error);
        if (capture != nullptr) {
            gst_object_unref(capture);
        }
        return 1;
    }

    GstRTSPServer* server = gst_rtsp_server_new();
    const std::string service = std::to_string(config.rtsp_port);
    gst_rtsp_server_set_service(server, service.c_str());

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    const std::string pipeline = build_rtsp_factory_pipeline();
    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_stop_on_disconnect(factory, TRUE);
    gst_rtsp_mount_points_add_factory(mounts, config.rtsp_mount.c_str(), factory);
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server, nullptr) == 0) {
        std::cerr << "error=failed_to_attach_rtsp_server\n";
        g_object_unref(server);
        gst_object_unref(capture);
        return 1;
    }

    RtspRuntime runtime{loop};
    GstBus* bus = gst_element_get_bus(capture);
    gst_bus_add_watch(bus, on_capture_bus_message, &runtime);
    gst_object_unref(bus);
    if (gst_element_set_state(capture, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "state=ERROR message=failed_to_start_rtsp_capture\n";
        g_object_unref(server);
        gst_object_unref(capture);
        return 1;
    }

    std::cout << "state=STREAMING url=rtsp://0.0.0.0:" << config.rtsp_port
              << config.rtsp_mount << std::endl;
    g_main_loop_run(loop);
    gst_element_set_state(capture, GST_STATE_NULL);
    gst_object_unref(capture);
    g_object_unref(server);
    return runtime.exit_code;
}

}  // namespace jetcam
