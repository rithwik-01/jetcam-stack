# Jetson Orin Nano + IMX219 baseline — 2026-08-07

## 结果摘要

- JetPack 6.2.3 / L4T R36.5.0 / Linux 5.15.185-tegra / GStreamer 1.20.3。
- IMX219 绑定 `nv_imx219.ko`，RAW 节点为 `/dev/video0`，格式为 RG10。
- 当前 overlay：`tegra234-p3767-camera-p3768-imx219-C.dtbo`。
- 当前映射：CAM1/J21 → `serial_c`/CSI-C → NVCSI → VI → `/dev/video0`；唯一 Argus source 为 `sensor-id=0`。
- 720p60 MMAP 测试：120/120 帧，sequence 0–119，约 60.12 FPS，无丢帧/超时，退出码 0。
- `nvarguscamerasrc` 和 `nvvidconv` 存在；`nvv4l2h264enc` 不存在，符合 Orin Nano 无 NVENC 的硬件限制。

## 文件说明

| 文件 | 内容 |
|---|---|
| `system.txt` | OS、JetPack/L4T、kernel、GStreamer 和插件版本 |
| `v4l2-all.txt` | `v4l2-ctl --all` |
| `v4l2-formats.txt` | 格式/分辨率/FPS 枚举与 controls |
| `media-topology.txt` | `media-ctl --print-topology` |
| `kernel-driver.txt` | 设备绑定、模块、源/头文件包和 kernel config |
| `device-tree-camera.txt` | 启动项、运行 DT 节点与 IMX219-C overlay 反编译 |
| `argus-modes.txt` | Argus source 与 sensor modes |
| `capture-720p60-120frames.txt` | 逐帧 MMAP 冒烟测试记录 |
| `SHA256SUMS` | 所有 `.txt` 文件的 SHA-256 |

校验：

```bash
cd baseline/2026-08-07-jetson-orin-nano
shasum -a 256 -c SHA256SUMS
```

重新采集：

```bash
./scripts/collect-baseline.sh baseline/2026-08-07-jetson-orin-nano
```

