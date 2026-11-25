/* video_test_common.h — shared helpers for the T0 video contract tests.
 *
 * Shared by tests/test_video_mp4_h264.c and tests/test_video_webm_vp9.c.
 * Pure test support: no production code lives here. Pixel assertions use the
 * dominant-channel rule with a channel margin; lossy YUV → RGB conversion
 * leaves up to ±10 channel deviation, so exact RGB comparison is avoided.
 */
#ifndef ZCSR_VIDEO_TEST_COMMON_H
#define ZCSR_VIDEO_TEST_COMMON_H

#include "zcsr/video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int zcsr_video_test_failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { \
        printf("FAIL %s (line %d)\n", (name), __LINE__); \
        ++zcsr_video_test_failures; \
    } \
} while (0)

static const uint8_t* vtx_pixel_at(const zcsr_video_frame* f, uint32_t x, uint32_t y) {
    return f->rgba + ((size_t)y * f->width + x) * 4u;
}

/* Block sample points match tests/video_data/FIXTURES.md:
 * red (8,8) · green (56,8) · blue (32,48). */
static void vtx_inspect_frame(const zcsr_video_frame* f, const char* label,
                              uint64_t frame_no) {
    const uint8_t* red   = vtx_pixel_at(f, 8u, 8u);
    const uint8_t* green = vtx_pixel_at(f, 56u, 8u);
    const uint8_t* blue  = vtx_pixel_at(f, 32u, 48u);

    printf("[%s] frame %llu: red=(%d,%d,%d) green=(%d,%d,%d) blue=(%d,%d,%d) alpha=%d\n",
           label, (unsigned long long)frame_no,
           red[0], red[1], red[2],
           green[0], green[1], green[2],
           blue[0], blue[1], blue[2], red[3]);

    CHECK(f->width == 64u && f->height == 64u, "frame dimensions match fixture");
    CHECK(red[3] == 0xFFu && green[3] == 0xFFu && blue[3] == 0xFFu, "alpha opaque");
    CHECK(red[0] > red[1] + 30 && red[0] > red[2] + 30 && red[0] >= 150,
          "red block decoded (R dominant)");
    CHECK(green[1] > green[0] + 30 && green[1] > green[2] + 30 && green[1] >= 100,
          "green block decoded (G dominant)");
    CHECK(blue[2] > blue[0] + 30 && blue[2] > blue[1] + 30 && blue[2] >= 100,
          "blue block decoded (B dominant)");
}

static void vtx_check_caps(void) {
    zcsr_video_container_caps cc;
    zcsr_video_codec_caps kc;
    memset(&cc, 0, sizeof(cc));
    memset(&kc, 0, sizeof(kc));
    zcsr_video_platform_caps(&cc, &kc);

    printf("caps: mp4=%d webm=%d h264=%d vp9=%d\n",
           cc.mp4, cc.webm, kc.h264, kc.vp9);
    CHECK(cc.mp4, "caps: MP4 reachable");
    CHECK(cc.webm, "caps: WebM reachable");
    CHECK(kc.h264, "caps: H.264 decoder present");
    CHECK(kc.vp9, "caps: VP9 decoder present");
}

