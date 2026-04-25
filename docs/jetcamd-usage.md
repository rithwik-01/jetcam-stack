# jetcamd 使用手册

本手册适用于 Jetson Orin Nano、JetPack 6.2.3 / L4T R36.5、IMX219 和
`sensor-id=0`。命令默认在仓库根目录执行。

## 当前目标机部署

项目已部署到：

```text
/home/jetson/jetcam-stack
```

部署后的可执行文件：

```text
/home/jetson/jetcam-stack/install/bin/jetcamd
/home/jetson/jetcam-stack/install/bin/jetcam-raw-test
```

在 Jetson 上直接启动：

```bash
cd /home/jetson/jetcam-stack
./install/bin/jetcamd --config ./config/jetcamd.conf
```

本文后续示例中的 `./build/app/jetcamd/jetcamd` 也可替换为
`./install/bin/jetcamd`。

## 1. 安装依赖

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake g++ pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
  libgstrtspserver-1.0-dev vlc
```

检查关键插件：

```bash
for element in nvarguscamerasrc nvvidconv x264enc mpegtsmux jpegenc rtph264pay; do
  gst-inspect-1.0 "$element" >/dev/null || echo "missing: $element"
done
```

Orin Nano 不含 NVENC，`gst-inspect-1.0 nvv4l2h264enc` 失败是预期现象。

## 2. 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
cmake --install build --prefix /home/jetson/jetcam-stack/install
```

可执行文件为 `build/app/jetcamd/jetcamd`。先只打印管线而不打开相机：

```bash
./build/app/jetcamd/jetcamd \
  --config config/jetcamd.conf \
  --print-pipeline
```

## 3. 快速启动 720p60 UDP 服务

目标机终端 1：

```bash
./build/app/jetcamd/jetcamd --config config/jetcamd.conf
```

目标机终端 2：

```bash
cvlc udp://@:5000
```

也可在 VLC 图形界面中选择“媒体 → 打开网络串流”，输入：

```text
udp://@:5000
```

默认配置发送到 `127.0.0.1:5000`，用于目标机本地验收。

## 4. 在另一台电脑上使用 VLC

先查询 VLC 电脑的局域网 IP，例如 `192.168.1.50`。在 Jetson 上运行：

```bash
./build/app/jetcamd/jetcamd \
  --config config/jetcamd.conf \
  --set host=192.168.1.50 \
  --set port=5000
```

在 VLC 电脑打开：

```text
udp://@:5000
```

发送地址必须是 VLC 电脑的 IP，不是 Jetson 的 IP。若无画面，先确认两台机器
在同一网段，并允许接收端防火墙的 UDP 5000 入站流量。

## 5. 切换 1080p30

```bash
./build/app/jetcamd/jetcamd \
  --config config/jetcamd.conf \
  --set width=1920 \
  --set height=1080 \
  --set framerate=30 \
  --set bitrate_kbps=8000 \
  --set gop=30 \
  --set host=VLC_PC_IP
```

已验证模式仅为 `1280x720@60` 和 `1920x1080@30`。不完整的宽、高、帧率
组合会在打开相机前被拒绝。

## 6. 配置文件

`config/jetcamd.conf` 使用一行一个 `key=value`：

| 配置项 | 默认值 | 说明 |
|---|---:|---|
| `sensor_id` | `0` | Argus 传感器索引 |
| `width` / `height` | `1280` / `720` | 输出尺寸 |
| `framerate` | `60` | 帧率 |
| `bitrate_kbps` | `6000` | x264 目标码率，kbit/s |
| `gop` | `60` | 最大关键帧间隔 |
| `output` | `udp` | `udp` 或 `rtsp` |
| `host` / `port` | `127.0.0.1` / `5000` | UDP 接收端 |
| `rtsp_port` | `8554` | RTSP 监听端口 |
| `rtsp_mount` | `/jetcam` | RTSP 挂载路径 |
| `snapshot_file` | `/tmp/jetcam-snapshot.jpg` | JPEG 快照路径 |
| `snapshot_on_start` | `false` | 启动时是否保存一张 |

命令行 `--set KEY=VALUE` 覆盖配置文件。`--set` 应放在 `--config` 之后：

```bash
./build/app/jetcamd/jetcamd \
  --config config/jetcamd.conf \
  --set bitrate_kbps=4000 --set gop=120
```

## 7. JPEG 快照

启动时保存：

```bash
./build/app/jetcamd/jetcamd \
  --config config/jetcamd.conf \
  --set snapshot_file=/tmp/camera.jpg \
  --set snapshot_on_start=true
```

已运行时保存：

```bash
pid="$(pgrep -x -n jetcamd)"
kill -USR1 "$pid"
file /tmp/jetcam-snapshot.jpg
```

新快照使用普通截断写覆盖同名文件（不是临时文件加 `rename` 的原子替换）。
如果需要保留每次快照，请在下一次信号前移动或复制旧文件。

## 8. RTSP 输出

在 Jetson 上启动：

```bash
./build/app/jetcamd/jetcamd \
  --config config/jetcamd.conf \
  --set output=rtsp
```

客户端地址：

```text
rtsp://JETSON_IP:8554/jetcam
```

可用 GStreamer 命令验证：

```bash
gst-launch-1.0 -e \
  rtspsrc location=rtsp://JETSON_IP:8554/jetcam latency=100 protocols=tcp \
  ! rtph264depay ! h264parse ! decodebin ! autovideosink
```

目标 Jetson 自带的 VLC 3.0.16 构建禁用了 live555，所以目标机 VLC 不能用于
验收这个 RTSP URL。普通桌面版 VLC 如包含 RTSP 模块，可直接打开上述地址。

## 9. 退出和日志

前台运行时按 `Ctrl+C`。后台运行时发送 `SIGINT` 或 `SIGTERM`。正常退出会打印：

```text
state=STOPPED frames=...
```

运行中每 5 秒输出一次：

```text
metric=frames frames=... fps=...
```

管线 Error 会记录 `state=ERROR`、错误元素、消息和 GStreamer debug 文本，并以非零状态退出。

## 10. 自动验收

冒烟矩阵：

```bash
JETCAMD_BIN=./build/app/jetcamd/jetcamd \
JETCAMD_CONFIG=./config/jetcamd.conf \
./scripts/test-stage4.sh stage4-test-output
```

30 分钟稳定性：

```bash
JETCAMD_BIN=./build/app/jetcamd/jetcamd \
JETCAMD_CONFIG=./config/jetcamd.conf \
JETCAM_STABILITY_SECONDS=1800 \
./scripts/test-stage4-stability.sh stage4-stability-output
```

## 11. 常见问题

### `No such element or plugin 'mpegtsmux'`

```bash
sudo apt-get install gstreamer1.0-plugins-bad
```

### `error=rtsp_not_built`

```bash
sudo apt-get install libgstrtspserver-1.0-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### VLC 没有画面

- 确认 Jetson 的 `host` 配置是 VLC 电脑的 IP。
- 确认 VLC 打开的是 `udp://@:5000`。
- 确认接收端防火墙允许 UDP 5000。
- 等待最多一个 GOP；默认 720p60 的 GOP 为 60，约 1 秒。

### Argus 报告相机忙或无法启动

```bash
pgrep -a jetcamd
pgrep -a gst-launch-1.0
sudo systemctl status nvargus-daemon
```

同一个 IMX219 不能被多个 Argus 管线同时占用。先正常停止旧的测试或服务，不要
带电拔插 CSI 排线。
