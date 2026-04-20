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

## What the patch changes

The diff in [custom_ffmpeg.diff](custom_ffmpeg.diff) adds a *motion-vectors-only* fast path to the H.264, HEVC, and MPEG decoders. When the caller sets `AVCodecContext::motion_vectors_only = 1`, the decoder parses just enough of the bitstream to recover motion vectors and skips pixel reconstruction (residual IDCT, intra prediction, deblocking, SAO, film grain, etc.).

### New `AVMotionVectorCompact` side data

Because the mv-only consumer never looks at reconstructed pixels, most fields of the stock `AVMotionVector` struct are dead weight. The patch introduces a slimmer variant in [libavutil/motion_vector.h](../ffmpeg/FFmpeg-8.0-custom/FFmpeg/libavutil/motion_vector.h) exported through a new side-data type `AV_FRAME_DATA_MOTION_VECTORS_COMPACT`.

| Field | `AVMotionVector` (stock, ~40 B) | `AVMotionVectorCompact` (patched, 12 B) |
|---|---|---|
| `source` (int32) | yes | yes |
| `src_x`, `src_y` (int16) | yes | yes |
| `dst_x`, `dst_y` (int16) | yes | yes |
| `w`, `h` (uint8 block size) | yes | **dropped** |
| `flags` (uint64) | yes | **dropped** |
| `motion_x`, `motion_y` (int32) | yes | **dropped** |
| `motion_scale` (uint16) | yes | **dropped** |

The compact variant keeps only the two endpoints — reference-frame source position and current-frame block-center destination — which is all the extractors need. Per-MV memory drops from ~40 B to 12 B, and the encoder path writes directly into the side-data buffer, removing a temporary allocation and memcpy.

### Selecting the output

- `motion_vectors_only = 0` (default): decoder behaves as stock FFmpeg and emits `AV_FRAME_DATA_MOTION_VECTORS` with `AVMotionVector` entries.
- `motion_vectors_only = 1`: decoder skips reconstruction and emits `AV_FRAME_DATA_MOTION_VECTORS_COMPACT` with `AVMotionVectorCompact` entries.