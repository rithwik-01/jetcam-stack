# 阶段 2：V4L2 RAW 采集工具

本文说明工具实现、统计口径和验收结果。需要运行命令、逐项参数解释、输出字段和
常见问题时，请阅读 [jetcam-raw-test 使用手册](jetcam-raw-test-usage.md)。

## 实现范围

`jetcam-raw-test` 按阶段 1 确认的数据路径直接使用 Linux V4L2 ABI：

```text
open(O_RDWR | O_NONBLOCK)
  → VIDIOC_QUERYCAP
  → VIDIOC_ENUM_FMT / FRAMESIZES / FRAMEINTERVALS
  → VIDIOC_S_EXT_CTRLS(sensor_mode，Tegra 可选)
  → VIDIOC_S_FMT / VIDIOC_S_PARM
  → VIDIOC_REQBUFS / QUERYBUF / mmap / QBUF
  → VIDIOC_STREAMON
  → poll / DQBUF / 序号与 FPS 统计 / QBUF
  → VIDIOC_STREAMOFF / munmap / close
```

实现位于 `app/jetcam-raw-test`，没有 Argus、ISP 或编码依赖。默认参数来自阶段 1
基线：`/dev/video0`、sensor mode 4、1280×720、RG10、60 FPS、4 个 MMAP buffer。

## 统计口径

- `frames`：成功 DQBUF 的总数，包括驱动标记为 error 的 buffer。
- `actual_fps`：以首尾 V4L2 buffer timestamp 之间的帧间隔计算，即
  `(frames - 1) / (last_timestamp - first_timestamp)`。
- `dropped`：相邻 32 位 sequence 的正向缺口，正确处理 `UINT32_MAX → 0` 回绕。
- `discontinuities`：重复或倒退的 sequence。
- `buffer_errors`：带 `V4L2_BUF_FLAG_ERROR` 的 buffer 数量。
- `wall_seconds`：从 `STREAMON` 后到测试停止的单调时钟时间。

默认严格验证 sequence 和 buffer error。驱动协商后的宽度、高度、格式和帧率会
单独输出，避免把请求值误当成实际配置。

## 目标机验收

从仓库根目录构建后运行：

```bash
./scripts/test-stage2.sh
```

脚本默认执行以下步骤：

1. CTest 单元和 CLI 测试；
2. 能力和所有 RAW mode 枚举；
3. 720p60 连续采集 600 秒，要求无 timeout、sequence gap 和 buffer error；
4. 保存一帧 RAW，并检查文件非空；
5. 第一次进程完全退出后立即再次采集，验证设备已释放。

快速开发迭代可临时缩短稳定性测试：

```bash
DURATION_SECONDS=10 ./scripts/test-stage2.sh
```

测试日志默认写入 `out/stage2-<UTC timestamp>/`。只有完整 600 秒运行才能作为
“10 分钟验收通过”的依据。

## 2026-08-07 实测结果

目标 Jetson 已完成正式验收：CMake/G++ 构建成功，3/3 CTest 通过；720p60
连续取流 600.025 秒，共 36,076 帧，sequence 0–36,075 连续，实际 60.115 FPS，
`dropped=0`、`discontinuities=0`、`buffer_errors=0`，未发生 timeout。单帧 RAW
保存大小为 1,843,200 字节，短测和 10 分钟长测退出后均能立即重新打开设备。

可复核的原始文本日志位于
[`baseline/2026-08-07-stage2-jetson-orin-nano`](../baseline/2026-08-07-stage2-jetson-orin-nano/README.md)。

## 故障定位

| 最终状态/错误 | 含义 | 优先检查 |
|---|---|---|
| `TIMEOUT` / 4 | 指定时间没有可 DQBUF 的帧 | sensor mode、CSI/VI 内核日志、设备占用 |
| `FAIL` / 5 | sequence 不连续或 buffer error | `dmesg` 中 NVCSI/VI 错误、负载、排线 |
| `VIDIOC_S_EXT_CTRLS` / 3 | Tegra sensor mode 设置失败 | mode 范围、overlay、是否应使用 `--sensor-mode none` |
| `VIDIOC_S_FMT` / 3 | 格式配置失败 | 先运行 `--list`，核对 FOURCC 和分辨率 |
| `Device or resource busy` / 3 | 设备仍被占用 | Argus/v4l2 进程、上一次测试是否已退出 |

CSI 相机不用于带电热插拔测试。发生超时时应先正常退出工具，再检查内核日志。
