# JetCam Stack 阶段 3 验收 — 2026-08-07

## 环境

- 目标机：Jetson Orin Nano，L4T R36.5，Linux 5.15.185-tegra，aarch64。
- 相机：IMX219，CAM1/J21，CSI-C，2 lanes，`/dev/video0`。
- 最终 boot ID：`<redacted>`。
- Overlay 标记：`jetcam,stage3-revision = "test-pattern-v1"`。

## 结论

| 验收项 | 实测结果 |
|---|---|
| 官方 R36.5 补丁 | 两个补丁 dry-run 均可干净应用 |
| 新增标准控件 | `V4L2_CID_TEST_PATTERN`，0–4 五项菜单 |
| 重启自动绑定 | `/dev/video0` 存在，`imx219 9-0010` bound |
| 720p60 Disabled | 120 帧，60.116 FPS，零丢帧/错误 |
| 720p60 Color Bars A | 120 帧，60.116 FPS，零丢帧/错误 |
| 720p60 Color Bars B | 120 帧，60.116 FPS，零丢帧/错误 |
| 控件图像效果 | Disabled/Color Bars 有效区 MAE 6325.248 |
| 图案稳定性 | 两次 Color Bars 有效区 MAE 54.723 |
| 卸载/重载 | 节点按预期消失/恢复，重载后 120 帧再次通过 |
| CSI/Probe/Oops | 无符号、Probe、CSI、timeout 或 Oops 错误 |

自编模块因没有 NVIDIA 内核私钥签名，启动时产生一次预期的模块验证 taint 提示；
`sig_enforce=N`，不影响功能。RAW 验证帧只保存在临时目录，不纳入仓库。

## 文件

| 文件 | 内容 |
|---|---|
| `build-deploy.txt` | 最终模块/DTBO 哈希与关键符号 CRC |
| `controls-and-device-tree.txt` | V4L2 新控件、设备身份和 DT 标记 |
| `capture-test.txt` | 三次 720p60 采集与 RAW 有效区对比 |
| `unload-reload.txt` | sensor 模块卸载、重载和重载后采集 |
| `kernel-log.txt` | 最终启动与测试后的相机相关日志摘要 |
| `SHA256SUMS` | 上述证据文件的 SHA-256 |
