# 阶段 3：IMX219 驱动与 Device Tree 增强

## 结论

阶段 3 已于 2026-08-07 在 Jetson Orin Nano（L4T R36.5、Linux
5.15.185-tegra）完成。NVIDIA OOT IMX219 驱动新增标准
`V4L2_CID_TEST_PATTERN` 菜单控件，支持 Disabled、Solid Color、Color Bars、
Grey Color 和 PN9。IMX219-C overlay 保持阶段 1/2 已验证的 CAM1/J21、CSI-C、
2 lane 拓扑，并加入可在运行时设备树核验的阶段标记。

## 驱动审计

目标机原驱动为
`/lib/modules/5.15.185-tegra/updates/drivers/media/i2c/nv_imx219.ko`。原实现已通过
NVIDIA 私有 64-bit 控件提供曝光、增益、帧率和 sensor mode，但没有标准测试图案
或水平/垂直翻转控件。对照上游 Linux IMX219 驱动后，本阶段选择测试图案作为首个
缺失标准控件：它能在没有 ISP 的 RAW 路径中直接验证寄存器写入和图像变化。

IMX219 的测试图案选择寄存器为 16-bit 大端寄存器 `0x0600/0x0601`，合法值为
0–4。实现先将 MSB 写 0，再将菜单值写入 LSB；控件还加入 tegracam 的流开始同步
列表，因此 mode 切换或重新取流后会重新应用缓存值。

## 修改内容

- `driver/patches/0001-imx219-add-v4l2-test-pattern.patch`
  - 在 `nv_imx219.c` 注册并实现 `V4L2_CID_TEST_PATTERN`。
  - 在 tegracam 控件框架中增加标准菜单、分发、实现检查和流开始同步。
  - 将新回调追加在 `tegracam_ctrl_ops` 末尾，保留已有字段偏移。
- `driver/patches/0002-imx219-c-stage3-overlay.patch`
  - 维护官方 IMX219-C overlay，不改变已验证的 CSI-C、2-lane 拓扑。
  - 增加 `jetcam,stage3-revision = "test-pattern-v1"` 运行时标记。

两个补丁均已对 NVIDIA Jetson Linux R36.5 官方 OOT/DT 源码执行
`patch --dry-run -p1`，可以干净应用。

## 构建注意事项

`tegracam_ctrl_ops` 出现在导出函数原型中。即使只在结构体末尾追加回调，
`CONFIG_MODVERSIONS` 仍会改变相关导出符号 CRC。因此不能只替换 `nv_imx219.ko`；
本次同时按新 `tegra-camera` 符号表重链接以下消费者：

- `tegra-camera.ko`
- `nv_imx219.ko`
- `nvhost-nvcsi.ko`
- `nvhost-nvcsi-t194.ko`

单独以 `M=.../camera` 构建还会绕过 `drivers/media/Makefile` 的父级编译标志。
R36.5 目标内核的三项配置均为模块，构建 camera 目录时必须继承：

```text
-DCONFIG_V4L2_ASYNC
-DCONFIG_V4L2_FWNODE
-DCONFIG_VIDEOBUF2_DMA_CONTIG
```

缺少 `V4L2_ASYNC` 会使 CSI channel 初始化失败；缺少 `V4L2_FWNODE` 会使 VI
graph 初始化失败。最终模块已确认包含全部三项，且 sensor/NVCSI 所需 CRC 与
`tegra-camera` 导出值逐项一致。

## 部署结果

目标机安装路径与 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `tegra-camera.ko` | `f8add81c3da57e19a86860c1bffacf5d982edfabccbf4ae2828eeb33156ee5db` |
| `nv_imx219.ko` | `7f1c245ab6d95481111f79139db33c202766f188536f4d15f4856a1444a12ab8` |
| `nvhost-nvcsi.ko` | `8faf85e88ea88f0b9884431e24852b31ca5a9703f4a09346a88834bfd9be5520` |
| `nvhost-nvcsi-t194.ko` | `bf983cb78bf514629ffdcbc222af04bb157b74c62ad0c028a046d7a9802481c8` |
| IMX219-C DTBO | `42e7fd00324b8698ac4e05cb2e639325f2ad07fae7e9f4c090bee02e4ed5551d` |

原模块、DTBO 和 `extlinux.conf` 已备份到目标机：

```text
/var/backups/jetcam-stage3-20260807/
```

回滚时从该目录恢复四个 `.ko`、DTBO 和 `extlinux.conf`，执行 `depmod -a` 后重启。

## 验收结果

最终 boot ID 已在公开副本中匿名化；重启后
`imx219 9-0010` 自动绑定并生成 `/dev/video0`。运行时设备树标记为
`test-pattern-v1`。

`v4l2-ctl --list-ctrls-menus` 可见：

```text
test_pattern 0x009f0903 (menu): min=0 max=4 default=0 value=0 (Disabled)
  0: Disabled
  1: Solid Color
  2: Color Bars
  3: Grey Color
  4: PN9
```

Disabled、Color Bars A 和 Color Bars B 各采集 120 帧 720p60，三次均为
60.116 FPS，dropped、discontinuities 和 buffer error 均为 0。RAW 帧均为
1,843,200 字节。有效图像内区分析：

- Disabled 与 Color Bars 平均绝对差为 6325.248，几乎全部像素变化。
- 两次 Color Bars 平均绝对差为 54.723；82.1981% 像素差不超过一个
  10-bit 左对齐量化步长（64），99.915% 不超过 256。
- 测试后控件恢复为 Disabled。

`nv_imx219` 卸载后 `/dev/video0` 消失；重新加载后节点恢复，控件默认 Disabled，
随后 120 帧 720p60 再次通过且零丢帧。

## 与 Argus 管线切换

Argus/`jetcamd` 会把 NVIDIA 私有 `bypass_mode` 和 `override_enable` 设为 1。
如果 Argus 会话异常结束后直接运行 RAW V4L2 测试，这两个值可能没有恢复，表现为
首帧超时。`scripts/test-stage3.sh` 会在 `STREAMON` 前检查这两个控件并安全退出，
避免继续进入错误的 videobuf2 清理路径。

确认没有 Argus 或 `jetcamd` 进程占用相机后，可重载 sensor 模块恢复默认状态：

```bash
sudo modprobe -r nv_imx219
sudo modprobe nv_imx219
```

等待 `/dev/video0` 返回后重新运行阶段 3 脚本。该恢复路径已在目标机实测通过。

初始阶段 3 独立验收及 sensor 重载后的复测均没有符号不一致、Unknown symbol、
Probe 失败、CSI 错误、超时或 Oops。后续在刚结束的 Argus 会话之后复测时，曾复现
一次首帧超时及 videobuf2 清理 Warning；确认原因为上述两个私有控件遗留为 1，
重载 `nv_imx219` 后恢复，脚本现已增加前置保护。

自编 OOT 模块没有 NVIDIA 内核的私钥签名，因此启动时有一次预期的
`module verification failed ... tainting kernel`；目标内核 `sig_enforce=N`，不影响
加载或采集。目标机没有可用的原内核私钥，无法消除该签名提示。

完整证据位于 `baseline/2026-08-07-stage3-jetson-orin-nano/`。
