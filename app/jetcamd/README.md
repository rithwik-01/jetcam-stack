# jetcamd

`jetcamd` 是阶段 4 的 GStreamer C++ 视频服务。它在 Jetson Orin Nano 上使用
Argus/ISP 和 NVMM 获取 IMX219 图像，经 `nvvidconv` 转为系统内存 I420，再由
`x264enc` 软件编码 H.264。Orin Nano 没有 NVENC，因此本程序不会尝试使用
`nvv4l2h264enc`。

可直接按命令操作的完整说明见 [jetcamd 使用手册](../../docs/jetcamd-usage.md)。

## 依赖与构建

```bash
sudo apt-get install \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
  libgstrtspserver-1.0-dev vlc

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

缺少 GStreamer 开发文件时，顶层工程仍可构建 RAW 工具和配置测试，但不会生成
`jetcamd` 可执行文件。缺少 RTSP Server 开发包时仍能构建 UDP 功能；选择 RTSP
会返回明确错误。

## UDP/MPEG-TS 与 VLC

启动默认 720p60 服务：

```bash
./build/app/jetcamd/jetcamd --config config/jetcamd.conf
```

默认 `host=127.0.0.1` 用于目标机本地验收：在目标机 VLC 中打开
`udp://@:5000`，或运行 `cvlc udp://@:5000`。若 VLC 在另一台电脑上，启动时将
UDP 目标改为那台电脑的 IP：

```bash
./build/app/jetcamd/jetcamd --config config/jetcamd.conf \
  --set host=192.168.1.50
```

1080p30 可通过命令行覆盖配置：

```bash
./build/app/jetcamd/jetcamd --config config/jetcamd.conf \
  --set width=1920 --set height=1080 --set framerate=30 \
  --set bitrate_kbps=8000 --set gop=30
```

参数必须采用 `--set KEY=VALUE`，并放在 `--config` 之后。可配置项包括传感器、
分辨率、帧率、码率、GOP、UDP 地址/端口、输出模式、RTSP 端口/挂载点和快照路径。
程序只接受 IMX219 已验证的 720p60 与 1080p30 两种服务模式。

## JPEG 快照

`snapshot_on_start=true` 会在启动后保存一帧。运行中可发送 `SIGUSR1` 按需保存：

```bash
kill -USR1 "$(pgrep -n jetcamd)"
```

快照分支默认由 GStreamer `valve` 关闭；请求时只打开到第一帧 JPEG 写完，不会在
后台持续做 JPEG 编码。

## RTSP 扩展

```bash
./build/app/jetcamd/jetcamd --config config/jetcamd.conf --set output=rtsp
```

默认地址为 `rtsp://JETSON_IP:8554/jetcam`。可用 GStreamer 验证：

```bash
gst-launch-1.0 -e \
  rtspsrc location=rtsp://127.0.0.1:8554/jetcam latency=100 protocols=tcp \
  ! rtph264depay ! h264parse ! fakesink sync=false
```

目标机 Ubuntu 22.04 自带的 VLC 3.0.16 禁用了 live555，不能作为通用 RTSP
客户端；这不影响 VLC 播放主交付的 UDP/MPEG-TS，也不影响其他带 RTSP 支持的 VLC。

## 测试

```bash
JETCAMD_BIN=./build/app/jetcamd/jetcamd \
JETCAMD_CONFIG=./config/jetcamd.conf \
./scripts/test-stage4.sh

JETCAM_STABILITY_SECONDS=1800 \
JETCAMD_BIN=./build/app/jetcamd/jetcamd \
JETCAMD_CONFIG=./config/jetcamd.conf \
./scripts/test-stage4-stability.sh
```

第一个脚本测试 720p60、1080p30、JPEG、UDP 接收端连接/断开、VLC 解码和 RTSP；
第二个脚本执行 30 分钟 VLC 稳定性测试并采集 `tegrastats`。
