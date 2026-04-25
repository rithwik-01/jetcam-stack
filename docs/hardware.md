# 硬件与环境基线

## 1. 基线范围

采集日期：2026-08-07（America/Denver）
目标主机：已匿名化
原始记录：[baseline/2026-08-07-jetson-orin-nano](../baseline/2026-08-07-jetson-orin-nano/README.md)

本文结论来自目标机的真实命令输出、运行中的 Device Tree、当前启动配置和 NVIDIA 官方文档。发布副本仅对用户名、网络地址和设备唯一标识进行了匿名化，并由 `SHA256SUMS` 校验。

## 2. 硬件识别

| 项目 | 已确认值 | 证据 |
|---|---|---|
| 开发板 | Jetson Orin Nano Developer Kit Super | `hostnamectl`、`/proc/device-tree/model` |
| SOM/载板组合 | P3767-0005 + P3768 | 当前 FDT 文件名和 overlay compatible 列表 |
| 相机 | Sony IMX219 / Raspberry Pi Camera Module v2 类模块 | 运行中 DT 的 `compatible=sony,imx219` 与 `sensor_model=imx219` |
| 物理接口 | CAM1，22-pin J21 | 当前选择 IMX219-C、I²C mux 第二路、`serial_c`/`port-index=2`，且项目照片记录为 CAM1 |
| CSI 配置 | CSI-C，2 lane，24 MHz MCLK | overlay 中 `tegra_sinterface="serial_c"`、`num_lanes="2"`、`mclk_khz="24000"` |
| I²C 地址 | mux `i2c@1` 下的 `0x10`；Linux 标识 `9-0010` | DT 节点和 Media Controller entity |

P3768 载板上 J20 是 Camera #0，J21 是 Camera #1；接口的详细引脚定义见 NVIDIA 的 [Jetson Orin Nano Developer Kit Carrier Board Specification](https://developer.nvidia.com/downloads/assets/embedded/secure/jetson/orin_nano/docs/jetson_orin_nano_devkit_carrier_board_specification_sp.pdf)。

### CAM、Device Tree 与 Argus 映射

```text
CAM1 / J21
  └─ /boot/tegra234-p3767-camera-p3768-imx219-C.dtbo
      └─ cam_i2cmux/i2c@1/rbpcv2_imx219_c@10
          ├─ sony,imx219; I²C 0x10
          ├─ tegra_sinterface = serial_c
          ├─ CSI/VI port-index = 2; bus-width = 2
          └─ tegra-camera-platform/modules/module1
              ├─ position = rear
              └─ Argus Source Index 0 → sensor-id=0
                  └─ /dev/video0 (imx219 9-0010)
```

这里最容易混淆的是：物理 `CAM1`、DT `module1` 并不意味着应用必须使用 `sensor-id=1`。当前系统只启用了一个模块，`nvargus_nvraw --lps` 将它枚举为 Source Index 0，所以 `nvarguscamerasrc sensor-id=0` 才是当前正确值。

## 3. 固化的软件版本

| 组件 | 版本 |
|---|---|
| Ubuntu | 22.04.5 LTS (Jammy) |
| JetPack | 6.2.3+b81 |
| Jetson Linux / L4T | R36.5.0，GCID 43688277 |
| Kernel | 5.15.185-tegra，aarch64，PREEMPT |
| Kernel variant | OOT |
| GStreamer | 1.20.3 |
| v4l-utils (`v4l2-ctl`) | 1.22.1 |
| media-ctl | 1.22.1 |
| nvarguscamerasrc | 1.0.0 |
| nvvidconv | 1.2.3 |
| nvargus_nvraw | 1.17.0 |

完整包版本见 [system.txt](../baseline/2026-08-07-jetson-orin-nano/system.txt)。

## 4. 设备、驱动与内核配置

### 运行时绑定

- RAW 节点：`/dev/video0`
- Media Controller：`/dev/media0`
- Sensor subdev：`/dev/v4l-subdev1`
- NVCSI subdev：`/dev/v4l-subdev0`
- V4L2 capture driver：`tegra-video`
- VI platform driver：`tegra-camrtc-capture-vi`
- Sensor module：`nv_imx219`
- 已加载模块：`/lib/modules/5.15.185-tegra/updates/drivers/media/i2c/nv_imx219.ko`

当前使用 NVIDIA tegracam OOT 驱动，不是主线内核中的通用 `drivers/media/i2c/imx219.c`。目标机只安装了编译后的 `.ko` 和 OOT headers，没有安装 `nv_imx219.c` 源文件。

### 驱动源码位置

下载与目标 L4T R36.5.0 完全匹配的 Driver Package (BSP) Sources，解压 `public_sources.tbz2` 和其中的 `kernel_oot_modules_src.tbz2` 后，驱动位于：

