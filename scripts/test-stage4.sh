#!/usr/bin/env bash
set -euo pipefail

binary=${JETCAMD_BIN:-./build/app/jetcamd/jetcamd}
config=${JETCAMD_CONFIG:-./config/jetcamd.conf}
output_dir=${1:-stage4-test-output}
vlc_seconds=${JETCAM_VLC_SECONDS:-12}

mkdir -p "$output_dir"

for element in nvarguscamerasrc nvvidconv x264enc mpegtsmux jpegenc rtph264pay; do
    gst-inspect-1.0 "$element" >/dev/null
done
test -x "$binary"
test -r "$config"
command -v cvlc >/dev/null

active_pid=
cleanup() {
    if [[ -n "$active_pid" ]] && kill -0 "$active_pid" 2>/dev/null; then
        kill -INT "$active_pid" 2>/dev/null || true
        wait "$active_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

assert_fps() {
    local log=$1
    local minimum=$2
    awk -v minimum="$minimum" '
        /metric=frames/ {
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^fps=/) {
                    split($i, part, "=")
                    if (part[2] + 0 >= minimum) ok = 1
                }
            }
        }
        END { exit ok ? 0 : 1 }
    ' "$log"
}

run_udp_mode() {
    local label=$1
    local width=$2
    local height=$3
    local fps=$4
    local bitrate=$5
    local minimum_fps=$6
    local daemon_log="$output_dir/${label}-jetcamd.log"
    local vlc_log="$output_dir/${label}-vlc.log"
    local snapshot="$output_dir/${label}-snapshot.jpg"

    "$binary" --config "$config" \
        --set output=udp \
        --set host=127.0.0.1 \
        --set port=5000 \
        --set width="$width" \
        --set height="$height" \
        --set framerate="$fps" \
        --set bitrate_kbps="$bitrate" \
        --set gop="$fps" \
        --set snapshot_file="$snapshot" \
        --set snapshot_on_start=true >"$daemon_log" 2>&1 &
    active_pid=$!

    # Deliberately leave UDP without a receiver first. udpsink must not terminate.
    sleep 7
    kill -0 "$active_pid"

    set +e
    timeout --signal=INT "$((vlc_seconds + 5))" \
        cvlc -I dummy --verbose=2 --no-video-title-show --vout dummy --aout dummy \
        --network-caching=100 --run-time="$vlc_seconds" --play-and-exit \
        "udp://@:5000" >"$vlc_log" 2>&1
    local vlc_status=$?
    set -e
    if [[ "$vlc_status" -ne 0 && "$vlc_status" -ne 124 ]]; then
        echo "VLC failed for $label with status $vlc_status" >&2
        return 1
    fi

    # The sender must survive a receiver disconnect as well.
    sleep 6
    kill -0 "$active_pid"
    kill -INT "$active_pid"
    wait "$active_pid"
    active_pid=

    grep -q 'state=STREAMING' "$daemon_log"
    grep -q 'snapshot=saved' "$daemon_log"
    grep -q 'state=STOPPED' "$daemon_log"
    test -s "$snapshot"
    file "$snapshot" | grep -q 'JPEG image data'
    assert_fps "$daemon_log" "$minimum_fps"
    grep -q 'using demux module "ts"' "$vlc_log"
    grep -q 'codec (h264) started' "$vlc_log"
    grep -q 'using video decoder module "avcodec"' "$vlc_log"
}

run_rtsp() {
    local daemon_log="$output_dir/rtsp-jetcamd.log"
    local client_log="$output_dir/rtsp-gstreamer-client.log"

    "$binary" --config "$config" \
        --set output=rtsp \
        --set width=1280 --set height=720 --set framerate=60 \
        --set bitrate_kbps=6000 --set gop=60 >"$daemon_log" 2>&1 &
    active_pid=$!
    sleep 3
    # Give the shared factory time to tear down Argus after the last client leaves.
    sleep 3
    kill -0 "$active_pid"

    set +e
    timeout --signal=INT "$((vlc_seconds + 5))" \
        gst-launch-1.0 -e rtspsrc location=rtsp://127.0.0.1:8554/jetcam \
        latency=100 protocols=tcp \
        ! rtph264depay ! h264parse ! fakesink sync=false >"$client_log" 2>&1
    local client_status=$?
    set -e
    if [[ "$client_status" -ne 0 && "$client_status" -ne 124 ]]; then
        echo "RTSP client failed with status $client_status" >&2
        return 1
    fi

    kill -0 "$active_pid"
    kill -INT "$active_pid"
    wait "$active_pid"
    active_pid=
    grep -q 'state=STREAMING url=rtsp://' "$daemon_log"
    grep -q 'Setting pipeline to PLAYING' "$client_log"
    grep -q 'Redistribute latency' "$client_log"
    if grep -q '(Argus) Error' "$daemon_log"; then
        echo "Argus reported an RTSP teardown error" >&2
        return 1
    fi
}

run_udp_mode 720p60 1280 720 60 6000 55
run_udp_mode 1080p30 1920 1080 30 8000 27
run_rtsp

echo "STAGE4_RESULT=PASS"