/* Positive decode/seek/EOF case for one fixture. Returns 0 on success. */
static int vtx_run_case(const char* data_dir, const char* file,
                        zcsr_video_container expect_container,
                        zcsr_video_codec expect_codec, const char* label) {
    char path[512];
    zcsr_video_error err = ZCSR_VIDEO_OK;
    zcsr_video* video = NULL;
    zcsr_video_info info;
    zcsr_video_frame frame;
    uint8_t* buffer = NULL;
    size_t frame_bytes;
    size_t decoded = 0u;
    size_t i;
    double fps;

    snprintf(path, sizeof(path), "%s/%s", data_dir, file);
    printf("[%s] open %s\n", label, path);

    video = zcsr_video_open(path, &err);
    if (!video) {
        printf("[%s] open failed: %s (%d)\n", label,
               zcsr_video_error_string(err), (int)err);
        CHECK(0, "fixture opens");
        return 1;
    }

    CHECK(zcsr_video_get_info(video, &info) == ZCSR_VIDEO_OK, "info accepted");
    printf("[%s] info: %ux%u container=%d codec=%d fps=%u/%u duration=%ums frames=%u pixel=%d\n",
           label, info.width, info.height, (int)info.container, (int)info.codec,
           info.fps_numerator, info.fps_denominator, info.duration_ms,
           info.frame_count, (int)info.pixel_format);

    CHECK(info.width == 64u && info.height == 64u, "info dimensions match fixture");
    CHECK(info.container == expect_container, "info container matches fixture");
    CHECK(info.codec == expect_codec, "info codec matches fixture");
    CHECK(info.fps_denominator > 0u && info.fps_numerator > 0u, "frame rate reported");
    if (info.fps_denominator > 0u && info.fps_numerator > 0u) {
        fps = (double)info.fps_numerator / (double)info.fps_denominator;
        CHECK(fps >= 10.0 && fps <= 14.0, "frame rate ~12 fps");
    }

    frame_bytes = zcsr_video_frame_bytes(video);
    CHECK(frame_bytes == 64u * 64u * 4u, "frame buffer size = RGBA8");
    buffer = (uint8_t*)malloc(frame_bytes);
    CHECK(buffer != NULL, "frame buffer allocated");
    if (!buffer) {
        zcsr_video_close(video);
        return 1;
    }

    /* First frame: decode + block pixels. */
    err = zcsr_video_read_frame(video, buffer, frame_bytes, &frame);
    CHECK(err == ZCSR_VIDEO_OK, "first frame decoded");
    if (err == ZCSR_VIDEO_OK) {
        vtx_inspect_frame(&frame, label, frame.frame_index);
        decoded = 1u;
    }

    /* Drain the stream; dimensions stay stable and pixels stay non-zero. */
    for (i = 1u; i < 32u; ++i) {
        err = zcsr_video_read_frame(video, buffer, frame_bytes, &frame);
        if (err == ZCSR_VIDEO_ERR_EOF) {
            break;
        }
        if (err != ZCSR_VIDEO_OK) {
            CHECK(0, "subsequent frames decode");
            break;
        }
        CHECK(frame.width == 64u && frame.height == 64u, "drained frame dimensions");
        ++decoded;
    }
    printf("[%s] drained %zu frames total\n", label, decoded);
    CHECK(decoded >= 3u, "at least three frames decoded");

    /* seek_frame to index 2: the next frame must land on index 2. */
    err = zcsr_video_seek_frame(video, 2u);
    if (err != ZCSR_VIDEO_OK) {
        printf("[%s] seek_frame(2) error: %s (%d)\n", label,
               zcsr_video_error_string(err), (int)err);
    }
    CHECK(err == ZCSR_VIDEO_OK, "seek by frame index accepted");
    if (err == ZCSR_VIDEO_OK) {
        err = zcsr_video_read_frame(video, buffer, frame_bytes, &frame);
        CHECK(err == ZCSR_VIDEO_OK, "frame after index seek decoded");
        if (err == ZCSR_VIDEO_OK) {
            printf("[%s] after seek_frame 2: index=%llu time=%llums\n",
                   label, (unsigned long long)frame.frame_index,
                   (unsigned long long)frame.time_ms);
            CHECK(frame.frame_index == 2u, "seek lands on requested frame");
            vtx_inspect_frame(&frame, label, frame.frame_index);
        }
    }

    /* seek_ms back to time 0: first-frame blocks visible again. */
    err = zcsr_video_seek_ms(video, 0u);
    CHECK(err == ZCSR_VIDEO_OK, "seek to time 0 accepted");
    if (err == ZCSR_VIDEO_OK) {
        err = zcsr_video_read_frame(video, buffer, frame_bytes, &frame);
        CHECK(err == ZCSR_VIDEO_OK, "frame after time-0 seek decoded");
        if (err == ZCSR_VIDEO_OK) {
            CHECK(frame.frame_index == 0u, "time-0 seek lands on frame 0");
            vtx_inspect_frame(&frame, label, frame.frame_index);
        }
    }

    /* Drain again: EOF must be reported explicitly, not a silent repeat. */
    for (;;) {
        err = zcsr_video_read_frame(video, buffer, frame_bytes, &frame);
        if (err == ZCSR_VIDEO_ERR_EOF) {
            break;
        }
        if (err != ZCSR_VIDEO_OK) {
            CHECK(0, "drain after time-0 seek decodes cleanly");
            break;
        }
    }
    CHECK(err == ZCSR_VIDEO_ERR_EOF, "EOF reported after draining");

    /* Out-of-range frame seek is rejected without changing stream state. */
    err = zcsr_video_seek_frame(video, 1000u);
    CHECK(err == ZCSR_VIDEO_ERR_OUT_OF_RANGE, "seek past last frame rejected");

    free(buffer);
    zcsr_video_close(video);
    return zcsr_video_test_failures != 0 ? 1 : 0;
}

