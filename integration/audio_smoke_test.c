/* audio_smoke_test.c — minimal audiomix smoke test.
 * Build: cc audio_smoke_test.c -o audio_smoke_test -I include zcsr_audiomix.lib zcsr_signal.lib
 * Run:   ./audio_smoke_test
 * Expected: 3 seconds of 440Hz beep, then exit.
 */
#include "zcsr/audio_mix.h"
#include <stdio.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void sleep_ms(unsigned ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, 0);
}
#endif

int main(void) {
    printf("[smoke] starting mixer...\n");
    zcsr_mixer* m = zcsr_mix_start();
    if (!m) {
        printf("[smoke] FAIL: zcsr_mix_start returned NULL\n");
        return 1;
    }
    printf("[smoke] mixer started at %p\n", (void*)m);

    /* click.wav is 300ms 440Hz beep in integration/ */
    if (!zcsr_mix_register_wav(m, "beep", "../integration/click.wav")) {
        printf("[smoke] FAIL: register_wav failed (run from build-box2d/)\n");
        zcsr_mix_stop(m);
        return 1;
    }
    printf("[smoke] beep.wav registered\n");

    printf("[smoke] playing beep (loop=true) for 3 seconds...\n");
    int ch = zcsr_mix_play(m, "beep", 1.0f, true);
    printf("[smoke] zcsr_mix_play returned channel=%d (-1=fail)\n", ch);

    if (ch >= 0) {
        sleep_ms(3000);
        printf("[smoke] stopping channel %d\n", ch);
        zcsr_mix_stop_channel(m, ch);
        sleep_ms(500);
    }

    printf("[smoke] stopping mixer...\n");
    zcsr_mix_stop(m);
    printf("[smoke] done. If you heard a 3-second beep, WASAPI backend works.\n");
    return 0;
}
