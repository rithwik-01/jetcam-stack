#include "jetcam/pipeline.hpp"

#include <sstream>

namespace jetcam {
namespace {

std::string capture_and_encode(const Config& config) {
    std::ostringstream pipeline;
    pipeline << "nvarguscamerasrc sensor-id=" << config.sensor_id
             << " ! video/x-raw(memory:NVMM),format=NV12,width=" << config.width
             << ",height=" << config.height << ",framerate=" << config.framerate << "/1"
             << " ! nvvidconv"
             << " ! video/x-raw,format=I420"
             << " ! identity name=frame_probe silent=true"
             << " ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=" << config.bitrate_kbps
             << " key-int-max=" << config.gop
             << " bframes=0 byte-stream=true aud=true sliced-threads=true"
             << " ! video/x-h264,profile=baseline"
             << " ! h264parse config-interval=1";
    return pipeline.str();
}

}  // namespace

std::string build_udp_pipeline(const Config& config) {
    std::ostringstream pipeline;
    pipeline << "nvarguscamerasrc sensor-id=" << config.sensor_id
             << " ! video/x-raw(memory:NVMM),format=NV12,width=" << config.width
             << ",height=" << config.height << ",framerate=" << config.framerate << "/1"
             << " ! nvvidconv"
             << " ! video/x-raw,format=I420"
             << " ! tee name=rawtee "
             << "rawtee. ! queue max-size-buffers=4 leaky=downstream"
             << " ! identity name=frame_probe silent=true"
             << " ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=" << config.bitrate_kbps
             << " key-int-max=" << config.gop
             << " bframes=0 byte-stream=true aud=true sliced-threads=true"
             << " ! video/x-h264,profile=baseline"
             << " ! h264parse config-interval=1"
             << " ! mpegtsmux alignment=7"
             << " ! udpsink host=" << config.host << " port=" << config.port
             << " sync=false async=false "
             << "rawtee. ! queue max-size-buffers=1 leaky=downstream"
             << " ! valve name=snapshot_valve drop=true"
             << " ! videoconvert ! jpegenc quality=90"
             << " ! appsink name=snapshot_sink emit-signals=true max-buffers=1 drop=true sync=false";
    return pipeline.str();
}

std::string build_rtsp_capture_pipeline(const Config& config) {
    return capture_and_encode(config) +
           " ! rtph264pay pt=96 config-interval=1"
           " ! udpsink host=127.0.0.1 port=5400 sync=false async=false";
}

std::string build_rtsp_factory_pipeline() {
    return "( udpsrc address=127.0.0.1 port=5400 "
           "caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000\""
           " ! rtph264depay ! rtph264pay name=pay0 pt=96 config-interval=1 )";
}

}  // namespace jetcam
