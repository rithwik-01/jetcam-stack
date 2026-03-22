# jetcam-raw-test

`jetcam-raw-test` 是阶段 2 的纯 V4L2 RAW 验证工具。它直接对 `/dev/video0`
执行能力/模式枚举、格式和帧率设置、MMAP 缓冲区取流、帧序号验证、超时检测和
单帧保存；不依赖 Argus、GStreamer 或 OpenCV。

面向实际操作的参数解释、完整示例、输出字段和故障排查请阅读
[jetcam-raw-test 使用手册](../../docs/jetcam-raw-test-usage.md)。本文只保留构建和
快速命令参考。

## 构建

目标系统需要 CMake 3.16+、C++17 编译器和 Linux V4L2 UAPI 头文件：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

可执行文件为 `build/app/jetcam-raw-test/jetcam-raw-test`。

## 常用命令

枚举设备能力、格式、分辨率和帧率：

```bash
./build/app/jetcam-raw-test/jetcam-raw-test --list
```

按阶段 1 已确认的 IMX219 mode 4 执行 720p60、120 帧测试：

```bash
./build/app/jetcam-raw-test/jetcam-raw-test \
  --sensor-mode 4 --width 1280 --height 720 --pixel-format RG10 \
  --fps 60 --frames 120
```

采集 10 分钟，并保存第 30 帧。`--frames 0` 表示不以帧数终止：

```bash
./build/app/jetcam-raw-test/jetcam-raw-test \
  --sensor-mode 4 --width 1280 --height 720 --pixel-format RG10 \
  --fps 60 --frames 0 --duration 600 \
  --save-frame frame-720p.rg10.raw --save-at 30
```

若不是 Tegra 相机，使用 `--sensor-mode none` 跳过 NVIDIA 私有控制。收到
`SIGINT` 或 `SIGTERM` 后，程序会执行 `STREAMOFF`、`munmap` 和 `close`，因此设备
可以立即重新打开。

## 可解析输出与退出码

输出由带类型前缀的 `key=value` 记录组成：`CAPABILITY`、`FORMAT`、`SIZE`、
`INTERVAL`、`NEGOTIATED`、`PROGRESS` 和最终的 `RESULT`。成功示例：

```text
RESULT status=PASS frames=120 first_sequence=0 last_sequence=119 dropped=0 discontinuities=0 buffer_errors=0 capture_seconds=1.980 wall_seconds=2.013 actual_fps=60.101 requested_fps=60.000 negotiated_fps=60.000 width=1280 height=720 fourcc=RG10 snapshot_saved=false
```

默认情况下，帧序号缺口、重复/倒退或 `V4L2_BUF_FLAG_ERROR` 会令最终状态为
`FAIL` 并返回 5。诊断场景可用 `--allow-drops` 只记录而不失败。

| 退出码 | 含义 |
|---:|---|
| 0 | 测试通过或 `--list`/`--help` 成功 |
| 2 | 命令行参数错误 |
| 3 | 打开、配置、MMAP、取流或文件 I/O 错误 |
| 4 | 等待新帧超时 |
| 5 | 帧序号或 buffer error 验证失败 |
| 128+signal | 被信号中断，例如 `SIGINT` 为 130 |

完整的阶段 2 验收步骤见 [阶段 2 测试文档](../../docs/stage2-raw-test.md)。