/* Negative paths: missing file, disguised extension, truncated file, NULL args. */
static void vtx_run_negatives(const char* data_dir, const char* fixture_file) {
    char path[512];
    char fixture_path[512];
    zcsr_video* video;
    zcsr_video_error err;
    FILE* f;
    static const uint8_t junk[] = "this is plainly not a video container";
    uint8_t truncated[128];
    size_t trunc_len = 0u;

    snprintf(path, sizeof(path), "%s/__zcsr_nothing.mp4", data_dir);
    video = zcsr_video_open(path, &err);
    CHECK(video == NULL && err == ZCSR_VIDEO_ERR_NOT_FOUND, "missing file -> NOT_FOUND");
    printf("missing file error: %s\n", zcsr_video_error_string(err));

    /* Content sniffing: a text blob renamed with a video extension must fail. */
    snprintf(path, sizeof(path), "%s/__zcsr_fake.mp4", data_dir);
    f = fopen(path, "wb");
    if (f) {
        fwrite(junk, 1u, sizeof(junk), f);
        fclose(f);
    }
    video = zcsr_video_open(path, &err);
    CHECK(video == NULL && err == ZCSR_VIDEO_ERR_NOT_VIDEO,
          "disguised extension -> NOT_VIDEO");
    printf("fake mp4 error: %s\n", zcsr_video_error_string(err));
    (void)remove(path);

    /* Truncated real fixture: open must fail explicitly, never crash or succeed. */
    snprintf(fixture_path, sizeof(fixture_path), "%s/%s", data_dir, fixture_file);
    f = fopen(fixture_path, "rb");
    if (f) {
        trunc_len = fread(truncated, 1u, sizeof(truncated), f);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/__zcsr_trunc.bin", data_dir);
    f = fopen(path, "wb");
    if (f && trunc_len > 0u) {
        fwrite(truncated, 1u, trunc_len, f);
        fclose(f);
    }
    video = zcsr_video_open(path, &err);
    CHECK(video == NULL, "truncated file does not open");
    CHECK(err != ZCSR_VIDEO_OK, "truncated file returns an explicit error code");
    printf("truncated file error: %s (%d)\n", zcsr_video_error_string(err), (int)err);
    (void)remove(path);

    /* NULL-argument contract: no crash, explicit error. */
    CHECK(zcsr_video_open(NULL, &err) == NULL, "NULL path rejected");
    CHECK(zcsr_video_last_error(NULL) == ZCSR_VIDEO_ERR_INVALID_ARG,
          "NULL handle last_error");
    zcsr_video_close(NULL);
}

#endif /* ZCSR_VIDEO_TEST_COMMON_H */
