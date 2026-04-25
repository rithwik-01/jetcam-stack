# JetCam Stack 阶段 2 验收 — 2026-08-07

## 验收环境

- 目标机：Jetson Orin Nano，Linux 5.15.185-tegra，aarch64。
- 相机：IMX219，`/dev/video0`，Tegra `sensor_mode=4`。
- 编译器：GCC/G++ 11.4.0。
- 构建系统：CMake 3.22.1。
- 测试模式：1280×720、RG10、60 FPS、4 个 MMAP buffer。

## 结论

阶段 2 验收通过：

| 验收项 | 实测结果 |
|---|---|
| CMake 构建 | 成功，无编译错误 |
| CTest | 3/3 通过 |
| 模式枚举 | RG10 的 5 个离散模式与阶段 1 基线一致 |
| 120 帧短测 | 120/120，sequence 0–119，60.116 FPS，零错误 |
| 单帧 RAW | 第 30 帧保存成功，1,843,200 字节 |
| 10 分钟稳定性 | 600.025 秒，36,076 帧，60.115 FPS |
| sequence 连续性 | 0–36,075，dropped=0，discontinuities=0 |
| buffer/timeout | buffer_errors=0，未发生 timeout |
| 退出后重开 | 长测退出后立即采集 2 帧成功 |

10 分钟测试最终记录：

```text
RESULT status=PASS frames=36076 first_sequence=0 last_sequence=36075 dropped=0 discontinuities=0 buffer_errors=0 capture_seconds=600.095 wall_seconds=600.025 actual_fps=60.115 requested_fps=60.000 negotiated_fps=60.000 width=1280 height=720 fourcc=RG10 snapshot_saved=false
```

## 文件

| 文件 | 内容 |
|---|---|
| `cmake-build-ctest.log` | CMake 配置、G++ 构建和 3 项 CTest |
| `build-and-tests.log` | 直接编译阶段的 stats/CLI 检查摘要 |
| `modes.log` | 能力、格式、分辨率和帧率枚举 |
| `short-capture.log` | 120 帧测试和单帧 RAW 保存记录 |
| `immediate-reopen.log` | 短测退出后的立即重开记录 |
| `stability-720p60-600s.log` | 720p60、600 秒完整稳定性日志 |
| `post-stability-reopen.log` | 10 分钟长测退出后的立即重开记录 |
| `SHA256SUMS` | 所有 `.log` 文件的 SHA-256 |

RAW 帧只在目标机临时目录用于文件大小验证，没有纳入 Git 仓库。
