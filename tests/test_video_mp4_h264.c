/* Contract test: MP4 / H.264 T0 video fixture.
 *
 * Usage: video_mp4_h264_test [<video_data_dir>]
 * Falls back to the ZCSR_VIDEO_DATA_DIR environment variable, then ".".
 *
 * Asserts: open, info (container/codec/dimensions/rate), >=3 decoded frames
 * with dominant-color block pixels, seek_frame to index 2, seek_ms to frame 0,
 * explicit EOF, out-of-range seek rejection, and the negative failure paths.
 */
#include "video_data/video_test_common.h"

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : getenv("ZCSR_VIDEO_DATA_DIR");
    if (!data_dir || !*data_dir) {
        data_dir = ".";
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("video_mp4_h264: caps start\n");
    vtx_check_caps();

    printf("video_mp4_h264: fixture start\n");
    vtx_run_case(data_dir, "t0_h264.mp4",
                 ZCSR_VIDEO_CONTAINER_MP4, ZCSR_VIDEO_CODEC_H264, "mp4/h264");

    printf("video_mp4_h264: failure paths start\n");
    vtx_run_negatives(data_dir, "t0_h264.mp4");

    if (zcsr_video_test_failures == 0) {
        printf("video_mp4_h264 PASS\n");
        return 0;
    }
    printf("video_mp4_h264 FAIL (%d)\n", zcsr_video_test_failures);
    return 1;
}
