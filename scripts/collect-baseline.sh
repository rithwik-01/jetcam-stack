#!/usr/bin/env bash

set -euo pipefail

REMOTE="${JETCAM_REMOTE:?Set JETCAM_REMOTE to user@host}"
OUTPUT_DIR="${1:-baseline/$(date +%Y-%m-%d)-jetson-orin-nano}"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
CONTROL_DIR="$(mktemp -d "${TMPDIR:-/tmp}/jetcam-ssh.XXXXXX")"
CONTROL_SOCKET="$CONTROL_DIR/control"

cleanup() {
    ssh -S "$CONTROL_SOCKET" -O exit "$REMOTE" >/dev/null 2>&1 || true
    rmdir "$CONTROL_DIR" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Opening SSH connection to $REMOTE (interactive authentication may be requested once)..."
ssh -M -S "$CONTROL_SOCKET" \
    -o ControlPersist=60 \
    -o ConnectTimeout=10 \
    -o StrictHostKeyChecking=accept-new \
    -N -f "$REMOTE"

collect() {
    local filename="$1"
    local target="$OUTPUT_DIR/$filename"
    local status

    echo "Collecting $filename"
    {
        printf '# host=<redacted>\n'
        printf '# collected_at=%s\n' "$(date --iso-8601=seconds 2>/dev/null || date '+%Y-%m-%dT%H:%M:%S%z')"
        printf '# generated_by=scripts/collect-baseline.sh\n\n'
        set +e
        ssh -T -S "$CONTROL_SOCKET" "$REMOTE" bash -s
        status=$?
        set -e
        printf '\n# ssh_exit_status=%d\n' "$status"
    } >"$target" 2>&1

    # Remove machine-specific identifiers before evidence can be committed.
    sed -E \
        -e 's/^([[:space:]]*Static hostname:).*/\1 <redacted>/' \
        -e 's/^([[:space:]]*Machine ID:).*/\1 <redacted>/' \
        -e 's/^([[:space:]]*Boot ID:).*/\1 <redacted>/' \
        -e 's/^Linux [^ ]+/Linux <redacted>/' \
        -e 's/PARTUUID=[^ ]+/PARTUUID=<redacted>/g' \
        "$target" >"$target.sanitized"
    mv "$target.sanitized" "$target"
}

collect system.txt <<'REMOTE'
set +e
echo '===== hostnamectl ====='
hostnamectl
echo '===== /etc/os-release ====='
cat /etc/os-release
echo '===== /etc/nv_tegra_release ====='
cat /etc/nv_tegra_release
echo '===== uname -a ====='
uname -a
echo '===== NVIDIA packages ====='
dpkg-query -W -f='${Package}\t${Version}\n' \
  'nvidia-jetpack*' nvidia-l4t-core nvidia-l4t-kernel nvidia-l4t-gstreamer
echo '===== tool versions ====='
v4l2-ctl --version
media-ctl --version
gst-launch-1.0 --version
echo '===== selected GStreamer plugins ====='
for plugin in nvarguscamerasrc nvv4l2h264enc nvvidconv; do
  echo "--- $plugin"
  gst-inspect-1.0 "$plugin" 2>&1 | sed -n '1,28p'
done
REMOTE

collect v4l2-all.txt <<'REMOTE'
v4l2-ctl --device=/dev/video0 --all
REMOTE

collect v4l2-formats.txt <<'REMOTE'
echo '===== pixel formats, sizes and frame intervals ====='
v4l2-ctl --device=/dev/video0 --list-formats-ext
echo '===== controls ====='
v4l2-ctl --device=/dev/video0 --list-ctrls-menus
REMOTE

collect media-topology.txt <<'REMOTE'
media-ctl --device=/dev/media0 --print-topology
REMOTE

collect kernel-driver.txt <<'REMOTE'
set +e
echo '===== device nodes ====='
ls -l /dev/video* /dev/media*
echo '===== video0 identity ====='
udevadm info -q property -n /dev/video0
echo '===== sysfs binding ====='
echo "video-device=$(readlink -f /sys/class/video4linux/video0/device)"
echo "video-driver=$(readlink -f /sys/class/video4linux/video0/device/driver)"
echo '===== relevant loaded modules ====='
lsmod | grep -Ei 'imx219|tegra_camera|nvhost_(vi|nvcsi|isp)'
echo '===== nv_imx219 module ====='
modinfo nv_imx219
echo '===== installed source/header packages ====='
dpkg-query -W -f='${Package}\t${Version}\n' \
  'nvidia-l4t-kernel*' 'linux-headers*' 2>/dev/null | sort
echo '===== installed IMX219 source/module paths ====='
find /usr/src "/lib/modules/$(uname -r)" -type f \
  \( -iname '*imx219*.c' -o -iname '*imx219*.ko*' \) 2>/dev/null | sort
echo '===== relevant kernel configuration ====='
config="/boot/config-$(uname -r)"
if [ -r "$config" ]; then
  grep -E 'CONFIG_(I2C|I2C_TEGRA|MEDIA_SUPPORT|MEDIA_CONTROLLER|VIDEO_DEV|VIDEO_V4L2|VIDEO_TEGRA|VIDEO_IMX219|NV_VIDEO_IMX219)=' "$config"
elif [ -r /proc/config.gz ]; then
  zcat /proc/config.gz | grep -E 'CONFIG_(I2C|I2C_TEGRA|MEDIA_SUPPORT|MEDIA_CONTROLLER|VIDEO_DEV|VIDEO_V4L2|VIDEO_TEGRA|VIDEO_IMX219|NV_VIDEO_IMX219)='
else
  echo 'No readable kernel config found'
fi
REMOTE

collect device-tree-camera.txt <<'REMOTE'
set +e
echo '===== running board model ====='
tr -d '\000' </proc/device-tree/model
echo
echo '===== active extlinux entry ====='
sed -n '1,220p' /boot/extlinux/extlinux.conf
echo '===== tegra-camera-platform modules ====='
base=/proc/device-tree/tegra-camera-platform/modules
if [ -d "$base" ]; then
  find "$base" -maxdepth 3 -type f -print | sort | while read -r property; do
    case "${property##*/}" in
      badge|position|orientation|status|pcl_id|devname|proc-device-tree|sysfs-device-tree)
        printf '%s=' "${property#/proc/device-tree/}"
        tr -d '\000' <"$property"
        echo
        ;;
    esac
  done
