# jetcam-raw-test 使用手册

## 1. 工具用途

`jetcam-raw-test` 是直接操作 Linux V4L2 capture API 的 RAW 相机测试工具，主要用于
验证 IMX219 驱动、MIPI CSI-2、NVCSI 和 VI 采集链路是否稳定。它可以完成：

- 查询 `/dev/video0` 的驱动信息和 streaming 能力；
- 枚举像素格式、分辨率和帧率；
- 选择 Jetson 的 `sensor_mode`；
- 通过 MMAP buffer 连续采集 RAW 帧；
- 统计实际 FPS、sequence 缺口、sequence 异常和错误 buffer；
- 检测规定时间内收不到帧的 timeout；
- 保存一帧原始 Bayer 数据；
- 在正常结束或收到信号后释放相机设备。

该工具用于驱动和 CSI 链路验证，不经过 Argus/ISP，也不执行去马赛克、自动白平衡、
颜色处理或视频编码。保存的 `RG10` 文件不是 JPEG/PNG，VLC 也不能直接播放。

## 2. 目标机位置

项目已经部署在 Jetson：

```text
/home/jetson/jetcam-stack
```

正式安装的工具位于：

```text
/home/jetson/jetcam-stack/install/bin/jetcam-raw-test
```

登录目标机后进入项目目录：

```bash
ssh jetson@JETSON_IP
cd /home/jetson/jetcam-stack
```

后续示例使用以下简写：

```bash
RAW_TEST=./install/bin/jetcam-raw-test
```

如果尚未执行安装，也可以使用构建目录中的版本：

```text
./build/app/jetcam-raw-test/jetcam-raw-test
```

## 3. 运行前检查

确认设备节点存在：

```bash
ls -l /dev/video0
```

确认当前用户属于可以访问相机的用户组：

```bash
id
```

确认设备没有被其他相机程序占用：

```bash
fuser /dev/video0
```

`fuser` 没有输出表示当前通常没有进程占用设备。若 Argus、GStreamer、
`v4l2-ctl` 或另一个 `jetcam-raw-test` 正在取流，应先正常停止对应进程。

## 4. 第一次运行

### 4.1 查看帮助

```bash
$RAW_TEST --help
```

### 4.2 枚举相机能力和模式

```bash
$RAW_TEST --device /dev/video0 --list
```

当前 IMX219 应枚举出一个 `RG10` 格式和以下模式：

| sensor mode | 分辨率 | 最大帧率 | 典型用途 |
|---:|---:|---:|---|
| 0 | 3280×2464 | 21 FPS | 全分辨率 RAW |
| 1 | 3280×1848 | 28 FPS | 宽画幅高分辨率 |
| 2 | 1920×1080 | 30 FPS | 1080p RAW |
| 3 | 1640×1232 | 30 FPS | 4:3 RAW 模式 |
| 4 | 1280×720 | 60 FPS | 阶段 2 默认稳定性测试 |

此映射来自当前 IMX219-C overlay。更换相机或 Device Tree overlay 后，应重新执行
`--list` 并核对目标机的 mode 定义，不要直接沿用表中的编号。

### 4.3 执行默认测试

```bash
$RAW_TEST
```

默认行为等价于：

```bash
$RAW_TEST \
  --device /dev/video0 \
  --sensor-mode 4 \
  --width 1280 --height 720 \
  --pixel-format RG10 \
  --fps 60 \
  --buffers 4 \
  --frames 120 \
  --timeout-ms 2500
```

它大约运行 2 秒。最终出现 `RESULT status=PASS`，且 `dropped=0`、
`discontinuities=0`、`buffer_errors=0`，表示本次采集通过。

## 5. 参数说明

| 参数 | 默认值 | 说明 |
|---|---:|---|
| `--device PATH` | `/dev/video0` | V4L2 capture 设备节点。 |
| `--width PIXELS` | `1280` | 请求的图像宽度。最终值以 `NEGOTIATED` 为准。 |
| `--height PIXELS` | `720` | 请求的图像高度。最终值以 `NEGOTIATED` 为准。 |
| `--pixel-format FOURCC` | `RG10` | 四字符 V4L2 像素格式。 |
| `--fps RATE` | `60` | 请求帧率。最终值以 `FRAME_RATE negotiated_fps` 为准。 |
| `--sensor-mode INDEX` | `4` | 设置 Jetson 私有 `sensor_mode` 控制。非 Tegra 设备使用 `none`。 |
| `--buffers COUNT` | `4` | MMAP buffer 数量，允许范围 2–32。 |
| `--frames COUNT` | `120` | 达到指定帧数后停止；`0` 表示不限制帧数。 |
| `--duration SECONDS` | `0` | 达到指定运行时间后停止；`0` 表示不限制时间。 |
| `--timeout-ms MS` | `2500` | 等待下一帧的最长时间，超时返回退出码 4。 |
| `--save-frame PATH` | 未设置 | 将指定的一帧保存到文件。已有文件会被覆盖。 |
| `--save-at NUMBER` | `1` | 保存第几帧，从 1 开始计数。 |
| `--report-interval SEC` | `1` | `PROGRESS` 统计输出周期。 |
| `--allow-drops` | 关闭 | 仍统计帧异常，但不因异常返回退出码 5。仅用于诊断。 |
| `--list` | 关闭 | 只枚举能力和模式，不执行取流。 |
| `--help` | 关闭 | 显示内置帮助。 |

