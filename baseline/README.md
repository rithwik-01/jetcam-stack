# Baseline records

Each dated directory contains unedited command output collected from the Jetson target.
Run the collector from the repository root:

```bash
./scripts/collect-baseline.sh
```

Override the target or output directory when needed:

```bash
JETCAM_REMOTE=user@host ./scripts/collect-baseline.sh baseline/YYYY-MM-DD-board-name
```

The script opens one SSH control connection, prompts for a password if key authentication is not configured, performs read-only discovery, and runs a bounded 120-frame V4L2 capture smoke test. It does not save frame payloads on either host.

Stage-specific acceptance records are also stored under dated directories. Stage 4 includes
the final build/CTest output, VLC decode logs, RTSP session logs, JPEG snapshots, a 30-minute
`jetcamd`/VLC run, and `tegrastats` samples:

- `2026-08-07-stage4-jetson-orin-nano/`
