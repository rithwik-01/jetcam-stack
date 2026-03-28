# Driver patch licensing and distribution

These patches target the official NVIDIA Jetson Linux R36.5 source release. They are intentionally distributed as source patches, not as complete NVIDIA source files or prebuilt kernel modules.

| Patch | Purpose | License handling |
|---|---|---|
| `0001-imx219-add-v4l2-test-pattern.patch` | Adds standard IMX219 V4L2 test-pattern control support | GPL-2.0-only; retain all upstream notices |
| `0002-imx219-c-stage3-overlay.patch` | Marks the validated IMX219-C overlay revision | Same upstream/file-specific terms continue to apply; retain all notices |

Apply only to the matching R36.5 source package. If you redistribute a compiled GPL-covered module, provide its complete corresponding source—including the exact upstream source, these modifications, build scripts/configuration needed to reproduce it, and the GPL notice. Do not redistribute NVIDIA proprietary binaries or modified headers unless the applicable NVIDIA terms expressly permit it.
PREMADI
cat /tmp/jetcam-stack-rebranded/driver/patches/README.md