### 帧数与时间限制的关系

`--frames` 和 `--duration` 同时生效，任意一个条件先满足都会结束采集。因此执行
10 分钟测试时必须显式加入 `--frames 0`：

```bash
$RAW_TEST --frames 0 --duration 600
```

若只写 `--duration 600`，程序仍会在默认的 120 帧处先结束。

将两者都设置为 0 会持续运行，直到收到 `Ctrl+C`、`SIGINT` 或 `SIGTERM`：

```bash
$RAW_TEST --frames 0 --duration 0
```

## 6. 常用操作

### 6.1 720p60 短测试

```bash
$RAW_TEST \
  --sensor-mode 4 \
  --width 1280 --height 720 \
  --pixel-format RG10 --fps 60 \
  --frames 120
```

### 6.2 1080p30 测试

```bash
$RAW_TEST \
  --sensor-mode 2 \
  --width 1920 --height 1080 \
  --pixel-format RG10 --fps 30 \
  --frames 300
```

### 6.3 采集 10 分钟

```bash
$RAW_TEST \
  --sensor-mode 4 \
  --width 1280 --height 720 \
  --pixel-format RG10 --fps 60 \
  --frames 0 --duration 600 \
  --timeout-ms 2500 \
  --report-interval 10
```

### 6.4 保存一帧 RAW

```bash
mkdir -p out/manual

$RAW_TEST \
  --sensor-mode 4 \
  --width 1280 --height 720 \
  --pixel-format RG10 --fps 60 \
  --frames 60 \
  --save-frame out/manual/frame-720p.rg10.raw \
  --save-at 30
```

720p RG10 在当前 Tegra 驱动上的协商结果为：

```text
bytesperline=2560
sizeimage=1843200
```

因此保存文件应为 `1280 × 720 × 2 = 1,843,200` 字节。这里每个像素占用两个
存储字节，但有效数据为 10 bit。处理 RAW 时应同时参考 `fourcc`、`bytesperline`
和 Bayer 排列，不能把文件直接当成 8-bit 灰度图。

检查文件大小：

```bash
wc -c out/manual/frame-720p.rg10.raw
```

### 6.5 保存测试日志

```bash
mkdir -p out/manual
set -o pipefail

$RAW_TEST --frames 120 2>&1 | tee out/manual/capture.log
```

`set -o pipefail` 很重要，否则 shell 可能只返回 `tee` 的退出状态，而忽略采集工具
的失败退出码。

### 6.6 执行完整自动验收

从项目根目录运行：

```bash
./scripts/test-stage2.sh
```

该脚本会构建项目、运行 CTest、枚举模式、执行 600 秒稳定性测试、保存 RAW，并
检查进程退出后设备能否立即重新打开。开发阶段可缩短时间：

```bash
DURATION_SECONDS=10 ./scripts/test-stage2.sh
```

缩短后的测试只能作为快速检查，不能替代正式 600 秒验收。

## 7. 输出如何阅读

程序每行以记录类型开头，后面是适合脚本解析的 `key=value`：

| 记录 | 含义 |
|---|---|
| `CAPABILITY` | 驱动、设备名称、总线和 streaming 能力。 |
| `FORMAT` | 设备支持的 FOURCC 像素格式。 |
| `SIZE` | 离散分辨率。 |
| `INTERVAL` | 分辨率对应的帧间隔和 FPS。 |
| `CONTROL` | 实际设置的 Tegra sensor mode。 |
| `NEGOTIATED` | 驱动最终接受的宽、高、格式、stride 和单帧大小。 |
| `FRAME_RATE` | 请求帧率和驱动协商帧率。 |
| `MMAP` | 实际映射的 capture buffer 数量。 |
| `STREAM` | `STREAMON` 或 `STREAMOFF` 状态。 |
| `PROGRESS` | 运行中的帧数、FPS 和错误统计。 |
| `SNAPSHOT` | RAW 保存路径、字节数和对应 sequence。 |
| `RESULT` | 最终结果，也是自动化测试最应关注的一行。 |

成功结果示例：

