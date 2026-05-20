// extractor.cpp — Motion vector extractor
//
//   - sets motion_vectors_only=1 and export_side_data=+mvs during
//     avformat_find_stream_info (same as set_av_flags)
//   - sets AV_CODEC_EXPORT_DATA_MVS + motion_vectors_only=1 on the decoder
//   - writes AV_FRAME_DATA_MOTION_VECTORS_COMPACT when built with
//     -DCUSTOM_FFMPEG, AV_FRAME_DATA_MOTION_VECTORS otherwise
//
// Usage:
//   extractor.exe <input_video> <output.csv>
//
// Build (MSYS2 MinGW64, custom FFmpeg):
//   g++ -std=c++17 -O2 -I<PREFIX>/include -L<PREFIX>/lib \
//       extractor1.cpp -o extractor1.exe \
//       -lavformat -lavcodec -lavutil -DCUSTOM_FFMPEG
//
// Build (stock FFmpeg):
//   g++ -std=c++17 -O2 -I<PREFIX>/include -L<PREFIX>/lib \
//       extractor1.cpp -o extractorx1.exe \
//       -lavformat -lavcodec -lavutil

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/motion_vector.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
}

// ── helpers ──────────────────────────────────────────────────────────────────

// Write "0x<hex>" for the flags field, matching the Rust push_hex_u64 output.
static void write_hex(FILE* f, uint64_t v) {
    fprintf(f, "0x%llx", (unsigned long long)v);
}

// ── full AVMotionVector ───────────────────────────────────────────────────────

static void write_full_header(FILE* f) {
    fputs("frame,source,w,h,src_x,src_y,dst_x,dst_y,flags,motion_x,motion_y,motion_scale\n", f);
}

static long long write_full_mvs(FILE* csv, int frame_num, AVFrame* frame) {
    AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
    if (!sd || !sd->data || sd->size <= 0) return 0;

    const AVMotionVector* mvs = reinterpret_cast<const AVMotionVector*>(sd->data);
    int count = (int)(sd->size / sizeof(AVMotionVector));
    long long written = 0;

    for (int i = 0; i < count; i++) {
        const AVMotionVector* mv = &mvs[i];
        if (mv->w == 0 || mv->h == 0) continue;
        fprintf(csv, "%d,%d,%d,%d,%d,%d,%d,%d,",
                frame_num,
                (int)mv->source,
                (int)mv->w, (int)mv->h,
                (int)mv->src_x, (int)mv->src_y,
                (int)mv->dst_x, (int)mv->dst_y);
        write_hex(csv, mv->flags);
        fprintf(csv, ",%d,%d,%d\n",
                (int)mv->motion_x, (int)mv->motion_y, (int)mv->motion_scale);
        written++;
    }
    return written;
}

// ── compact AVMotionVectorCompact (custom FFmpeg patch) ───────────────────────

#ifdef CUSTOM_FFMPEG

static void write_compact_header(FILE* f) {
    fputs("frame,source,src_x,src_y,dst_x,dst_y\n", f);
}

static long long write_compact_mvs(FILE* csv, int frame_num, AVFrame* frame) {
    AVFrameSideData* sd = av_frame_get_side_data(
        frame, AV_FRAME_DATA_MOTION_VECTORS_COMPACT);
    if (!sd || !sd->data || sd->size <= 0) return 0;

    const AVMotionVectorCompact* mvs =
        reinterpret_cast<const AVMotionVectorCompact*>(sd->data);
    int count = (int)(sd->size / sizeof(AVMotionVectorCompact));

    for (int i = 0; i < count; i++) {
        const AVMotionVectorCompact* mv = &mvs[i];
        fprintf(csv, "%d,%d,%d,%d,%d,%d\n",
                frame_num,
                (int)mv->source,
                (int)mv->src_x, (int)mv->src_y,
                (int)mv->dst_x, (int)mv->dst_y);
    }
    return count;
}

#endif // CUSTOM_FFMPEG

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_video> <output.csv>\n", argv[0]);
        return 1;
    }
    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    FILE* csv = fopen(output_path, "w");
    if (!csv) {
        fprintf(stderr, "Could not open output file: %s\n", output_path);
        return 1;
    }

#ifdef CUSTOM_FFMPEG
    write_compact_header(csv);
#else
    write_full_header(csv);
#endif

    avformat_network_init();

    // Open input
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_path, nullptr, nullptr) < 0) {
        fprintf(stderr, "Could not open input: %s\n", input_path);
        fclose(csv);
        return 1;
    }

    // Mirror set_av_flags: set motion_vectors_only + export_side_data on every
    // stream so the probing codecs inside avformat_find_stream_info also use it.
    AVDictionary** stream_opts =
        static_cast<AVDictionary**>(calloc(fmt_ctx->nb_streams, sizeof(AVDictionary*)));
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        av_dict_set(&stream_opts[i], "motion_vectors_only", "1", 0);
        av_dict_set(&stream_opts[i], "export_side_data",    "+mvs", 0);
    }

    if (avformat_find_stream_info(fmt_ctx, stream_opts) < 0) {
        fprintf(stderr, "Could not find stream info\n");
        return 1;
    }

    // Mirror unset_av_flags
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (stream_opts[i]) av_dict_free(&stream_opts[i]);
    }
    free(stream_opts);

    // Find first video stream
    int vsi = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vsi = (int)i;
            break;
        }
    }
    if (vsi < 0) {
        fprintf(stderr, "No video stream found\n");
        return 1;
    }
    AVStream* vstream = fmt_ctx->streams[vsi];

    // Open decoder
    const AVCodec* codec = avcodec_find_decoder(vstream->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        return 1;
    }

    AVCodecContext* dec_ctx = avcodec_alloc_context3(codec);
    if (!dec_ctx || avcodec_parameters_to_context(dec_ctx, vstream->codecpar) < 0) {
        fprintf(stderr, "Could not set up codec context\n");
        return 1;
    }

    dec_ctx->thread_count    = 0; // auto
    dec_ctx->export_side_data |= AV_CODEC_EXPORT_DATA_MVS;
    av_opt_set_int(dec_ctx, "motion_vectors_only", 1, 0);

    AVDictionary* dec_opts = nullptr;
    if (avcodec_open2(dec_ctx, codec, &dec_opts) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return 1;
    }

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) {
        fprintf(stderr, "Could not allocate packet/frame\n");
        return 1;
    }

    int       frame_num = 0;
    long long total_mvs = 0;

    auto process_frame = [&]() {
#ifdef CUSTOM_FFMPEG
        total_mvs += write_compact_mvs(csv, frame_num, frame);
#else
        total_mvs += write_full_mvs(csv, frame_num, frame);
#endif
        av_frame_unref(frame);
        frame_num++;
    };

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == vsi) {
            int ret = avcodec_send_packet(dec_ctx, pkt);
            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;
                process_frame();
            }
        }
        av_packet_unref(pkt);
    }

    // Flush decoder
    avcodec_send_packet(dec_ctx, nullptr);
    while (avcodec_receive_frame(dec_ctx, frame) == 0)
        process_frame();

    fflush(csv);
    fclose(csv);

    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    printf("%d %lld\n", frame_num, total_mvs);
    return 0;
}
