#!/usr/bin/env bash

set -euo pipefail

DEVICE="${JETCAM_DEVICE:-/dev/video0}"
BINARY="${JETCAM_RAW_TEST_BIN:-./install/bin/jetcam-raw-test}"
FRAMES="${FRAMES:-120}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUTPUT_DIR="${OUTPUT_DIR:-out/stage3-$STAMP}"
DISABLED_RAW="$OUTPUT_DIR/disabled.rg10.raw"
BARS_RAW="$OUTPUT_DIR/color-bars.rg10.raw"

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FRAMES must be a positive integer" >&2
    exit 2
fi
SAVE_AT=$((FRAMES < 60 ? FRAMES : 60))

mkdir -p "$OUTPUT_DIR"

restore_control() {
    v4l2-ctl -d "$DEVICE" --set-ctrl=test_pattern=0 >/dev/null 2>&1 || true
}
trap restore_control EXIT

test -c "$DEVICE"
test -x "$BINARY"

v4l2-ctl -d "$DEVICE" --list-ctrls-menus | tee "$OUTPUT_DIR/controls.log"
grep -q "test_pattern" "$OUTPUT_DIR/controls.log"
grep -q "Color Bars" "$OUTPUT_DIR/controls.log"

DIRECT_STATE="$(v4l2-ctl -d "$DEVICE" --get-ctrl=bypass_mode,override_enable)"
printf '%s\n' "$DIRECT_STATE" | tee "$OUTPUT_DIR/direct-v4l2-state.log"
if printf '%s\n' "$DIRECT_STATE" | grep -Eq '^(bypass_mode|override_enable): 1'; then
    cat >&2 <<'EOF'
Direct V4L2 is not in its reset state. Stop any Argus/jetcamd pipeline, then run:
  sudo modprobe -r nv_imx219 && sudo modprobe nv_imx219
Run this script again after /dev/video0 returns.
EOF
    exit 3
fi

v4l2-ctl -d "$DEVICE" --set-ctrl=test_pattern=0
"$BINARY" \
    --device "$DEVICE" --sensor-mode 4 \
    --width 1280 --height 720 --pixel-format RG10 --fps 60 \
    --frames "$FRAMES" --save-frame "$DISABLED_RAW" --save-at "$SAVE_AT" \
    | tee "$OUTPUT_DIR/disabled-capture.log"

v4l2-ctl -d "$DEVICE" --set-ctrl=test_pattern=2
"$BINARY" \
    --device "$DEVICE" --sensor-mode 4 \
    --width 1280 --height 720 --pixel-format RG10 --fps 60 \
    --frames "$FRAMES" --save-frame "$BARS_RAW" --save-at "$SAVE_AT" \
    | tee "$OUTPUT_DIR/color-bars-capture.log"

test "$(wc -c <"$DISABLED_RAW")" -eq 1843200
test "$(wc -c <"$BARS_RAW")" -eq 1843200
if cmp -s "$DISABLED_RAW" "$BARS_RAW"; then
    echo "FAIL: disabled and Color Bars RAW frames are identical" >&2
    exit 1
fi

restore_control
v4l2-ctl -d "$DEVICE" --get-ctrl=test_pattern | tee "$OUTPUT_DIR/restored-control.log"
grep -q "Disabled" "$OUTPUT_DIR/restored-control.log"
sha256sum "$DISABLED_RAW" "$BARS_RAW" | tee "$OUTPUT_DIR/raw-sha256.txt"

echo "Stage 3 acceptance passed; logs: $OUTPUT_DIR"