```text
RESULT status=PASS frames=120 first_sequence=0 last_sequence=119 dropped=0 discontinuities=0 buffer_errors=0 capture_seconds=1.980 wall_seconds=1.994 actual_fps=60.116 requested_fps=60.000 negotiated_fps=60.000 width=1280 height=720 fourcc=RG10 snapshot_saved=false
```

`RESULT` 字段解释：

| 字段 | 含义 |
|---|---|
| `status` | `PASS`、`FAIL`、`TIMEOUT` 或 `INTERRUPTED`。 |
| `frames` | 成功 DQBUF 的总次数。 |
| `first_sequence` / `last_sequence` | 驱动返回的首尾帧序号。 |
| `dropped` | sequence 正向缺口代表的估算丢帧数。 |
| `discontinuities` | 重复或倒退的 sequence 次数。 |
| `buffer_errors` | 带 `V4L2_BUF_FLAG_ERROR` 的 buffer 数量。 |
| `capture_seconds` | 首尾 V4L2 timestamp 的时间跨度。 |
| `wall_seconds` | 从开始取流到停止的单调时钟时间。 |
| `actual_fps` | `(frames - 1) / capture_seconds`。 |
| `requested_fps` | 命令行请求的 FPS。 |
| `negotiated_fps` | 驱动接受的 FPS。 |
| `width` / `height` / `fourcc` | 驱动最终接受的图像格式。 |
| `snapshot_saved` | 本次是否成功保存 RAW。 |

只有 `status=PASS` 不够，稳定性测试还应核对实际运行时长和帧数是否符合测试目标。

## 8. 退出码

| 退出码 | 含义 | 自动化处理建议 |
|---:|---|---|
| 0 | 测试通过，或 `--list`/`--help` 成功 | 继续后续测试。 |
| 2 | 参数错误 | 修正命令行，不要盲目重试。 |
| 3 | 设备、配置、MMAP、取流或文件 I/O 错误 | 检查错误信息和设备状态。 |
| 4 | 等待帧超时 | 检查 sensor、CSI/VI 和内核日志。 |
| 5 | sequence 或 buffer error 验证失败 | 保留日志并检查 CSI/VI 稳定性。 |
| 130 | 收到 `SIGINT`，通常来自 `Ctrl+C` | 属于操作者中止，不是测试通过。 |
| 143 | 收到 `SIGTERM` | 属于外部终止，不是测试通过。 |

## 9. 常见问题

### `Device or resource busy`

另一个进程正在占用相机：

```bash
fuser -v /dev/video0
```

正常停止对应程序后重试。不要直接对未知进程使用 `kill -9`。

### `Permission denied`

检查设备权限和用户组：

```bash
ls -l /dev/video0
id
```

如果系统通过 `video` 用户组授权，应由管理员将用户加入该组，并重新登录后生效。

### `VIDIOC_S_EXT_CTRLS(sensor_mode)` 失败

当前设备可能不是 Jetson tegracam 驱动，或 mode 编号不正确。先执行 `--list`；
非 Tegra V4L2 设备使用：

```bash
$RAW_TEST --sensor-mode none
```

### `VIDIOC_S_FMT` 或协商结果不符合请求

先运行 `--list`，选择设备实际支持的 FOURCC、宽高和帧率。最终配置始终以
`NEGOTIATED` 和 `FRAME_RATE negotiated_fps` 为准。

### `RESULT status=TIMEOUT`

程序在 `--timeout-ms` 内没有收到新帧。保留工具输出，并检查：

```bash
dmesg | tail -n 100
media-ctl -d /dev/media0 -p
```

重点查找 I²C、NVCSI、VI、CHANSEL 和 timeout 错误。CSI 相机不应通过带电拔插来
模拟故障。

### `RESULT status=FAIL`

查看 `dropped`、`discontinuities` 和 `buffer_errors` 哪一项非零。默认返回退出码 5。
`--allow-drops` 只应用于收集更多诊断信息，不应在正式验收中使用。

### RAW 文件无法直接打开

这是预期行为。`RG10` 是 Bayer 原始传感器数据，没有经过 ISP。需要可视图像时应
使用 Argus/ISP 路径；RAW 文件用于离线 Bayer 分析、驱动验证和逐像素检查。

## 10. 安全退出与设备释放

运行过程中按 `Ctrl+C` 后，程序会依次执行 `STREAMOFF`、`munmap` 和 `close`。
退出后可以立即重新运行工具。不要在工具运行时带电拔插 CSI 排线，也不要同时启动
另一个直接 V4L2 或 Argus 采集进程。

阶段 2 的实现设计和验收数据见
[阶段 2 技术文档](stage2-raw-test.md)与
[2026-08-07 验收记录](../baseline/2026-08-07-stage2-jetson-orin-nano/README.md)。
