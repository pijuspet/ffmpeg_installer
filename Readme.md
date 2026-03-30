# FFmpeg Builder

Build and install FFmpeg from source with custom patches.

## Prerequisites

git, gcc, make, patch, pkg-config, and python3 must be installed.

## Usage

**Build:**

```bash
make build
```

This runs the full pipeline automatically: checks dependencies, clones the FFmpeg `release/8.0` branch, applies the two patch files (`ffmpeg_version.diff` and `hevc-patch.diff`), runs `./configure`, and compiles using all available CPU cores.

**Install:**

```bash
sudo make install
```

Installs the built binaries to `/usr/local` (the default prefix).

**Verify:**

```bash
make verify
```