fi
echo '===== nodes compatible with IMX219 ====='
find -L /proc/device-tree -type f -name compatible -print 2>/dev/null | while read -r property; do
  if tr -d '\000' <"$property" | grep -qi imx219; then
    node="${property%/compatible}"
    echo "--- ${node#/proc/device-tree/}"
    find "$node" -maxdepth 1 -type f -print | sort | while read -r item; do
      case "${item##*/}" in
        compatible|status|sensor_model|use_sensor_mode_id|physical_w|physical_h)
          printf '%s=' "${item##*/}"
          tr -d '\000' <"$item"
          echo
          ;;
      esac
    done
  fi
done
echo '===== selected overlay decompile ====='
overlay=/boot/tegra234-p3767-camera-p3768-imx219-C.dtbo
if command -v dtc >/dev/null && [ -r "$overlay" ]; then
  dtc -I dtb -O dts "$overlay"
else
  echo "Cannot decompile $overlay (missing dtc or unreadable file)"
fi
REMOTE

collect argus-modes.txt <<'REMOTE'
set +e
if command -v nvargus_nvraw >/dev/null; then
  timeout 15 nvargus_nvraw --lps
else
  echo 'nvargus_nvraw is not installed'
fi
REMOTE

collect capture-720p60-120frames.txt <<'REMOTE'
set +e
echo '===== 1280x720 RG10, 60 fps, 120-frame MMAP smoke test ====='
echo 'The active overlay sets use_sensor_mode_id=true, so select mode 4 explicitly.'
echo 'Command: v4l2-ctl -d /dev/video0 --set-ctrl=sensor_mode=4'
v4l2-ctl -d /dev/video0 --set-ctrl=sensor_mode=4
echo 'Command: v4l2-ctl -d /dev/video0 --set-fmt-video=width=1280,height=720,pixelformat=RG10 --set-parm=60'
v4l2-ctl -d /dev/video0 \
  --set-fmt-video=width=1280,height=720,pixelformat=RG10 \
  --set-parm=60
echo '===== negotiated format before STREAMON ====='
v4l2-ctl -d /dev/video0 --get-fmt-video --get-parm
echo 'Command: v4l2-ctl -d /dev/video0 --stream-mmap=4 --stream-poll --stream-count=120 --verbose'
timeout 20 v4l2-ctl -d /dev/video0 \
  --stream-mmap=4 \
  --stream-poll \
  --stream-count=120 \
  --verbose
status=$?
echo "capture_exit_status=$status"
exit "$status"
REMOTE

(
  cd "$OUTPUT_DIR"
  shasum -a 256 -- *.txt >SHA256SUMS
)

echo "Baseline saved to $OUTPUT_DIR"
