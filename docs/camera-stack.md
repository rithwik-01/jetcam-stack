# IMX219 相机栈与数据路径

## 1. 总览

系统包含两条用途不同的用户态路径：直接 V4L2 RAW 路径用于驱动验证；Argus/ISP 路径用于可视图像、快照和网络视频服务。两条路径共享传感器、I²C 控制、MIPI CSI-2、NVCSI 和 VI，但用户态输出格式不同。

```text
控制面
应用 / Argus / v4l2-ctl
  → V4L2 controls / tegracam
  → nv_imx219.ko
  → CAM I²C mux, bus 9, address 0x10
  → IMX219 registers (mode/exposure/gain/frame rate/stream)

数据面
IMX219 Bayer RG10
  → 2-lane MIPI CSI-2 on CAM1/J21
  → CSI-C / serial_c / port-index 2
  → NVCSI
  → VI
  ├─ Direct V4L2 → /dev/video0 (RG10) → jetcam-raw-test
  └─ Camera Core / Argus → ISP → NV12/NVMM → nvarguscamerasrc → jetcamd
```

NVIDIA 对 Bayer 相机的两类应用入口也做了相同区分：`v4l2src`/ioctl 用于直接 V4L2，`nvarguscamerasrc` 通过 Argus 使用 ISP。参见 [Camera Software Development Solution](https://docs.nvidia.com/jetson/archives/r36.5/DeveloperGuide/SD/CameraDevelopment/CameraSoftwareDevelopmentSolution.html)。

## 2. 硬件和 Device Tree 路径

当前启动项加载：

```text
FDT      /boot/dtb/kernel_tegra234-p3768-0000+p3767-0005-nv-super.dtb
OVERLAYS /boot/tegra234-p3767-camera-p3768-imx219-C.dtbo
```

overlay 建立的关键关系：

| 层 | 当前节点/值 | 作用 |
|---|---|---|
| Camera module | `tegra-camera-platform/modules/module1` | 向 Camera Core/Argus 描述模块 |
| Sensor DT | `cam_i2cmux/i2c@1/rbpcv2_imx219_c@10` | 绑定 `sony,imx219` |
| Sensor bus | Linux `imx219 9-0010` | I²C adapter 9，地址 0x10 |
| Sensor output | `serial_c`, 2 lanes | MIPI CSI-2 物理输入 |
| NVCSI endpoint | `port-index=2`, `bus-width=2` | 连接 CSI-C channel |
| VI endpoint | `port-index=2`, `bus-width=2` | 连接到 Tegra VI capture |
| Video node | `/dev/video0` | RAW capture API |

模式节点还规定 MCLK、lane 数、像素相位、line length、pixel clock、曝光/增益/帧率范围等。驱动在 probe 时读取这些属性，将它们注册为 tegracam sensor modes 和 V4L2 controls。

## 3. 驱动注册路径

```text
Device Tree compatible = "sony,imx219"
  → I²C core matches nv_imx219 driver's OF table
  → nv_imx219 probe()
  → tegracam_device_register()
  → V4L2 subdev + media entity: "imx219 9-0010"
  → async endpoint binding to NVCSI
  → VI capture node registration: "/dev/video0"
```

运行时 Media Controller 已验证为：

```text
imx219 9-0010 (/dev/v4l-subdev1), pad 0 Source
  → 13e00000.host1x:nvcsi@15a00000- (/dev/v4l-subdev0), pad 0 Sink
  → NVCSI pad 1 Source
  → vi-output, imx219 9-0010 (/dev/video0), pad 0 Sink
```

所有 link 都是 `[ENABLED]`。完整实体和 pad 信息见 [media-topology.txt](../baseline/2026-08-07-jetson-orin-nano/media-topology.txt)。

## 4. 上电、配置与取流顺序

### 打开与配置

1. 应用打开 `/dev/video0` 或 Argus 打开 `sensor-id=0`。
2. 驱动根据选中的 `sensor_mode` 读取 DT mode properties。
3. tegracam/IMX219 驱动准备电源、MCLK 与 reset/power-down GPIO。
4. 通过 I²C 写入模式寄存器、曝光、增益和帧率。
5. V4L2 路径协商 RG10 格式并申请 MMAP buffers；Argus 路径创建 Camera Core/ISP buffers。

### STREAMON

1. VI 和 NVCSI 准备 capture channel。
2. 驱动写 IMX219 streaming 寄存器开始输出。
3. IMX219 发送 RG10 MIPI CSI-2 packets。
4. NVCSI 解包并交给 VI；VI 写入捕获缓冲区。
5. V4L2 应用收到带 sequence/timestamp 的 RAW buffer，或 Argus 将帧送入 ISP。

### STREAMOFF/关闭

1. 停止 sensor streaming。
2. 停止 VI/NVCSI capture，回收 queued buffers。
3. 释放管线资源，关闭 MCLK/电源并复位 sensor。
4. 应用关闭 fd；此时 `/dev/video0` 应能立即被再次打开。

具体函数名和错误路径将在阶段 3 对匹配的 R36.5 `nv_imx219.c` 做源码级审计后补充。

## 5. Direct V4L2 RAW 路径

```text
/dev/video0
  format: RG10 only
  memory: MMAP
  metadata: sequence, monotonic timestamp, bytesused
  consumers: v4l2-ctl, future jetcam-raw-test
```

此路径绕过 ISP，适合验证：

- sensor mode 和寄存器控制是否生效；
- CSI/VI 是否稳定；
- 帧序号连续性、FPS、超时和 buffer 生命周期；
- 保存 Bayer RAW 做离线分析。

RG10 是 Bayer 原始数据，不是 VLC 常规支持的 YUV/RGB 视频帧。因此让 VLC 直接打开 `/dev/video0` 不是正确的生产路径。

## 6. Argus/ISP 服务路径

```text
nvarguscamerasrc sensor-id=0
  → Camera Core / libargus
  → ISP (demosaic, AE/AWB, color processing)
  → video/x-raw(memory:NVMM), format=NV12
  → jetcamd
```

当前 `nvargus_nvraw --lps` 只报告 Source Index 0，共 5 个 sensor modes。这直接确认当前 Argus 参数应为 `sensor-id=0`。

### 当前 Orin Nano 的编码分支

```text
NV12/NVMM
  → nvvidconv
  → CPU-accessible I420/NV12
  → x264enc / libx264 (software)
  → h264parse
  → RTP/UDP or RTSP
  → VLC
```

Orin Nano 没有 NVENC，所以不能使用原计划中的 `nvv4l2h264enc` 硬件编码分支。GPU/ISP/NVMM 仍能用于采集、ISP 与部分转换，但软件编码前需要进入 CPU 可访问内存。NVIDIA 给出的参考 GStreamer 管线见 [Software Encode in Orin Nano](https://docs.nvidia.com/jetson/archives/r36.5/DeveloperGuide/SD/Multimedia/SoftwareEncodeInOrinNano.html)。

## 7. 故障定位边界

| 现象 | 优先检查层 | 基线命令 |
|---|---|---|
| 找不到 `/dev/video0` | DT overlay、I²C probe、driver binding | `lsmod`, `modinfo`, kernel log |
| Media link 缺失 | V4L2 async endpoint、port-index/remote-endpoint | `media-ctl -p -d /dev/media0` |
| STREAMON 超时 | mode、MCLK/reset、CSI lanes/polarity、NVCSI/VI | `v4l2-ctl --verbose`, kernel log |
| 分辨率仍是 3280×2464 | 未显式选择 sensor mode | `v4l2-ctl --set-ctrl=sensor_mode=4` |
| RAW 正常、Argus 失败 | Camera Core module metadata、Argus daemon、ISP | `nvargus_nvraw --lps`, journald |
| VLC 无法直接打开设备 | `/dev/video0` 是 RG10 Bayer | 改走 Argus/ISP + encoder |
| 找不到 `nvv4l2h264enc` | Orin Nano 无 NVENC | 改用 x264enc 或更换硬件 |

## 8. 阶段 2 的接口边界

`jetcam-raw-test` 应只依赖 Linux V4L2 ABI，按以下次序实现：

```text
open
→ VIDIOC_QUERYCAP
→ VIDIOC_ENUM_FMT / FRAMESIZES / FRAMEINTERVALS
→ select sensor_mode when required
→ VIDIOC_S_FMT / VIDIOC_S_PARM
→ VIDIOC_REQBUFS(MMAP)
→ QUERYBUF + mmap + QBUF
→ STREAMON
→ poll + DQBUF(sequence/timestamp) + QBUF
→ STREAMOFF
→ munmap + close
```

Argus、ISP、编码和网络输出不进入阶段 2 工具，避免把驱动稳定性测试与生产视频管线混在一起。