```text
Linux_for_Tegra/source/nvidia-oot/drivers/media/i2c/nv_imx219.c
```

关联 Device Tree 源码位于：

```text
Linux_for_Tegra/source/hardware/nvidia/t23x/nv-public/overlay/
  tegra234-camera-rbpcv2-imx219.dtsi
  tegra234-p3767-camera-p3768-imx219-C.dts
```

源码获取与 OOT 构建流程应遵循 R36.5 的 [Kernel Customization](https://docs.nvidia.com/jetson/archives/r36.5/DeveloperGuide/SD/Kernel/KernelCustomization.html)。不要拿 R36.4 或主线内核源码直接替换当前 R36.5 模块。

### 已确认的相关配置

```text
CONFIG_I2C=y
CONFIG_I2C_TEGRA=y
CONFIG_MEDIA_SUPPORT=m
CONFIG_MEDIA_CONTROLLER=y
CONFIG_VIDEO_DEV=m
CONFIG_VIDEO_V4L2=m
```

`CONFIG_VIDEO_IMX219` 没有出现在运行内核配置中是预期现象：本机实际使用单独打包的 NVIDIA OOT `nv_imx219.ko`。详见 [kernel-driver.txt](../baseline/2026-08-07-jetson-orin-nano/kernel-driver.txt)。

## 5. 相机能力基线

`/dev/video0` 只枚举一种像素格式：`RG10`（10-bit Bayer RGRG/GBGB）。

| Sensor mode | 分辨率 | V4L2 标称 FPS | Argus 报告 FPS |
|---:|---:|---:|---:|
| 0 | 3280×2464 | 21 | 20（显示取整） |
| 1 | 3280×1848 | 28 | 28 |
| 2 | 1920×1080 | 30 | 29（显示取整） |
| 3 | 1640×1232 | 30 | 29（显示取整） |
| 4 | 1280×720 | 60 | 59（显示取整） |

当前 overlay 设置了 `use_sensor_mode_id=true`，因此直接 V4L2 采集必须显式设置 `sensor_mode=4` 才能可靠选择 720p60。只给 `--set-fmt-video=1280x720` 可能仍停留在默认 mode 0。

## 6. 720p60 基线测试

最终测试步骤：

```bash
v4l2-ctl -d /dev/video0 --set-ctrl=sensor_mode=4
v4l2-ctl -d /dev/video0 \
  --set-fmt-video=width=1280,height=720,pixelformat=RG10 \
  --set-parm=60
v4l2-ctl -d /dev/video0 \
  --stream-mmap=4 --stream-poll --stream-count=120 --verbose
```

结果：

- 协商格式：1280×720，RG10，60/1 FPS。
- 收到 120/120 帧，sequence 0–119 连续，丢帧 0。
- 帧间隔约 16.634–16.635 ms，`v4l2-ctl` 报告约 60.12 FPS。
- `capture_exit_status=0`，`ssh_exit_status=0`。
- 每帧 `bytesused=1,843,200`，符合 1280×720×2-byte unpacked 存储。

完整逐帧记录见 [capture-720p60-120frames.txt](../baseline/2026-08-07-jetson-orin-nano/capture-720p60-120frames.txt)。

## 7. 阶段 1 发现的架构约束

### Orin Nano 没有 NVENC

目标机上 `gst-inspect-1.0 nvv4l2h264enc` 返回 `No such element or plugin`。这不是简单的漏装插件：NVIDIA 明确说明 Jetson Orin Nano 不包含 NVENC，应使用 libx264/FFmpeg 软件编码。参考 [Software Encode in Orin Nano](https://docs.nvidia.com/jetson/archives/r36.5/DeveloperGuide/SD/Multimedia/SoftwareEncodeInOrinNano.html)。

因此原计划“Orin Nano + 硬件 H.264 编码 + NVMM 零拷贝编码”在当前硬件上不可实现。后续必须二选一：

1. 保留 Orin Nano，采用 `nvarguscamerasrc → nvvidconv → system-memory I420/NV12 → x264enc`，并实测 720p60 的 CPU、温度和延迟；或
2. 保留硬件编码目标，更换为带 NVENC 的 Jetson 型号，再重新固化基线。

这一选择不影响阶段 2 的 RAW 工具和阶段 3 的 IMX219 驱动/DT 工作，但会改变阶段 4 的性能目标与实现路径。

## 8. 可复现采集

```bash
./scripts/collect-baseline.sh baseline/YYYY-MM-DD-board-name
```

脚本采集系统版本、V4L2 能力和格式、Media Controller 拓扑、驱动/内核配置、运行 DT、Argus 模式，并运行 120 帧冒烟测试。密码不会写入仓库。
