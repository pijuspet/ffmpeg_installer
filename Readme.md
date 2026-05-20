# FFmpeg Builder

Build and install FFmpeg from source with custom patches, and extract motion vectors from video files.

---

## Linux

### Prerequisites

git, gcc, make, patch, pkg-config, and nasm must be installed.

### Build & Install

```bash
make build
sudo make install
```

Clones FFmpeg `release/8.0`, applies `custom_ffmpeg.diff`, compiles, and installs to `/usr/local`.

### Verify

```bash
make verify
```

---

## Windows (MSYS2 / MinGW64)

All commands below must be run from the **MSYS2 MinGW x64** shell (not "MSYS2 MSYS").

### 1 — Install build dependencies (once)

```bash
make -f makefile.windows deps
```

Installs the MinGW64 toolchain, nasm, pkg-config, git, patch, and diffutils via `pacman`.

### 2 — Build FFmpeg

```bash
make -f makefile.windows build
```

Clones `release/8.0`, applies the custom patch, runs `./configure`, and compiles.

### 3 — Install

```bash
make -f makefile.windows install
```

Installs to `./ffmpeg-custom/` by default. Override with `PREFIX`:

```bash
make -f makefile.windows install PREFIX=/c/ffmpeg-custom
```

The DLLs land in `$(PREFIX)/bin/`. Add that folder to your Windows `PATH`, or copy the `.dll` files next to any binary that uses them.

### 4 — Verify

```bash
make -f makefile.windows verify
```

---

## Motion Vector Extraction

### Visualise MVs (overlay on video)

Renders motion vector arrows on top of the decoded frames and writes the result to a video file. Useful for a quick visual sanity check.

```bash
make -f makefile.windows test-mv VIDEO=clip.mp4
```

| Variable | Default | Description |
|---|---|---|
| `VIDEO` | `bigbunny_walking.mp4` | Input video |
| `TEST_OUTPUT` | `mv_test.mp4` | Output video with MV overlay |

### Extract MVs to CSV / JSON — C++ extractor

A self-contained C++ extractor (`extractor1.cpp`) that mirrors `extractor1.rs` exactly: it sets `motion_vectors_only=1`, opens the decoder with `AV_CODEC_EXPORT_DATA_MVS`, and streams motion vectors directly to a CSV file.

**Build:**

```bash
# custom FFmpeg (compact MVs — 6 columns, fast path)
make -f makefile.windows build-extractor

# stock FFmpeg headers (full MVs — 12 columns)
make -f makefile.windows build-extractor CUSTOM=0
```

**Run:**

```bash
make -f makefile.windows run-extractor VIDEO=clip.mp4 MV_CSV=out.csv
```

Or call the binary directly:

```bash
./extractor.exe clip.mp4 out.csv
```

Prints `<frame_count> <total_mvs>` to stdout on completion.

**CSV columns:**

| Mode | Columns |
|---|---|
| `CUSTOM=1` (default) | `frame, source, src_x, src_y, dst_x, dst_y` |
| `CUSTOM=0` | `frame, source, w, h, src_x, src_y, dst_x, dst_y, flags, motion_x, motion_y, motion_scale` |

| Variable | Default | Description |
|---|---|---|
| `EXTRACTOR_EXE` | `extractor.exe` | Output binary name |
| `CUSTOM` | `1` | `1` = custom FFmpeg (compact), `0` = stock |
| `CXXFLAGS` | `-std=c++17 -O2` | Extra compiler flags |

---

## What the patch changes

The diff in [custom_ffmpeg.diff](custom_ffmpeg.diff) adds a *motion-vectors-only* fast path to the H.264, HEVC, and MPEG decoders. When the caller sets `AVCodecContext::motion_vectors_only = 1`, the decoder parses just enough of the bitstream to recover motion vectors and skips pixel reconstruction (residual IDCT, intra prediction, deblocking, SAO, film grain, etc.).

### New `AVMotionVectorCompact` side data

Because the mv-only consumer never looks at reconstructed pixels, most fields of the stock `AVMotionVector` struct are dead weight. The patch introduces a slimmer variant exported through a new side-data type `AV_FRAME_DATA_MOTION_VECTORS_COMPACT`.

| Field | `AVMotionVector` (stock, ~40 B) | `AVMotionVectorCompact` (patched, 12 B) |
|---|---|---|
| `source` (int32) | yes | yes |
| `src_x`, `src_y` (int16) | yes | yes |
| `dst_x`, `dst_y` (int16) | yes | yes |
| `w`, `h` (uint8 block size) | yes | **dropped** |
| `flags` (uint64) | yes | **dropped** |
| `motion_x`, `motion_y` (int32) | yes | **dropped** |
| `motion_scale` (uint16) | yes | **dropped** |

Per-MV memory drops from ~40 B to 12 B, and the decoder writes directly into the side-data buffer, removing a temporary allocation and memcpy.

### Selecting the output

| `motion_vectors_only` | Decoder behaviour | Side data emitted |
|---|---|---|
| `0` (default) | Full decode, same as stock FFmpeg | `AV_FRAME_DATA_MOTION_VECTORS` → `AVMotionVector` |
| `1` | Skip pixel reconstruction | `AV_FRAME_DATA_MOTION_VECTORS_COMPACT` → `AVMotionVectorCompact` |
