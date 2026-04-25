# 阶段 4：软件 H.264 编码与 VLC 视频服务

## 结论与设计变更

阶段 1 已确认 Jetson Orin Nano 不含 NVENC，目标机也不存在
`nvv4l2h264enc`。因此阶段 4 保留 Argus/ISP 和 NVMM 采集路径，在编码边界由
`nvvidconv` 落到系统内存 I420，并采用 `x264enc` 软件 H.264 编码。这是当前硬件
能够实际部署的实现，不把软件编码描述为硬件编码或零拷贝编码。

主输出为 MPEG-TS/UDP，VLC 直接打开 `udp://@:5000`。扩展输出为 RTP/H.264 over
RTSP。程序支持 720p60、1080p30、码率、GOP、目标地址和端口配置，并提供按需
JPEG 快照。

## 数据路径

```text
IMX219
  -> nvarguscamerasrc / Argus / ISP
  -> NV12 in NVMM
  -> nvvidconv
  -> I420 in system memory
  -> x264enc (software, zerolatency, ultrafast, no B frames)
  -> h264parse
     -> mpegtsmux -> udpsink -> VLC
     or
     -> rtph264pay -> loopback RTP/UDP relay -> GStreamer RTSP Server

I420 tee
  -> leaky queue -> valve (normally closed) -> jpegenc -> appsink -> JPEG file
```

`udpsink` 设置 `sync=false async=false`。UDP 没有连接状态，接收端暂时不存在或中途
退出不会向上游传播致命错误。快照分支的 `valve` 默认丢弃数据，只有启动快照或
收到 `SIGUSR1` 时才打开，第一张 JPEG 写入后立即关闭。

RTSP 模式将 Argus/x264 捕获管线作为 `jetcamd` 直接管理的独立对象，再通过
`127.0.0.1:5400` 的 RTP 中继交给 RTSP factory。这样程序退出时能先将捕获管线
明确转到 `NULL`，避免 RTSP 媒体 factory 直接销毁 Argus 对象时的线程清理竞态。

## 实现文件

| 文件 | 内容 |
|---|---|
| `app/jetcamd/src/main.cpp` | GStreamer 生命周期、Bus 错误/EOS、信号退出、FPS 和快照 |
| `app/jetcamd/src/config.cpp` | `key=value` 配置、命令行覆盖和范围校验 |
| `app/jetcamd/src/pipeline.cpp` | UDP/MPEG-TS 与 RTSP 管线构造 |
| `app/jetcamd/src/rtsp_server.cpp` | 可选 GStreamer RTSP Server |
| `config/jetcamd.conf` | Nano 的 720p60 默认配置 |
| `scripts/test-stage4.sh` | 两种模式、VLC、快照、断连和 RTSP 冒烟矩阵 |
| `scripts/test-stage4-stability.sh` | 30 分钟 VLC 与 tegrastats 稳定性测试 |

## 构建与依赖

目标机原有 Argus、`nvvidconv`、`x264enc` 和 JPEG 插件，但缺少 `mpegtsmux` 与
RTSP Server 开发文件。本阶段安装：

```bash
sudo apt-get install gstreamer1.0-plugins-bad libgstrtspserver-1.0-dev
```

随后在 aarch64 目标机执行 CMake Release 构建。`jetcamd`、RAW 工具和 4/4 CTest
均通过。

## 验收方法

1. 720p60 UDP/MPEG-TS，在 VLC 接入前空跑、VLC 解码、VLC 退出后继续运行。
2. 1080p30 重复上述测试。
3. 每种 UDP 模式保存一张 JPEG，并用 `file` 验证格式。
4. 启动 RTSP 服务，并以 TCP `rtspsrc` 拉流到 H.264 parser/fakesink。
5. 检查服务日志无 `state=ERROR` 或 `pipeline_error`。

目标机 VLC 详细日志必须出现 TS demux、H.264 packetizer 和 `avcodec` video decoder，
而不是只判断进程启动。目标机 VLC 构建禁用了 live555，因此 RTSP 扩展使用
GStreamer 客户端验收；UDP/MPEG-TS 仍由 VLC 实际解码验收。

## 当前实测结果

短测中 720p60 稳态为 60.0 FPS，1080p30 稳态为 30.0 FPS；两种模式均保存有效
JPEG，VLC 均启用了 TS demux 与 H.264 `avcodec` 解码器。UDP 在接收端接入前和
退出后服务保持运行。RTSP/TCP 完成 SETUP、PLAY、10 秒数据传输和 EOS 清理。

30 分钟稳定性记录和完整原始日志见
`baseline/2026-08-07-stage4-jetson-orin-nano/`。

## 限制

- 软件编码会占用 Arm CPU，功耗、温度和延迟不能套用 NVENC 指标。
- 阶段 4 只负责视频服务与基础 Bus 错误退出；自动重建、退避和长期状态接口属于
  阶段 5。
- 若必须使用 NVMM 到硬件 H.264 的零拷贝链路，需要更换为带 NVENC 的 Jetson。
