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

A self-contained C++ extractor (`extractor.cpp`) modelled on `extractor1.rs`: it sets `motion_vectors_only=1`, opens the decoder with `AV_CODEC_EXPORT_DATA_MVS`, and streams motion vectors directly to a CSV file. It differs from `extractor1.rs` in one respect — it filters the rows it writes, see [Row filtering](#row-filtering) below.

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

<a name="row-filtering"></a>
**Row filtering.** Only list-0, non-zero-size vectors are written. This is the
same subset the benchmark's comparison step joins on.

Against custom FFmpeg the filtering happens **in the decoder**, the way
`extractor1.rs` does it: the extractor sets the `mv_l0_only` AVOption added by
`custom_ffmpeg.diff`, so direction 1 is never built and the MV buffer is sized
for one direction. Zero-size vectors are likewise dropped inside the patched
decoder. Set `L0_ONLY=0` to export list-1 rows as well.

Stock FFmpeg has neither, so `av_opt_set_int` returns
`AVERROR_OPTION_NOT_FOUND` and the `keep_mv()` predicate in `extractor.cpp`
applies the same two rules in user space. That is what makes a `CUSTOM=0` build
produce the same subset as `CUSTOM=1`. `keep_mv()` honours `L0_ONLY` too, and
the printed `<total_mvs>` counts rows actually written, not raw side-data
entries.

- **`source == -1` (list-0 only).** H.264 and MPEG set `source` to exactly `-1`
  (list 0) or `+1` (list 1), so this is an exact list-0 filter for them. HEVC
  instead encodes `±(ref_idx + 1)`, so *its* list-0 rows with `ref_idx > 0`
  arrive as `-2`, `-3`, … and are dropped as well. Widen `keep_mv()` to
  `source < 0` if you need those.
- **No zero-size vectors.** `src == dst` means the block did not move, so there
  is no displacement to record. This also drops sub-pel motion that truncates
  to zero (e.g. `motion_x = 2` at `motion_scale = 4`).

Note the two builds agree on *which subset* is exported, not necessarily on
every vector: the patched decoder still differs slightly from stock on B-frame
content. On `dashcam.mp4` (no B-frames) both emit an identical 757,795 rows; on
`MCTTR0102b.mp4` they differ by ~461 of ~475,000. That gap is a pre-existing
decoder difference, unrelated to this filtering.

Build variables:

| Variable | Default | Description |
|---|---|---|
| `EXTRACTOR_EXE` | `extractor.exe` | Output binary name |
| `CUSTOM` | `1` | `1` = custom FFmpeg (compact), `0` = stock |
| `CXXFLAGS` | `-std=c++17 -O2` | Extra compiler flags |

Runtime environment variables (read by the binary, not by `make`):

| Variable | Default | Description |
|---|---|---|
| `L0_ONLY` | `1` | `1` = export list-0 only. `0` = also export list-1 (`source > 0`) rows. See [Row filtering](#row-filtering). |

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

### Fewer exported vectors

Two further reductions apply to both side-data flavours, so the decoder never
builds rows the mv-only consumer would discard.

**`mv_l0_only` (new AVOption, default `0`).** When set, MV export skips
direction 1 entirely, emitting only list-0 (`source < 0`) vectors and dropping
list-1 (`source > 0`). P slices are list-0 only and unaffected; B slices keep
just their list-0 vectors; I slices stay empty. The worst-case MV buffer is
sized for one direction instead of two. Set it with:

```c
av_opt_set_int(dec_ctx, "mv_l0_only", 1, 0);
```

**Zero-size vectors are dropped.** A vector whose source and destination
coincide (`src_x == dst_x && src_y == dst_y`) records no displacement, so it is
not exported. This covers exactly-zero motion and sub-pel motion that truncates
to zero (e.g. `motion_x = 2` at `motion_scale = 4`). Unconditional — there is no
option to re-enable them; stock FFmpeg still emits them.

Both are what `extractor.cpp` and `extractor1.rs` rely on rather than filtering
after the fact — see [Row filtering](#row-filtering).
