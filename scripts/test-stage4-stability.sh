#!/usr/bin/env bash
set -euo pipefail

binary=${JETCAMD_BIN:-./build/app/jetcamd/jetcamd}
config=${JETCAMD_CONFIG:-./config/jetcamd.conf}
duration=${JETCAM_STABILITY_SECONDS:-1800}
output_dir=${1:-stage4-stability-output}

mkdir -p "$output_dir"
daemon_log="$output_dir/jetcamd-720p60-${duration}s.log"
vlc_log="$output_dir/vlc-720p60-${duration}s.log"
system_log="$output_dir/tegrastats-720p60-${duration}s.log"

jetcamd_pid=
tegrastats_pid=
cleanup() {
    if [[ -n "$jetcamd_pid" ]] && kill -0 "$jetcamd_pid" 2>/dev/null; then
        kill -INT "$jetcamd_pid" 2>/dev/null || true
        wait "$jetcamd_pid" 2>/dev/null || true
    fi
    if [[ -n "$tegrastats_pid" ]] && kill -0 "$tegrastats_pid" 2>/dev/null; then
        kill "$tegrastats_pid" 2>/dev/null || true
        wait "$tegrastats_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

date --iso-8601=seconds >"$output_dir/start-time.txt"
"$binary" --config "$config" \
    --set output=udp --set host=127.0.0.1 --set port=5000 \
    --set width=1280 --set height=720 --set framerate=60 \
    --set bitrate_kbps=6000 --set gop=60 >"$daemon_log" 2>&1 &
jetcamd_pid=$!

if command -v tegrastats >/dev/null; then
    tegrastats --interval 5000 >"$system_log" 2>&1 &
    tegrastats_pid=$!
fi

sleep 7
kill -0 "$jetcamd_pid"

set +e
timeout --signal=INT "$((duration + 15))" \
    cvlc -I dummy --verbose=2 --no-video-title-show --vout dummy --aout dummy \
    --network-caching=100 --run-time="$duration" --play-and-exit \
    "udp://@:5000" >"$vlc_log" 2>&1
vlc_status=$?
set -e
if [[ "$vlc_status" -ne 0 && "$vlc_status" -ne 124 ]]; then
    echo "VLC failed with status $vlc_status" >&2
    exit 1
fi

kill -0 "$jetcamd_pid"
kill -INT "$jetcamd_pid"
wait "$jetcamd_pid"
jetcamd_pid=
if [[ -n "$tegrastats_pid" ]]; then
    kill "$tegrastats_pid" 2>/dev/null || true
    wait "$tegrastats_pid" 2>/dev/null || true
    tegrastats_pid=
fi
date --iso-8601=seconds >"$output_dir/end-time.txt"

grep -q 'state=STREAMING' "$daemon_log"
grep -q 'state=STOPPED' "$daemon_log"
if grep -Eq 'state=ERROR|pipeline_error=' "$daemon_log"; then
    echo "jetcamd reported a pipeline error" >&2
    exit 1
fi
grep -q 'using demux module "ts"' "$vlc_log"
grep -q 'codec (h264) started' "$vlc_log"
grep -q 'using video decoder module "avcodec"' "$vlc_log"

awk '
    /metric=frames/ {
        for (i = 1; i <= NF; ++i) {
            if ($i ~ /^fps=/) {
                split($i, part, "=")
                value = part[2] + 0
                if (value >= 55) good++
                samples++
            }
        }
    }
    END {
        if (samples < 2 || good < samples - 1) exit 1
    }
' "$daemon_log"

echo "STAGE4_STABILITY_RESULT=PASS duration_seconds=$duration"
