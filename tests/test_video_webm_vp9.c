/* Contract test: WebM / VP9 T0 video fixture.
 *
 * Usage: video_webm_vp9_test [<video_data_dir>]
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

    printf("video_webm_vp9: caps start\n");
    vtx_check_caps();

    printf("video_webm_vp9: fixture start\n");
    vtx_run_case(data_dir, "t0_vp9.webm",
                 ZCSR_VIDEO_CONTAINER_WEBM, ZCSR_VIDEO_CODEC_VP9, "webm/vp9");

    printf("video_webm_vp9: failure paths start\n");
    vtx_run_negatives(data_dir, "t0_vp9.webm");

    if (zcsr_video_test_failures == 0) {
        printf("video_webm_vp9 PASS\n");
        return 0;
    }
    printf("video_webm_vp9 FAIL (%d)\n", zcsr_video_test_failures);
    return 1;
}
