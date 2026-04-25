# JetCam Stack 阶段 4 验收 — 2026-08-07

## 环境与实现选择

- 目标机：Jetson Orin Nano，L4T R36.5，Linux 5.15.185-tegra，aarch64。
- 相机：IMX219，Argus `sensor-id=0`。
- GStreamer：1.20.3。
- 编码器：`x264enc` 软件 H.264；Orin Nano 不存在 `nvv4l2h264enc`。
- 主输出：MPEG-TS/UDP；扩展输出：RTSP/TCP。

实际数据路径为：

```text
nvarguscamerasrc -> NVMM/NV12 -> nvvidconv -> system-memory I420
  -> x264enc -> h264parse (SPS/PPS every second) -> mpegtsmux -> UDP -> VLC
```

RTSP 模式由 `jetcamd` 显式管理上述 Argus/x264 管线，再通过
`127.0.0.1:5400` 的 RTP 中继交给 RTSP factory。这保证程序退出时捕获管线
先转到 `NULL`，最终日志没有 Argus outstanding-thread 错误。

## 验收结果

| 验收项 | 实测结果 |
|---|---|
| aarch64 Release 构建 | 成功 |
| CTest | 4/4 通过 |
| 720p60 UDP/MPEG-TS | 稳态 60.0 FPS，VLC TS demux + H.264 `avcodec` 解码 |
| 1080p30 UDP/MPEG-TS | 稳态 30.0 FPS，VLC TS demux + H.264 `avcodec` 解码 |
| JPEG 快照 | 1280×720 和 1920×1080 均为有效 JFIF JPEG |
| `SIGUSR1` 快照 | 运行中请求、保存、正常退出，进程状态 0 |
| UDP 接收端中断 | VLC 接入前和退出后 `jetcamd` 均保持运行 |
| RTSP/TCP | SETUP、PLAY、10 秒数据传输、EOS 和服务清理通过 |
| 统一冒烟脚本 | `STAGE4_RESULT=PASS` |
| 30 分钟稳定性 | `STAGE4_STABILITY_RESULT=PASS duration_seconds=1800` |
| 长测内核相机错误 | 按 IMX219/NVCSI/VI/timeout/Oops/error 筛选为 0 行 |

## 30 分钟 720p60 稳定性

- 目标机时间：2026-08-07 21:11:22 至 21:41:35（UTC+08:00）。
- VLC 设置的播放时间：1,800 秒。
- `jetcamd` 总帧数：108,469（包含 VLC 前后的有界等待时间）。
- 启动后稳态 FPS：59.8–60.2，最终 60.1995 FPS。
- 六核逐核采样平均占用：39.87%；采样范围 0–53%。
- `tj` 平均 55.069°C，最高 55.562°C。
- RAM 平均 2,249.6 MiB，最高 2,449 MiB / 7,607 MiB，Swap 未使用。
- `VDD_IN` 平均约 8.142 W，最高 8.757 W。
- 正常收到 `SIGINT`，Argus 打印 `Done Success`，`state=STOPPED`。

长测 VLC 以较低详细度记录，进程保持 1,800 秒并正常退出；解复用器和解码器
的明确模块名称来自最终冒烟测试的 VLC `--verbose=2` 日志。改进后的稳定性脚本
也使用 `--verbose=2`，并显式要求 TS demux、H.264 started 和 `avcodec` 三项证据。

## 文件

| 路径 | 内容 |
|---|---|
| `evidence/build-and-tests.txt` | 最终构建、4/4 CTest、UDP 和 RTSP 管线 |
| `evidence/environment-and-plugins.txt` | 内核、GStreamer、软件包和插件路径 |
| `evidence/smoke-summary.txt` | 统一 PASS、FPS、VLC 解码模块和 RTSP 会话 |
| `evidence/snapshot-validation.txt` | 两种分辨率和 `SIGUSR1` JPEG 验证 |
| `evidence/stability-summary.txt` | 30 分钟最终帧数、温度、内存与 PASS |
| `evidence/kernel-camera-log.txt` | 长测起始后的相机/错误关键字筛选，0 字节表示无命中 |
| `smoke/` | 最终 720p60、1080p30、VLC、JPEG 和 RTSP 原始记录 |
| `stability/` | 30 分钟 `jetcamd`、VLC、tegrastats 和起止时间 |
| `sigusr1-snapshot.log` | 运行中快照原始服务日志 |
| `smoke-pre-fix/` / `evidence-pre-fix/` | 保留的初始追踪；发现 RTSP factory 直接管理 Argus 的退出竞态，不作为最终验收依据 |

`SHA256SUMS` 覆盖本目录内除其自身之外的所有文件。
