#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BINARY="${JETCAM_RAW_TEST_BIN:-$BUILD_DIR/app/jetcam-raw-test/jetcam-raw-test}"
DEVICE="${JETCAM_DEVICE:-/dev/video0}"
DURATION_SECONDS="${DURATION_SECONDS:-600}"
TIMEOUT_MS="${TIMEOUT_MS:-2500}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUTPUT_DIR="${OUTPUT_DIR:-out/stage2-$STAMP}"
RAW_PATH="$OUTPUT_DIR/snapshot-720p.rg10.raw"

mkdir -p "$OUTPUT_DIR"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$OUTPUT_DIR/ctest.log"

"$BINARY" --device "$DEVICE" --list | tee "$OUTPUT_DIR/modes.log"

"$BINARY" \
    --device "$DEVICE" \
    --sensor-mode 4 \
    --width 1280 \
    --height 720 \
    --pixel-format RG10 \
    --fps 60 \
    --frames 0 \
    --duration "$DURATION_SECONDS" \
    --timeout-ms "$TIMEOUT_MS" \
    | tee "$OUTPUT_DIR/stability-720p60.log"

"$BINARY" \
    --device "$DEVICE" \
    --sensor-mode 4 \
    --width 1280 \
    --height 720 \
    --pixel-format RG10 \
    --fps 60 \
    --frames 2 \
    --timeout-ms "$TIMEOUT_MS" \
    --save-frame "$RAW_PATH" \
    --save-at 1 \
    | tee "$OUTPUT_DIR/snapshot-and-close.log"

test -s "$RAW_PATH"
test "$(wc -c <"$RAW_PATH")" -eq 1843200

"$BINARY" \
    --device "$DEVICE" \
    --sensor-mode 4 \
    --width 1280 \
    --height 720 \
    --pixel-format RG10 \
    --fps 60 \
    --frames 2 \
    --timeout-ms "$TIMEOUT_MS" \
    | tee "$OUTPUT_DIR/immediate-reopen.log"

echo "Stage 2 acceptance passed; logs: $OUTPUT_DIR"
