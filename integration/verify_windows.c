/* integration/verify_windows.c — Windows 后端五项验证。
 * 需要：WASAPI 音频 + XInput 手柄 + Win32 窗口。
 * 运行后会自动完成 1、2、5 三项，3、4 两项需要你手动操作。 */
#include "zcsr/audio_mix.h"
#include "zcsr/input_ext.h"
#include "zcsr/platform.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#if !defined(_WIN32)
int main(void) { printf("仅 Windows 下有意义\n"); return 0; }
#else
#include <windows.h>
/* --- 生成一个 0.5 秒 44100Hz 单声道 440Hz 正弦波 WAV 到磁盘 --- */
static bool write_test_wav(const char* path) {
    const int rate = 44100;
    const int samples = rate / 2;
    const int data_bytes = samples * 2;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    unsigned char hdr[44] = {0};
    unsigned int chunk = 36 + data_bytes;
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 4, &chunk, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    unsigned int fmt_size = 16; memcpy(hdr + 16, &fmt_size, 4);
    unsigned short fmt = 1; memcpy(hdr + 20, &fmt, 2);
    unsigned short ch = 1; memcpy(hdr + 22, &ch, 2);
    unsigned int r = rate; memcpy(hdr + 24, &r, 4);
    unsigned int br = rate * 2; memcpy(hdr + 28, &br, 4);
    unsigned short ba = 2; memcpy(hdr + 32, &ba, 2);
    unsigned short bps = 16; memcpy(hdr + 34, &bps, 2);
    memcpy(hdr + 36, "data", 4);
    unsigned int ds = data_bytes; memcpy(hdr + 40, &ds, 4);
    fwrite(hdr, 1, 44, f);
    for (int i = 0; i < samples; ++i) {
        double t = (double)i / rate;
        int16_t s = (int16_t)(sin(2.0 * 3.14159265358979 * 440.0 * t) * 12000.0);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    return true;
}
int main(void) {
    printf("=== zcsr Windows 后端验证 ===\n\n");
    /* ---------- 1. 音频 ---------- */
    printf("[1] 音频：注册 WAV 并播放（应能听到 0.5 秒 440Hz 提示音）\n");
    const char* wav_path = "zcsr_verify.wav";
    if (!write_test_wav(wav_path)) { printf("    失败：无法写临时 WAV\n"); }
    zcsr_mixer* mixer = zcsr_mix_start();
    if (!mixer) {
        printf("    失败：zcsr_mix_start() 返回 NULL（WASAPI 未打开）\n");
    } else {
        if (!zcsr_mix_register_wav(mixer, "beep", wav_path)) {
            printf("    失败：register_wav 失败\n");
        } else {
            int ch = zcsr_mix_play(mixer, "beep", 0.6f, false);
            printf("    play 返回 channel=%d（-1 表示失败）\n", ch);
            Sleep(800);
            int fin[16]; size_t n = zcsr_mix_poll_finished(mixer, fin, 16);
            printf("    finished 通道数=%zu\n", n);
        }
    }
    /* ---------- 4. 鼠标焦点（等待用户操作） ---------- */
    printf("\n[4] 鼠标焦点：3 秒内切到别的窗口后按左键，应不响应\n");
    printf("    3 秒后开始采样...\n");
    Sleep(3000);
    {
        unsigned char buf[1024];
        zcsr_surface* s = zcsr_surface_create("zcsr-verify", (zcsr_rect){100,100,640,480});
        zcsr_input_ext* in = (zcsr_input_ext*)buf;
        if (!s) { printf("    失败：无法创建 surface\n"); }
        else {
            in = zcsr_input_ext_init(buf, sizeof buf);
            if (!in) { printf("    失败：input_ext_init\n"); }
            else {
                /* 采样 2 秒，观察鼠标按键 */
                for (int i = 0; i < 120; ++i) {
                    zcsr_surface_pump_events(s);
                    zcsr_input_ext_pump(in, s);
                    zcsr_mouse_state ms = zcsr_input_ext_mouse(in);
                    if (ms.buttons) {
                        printf("    采样 %d：buttons=0x%02x（应只在你操作本窗口时出现）\n", i, ms.buttons);
                    }
                    Sleep(16);
                }
                printf("    采样结束\n");
                /* ---------- 5. 滚轮 ---------- */
                printf("\n[5] 滚轮：3 秒内在本窗口内滚动，观察 wheel 变化\n");
                for (int i = 0; i < 180; ++i) {
                    zcsr_surface_pump_events(s);
                    zcsr_input_ext_pump(in, s);
                    zcsr_mouse_state ms = zcsr_input_ext_mouse(in);
                    if (ms.wheel) printf("    wheel=%d\n", ms.wheel);
                    Sleep(16);
                }
                zcsr_input_ext_shutdown(in);
            }
            zcsr_surface_destroy(s);
        }
    }
    /* ---------- 2. 手柄 ---------- */
    printf("\n[2] 手柄：3 秒内按手柄 A 键或推左摇杆\n");
    {
        unsigned char buf[1024];
        zcsr_input_ext* in = zcsr_input_ext_init(buf, sizeof buf);
        if (!in) { printf("    失败：input_ext_init\n"); }
        else {
            for (int i = 0; i < 180; ++i) {
                zcsr_input_ext_pump(in, NULL);
                zcsr_pad_state p = zcsr_input_ext_pad(in, 0);
                if (p.connected && (p.buttons || fabsf(p.lx) > 0.1f || fabsf(p.ly) > 0.1f)) {
                    printf("    pad0: buttons=0x%04x lx=%.2f ly=%.2f lt=%.2f rt=%.2f\n",
                           p.buttons, p.lx, p.ly, p.lt, p.rt);
                }
                Sleep(16);
            }
            zcsr_input_ext_shutdown(in);
        }
    }
    if (mixer) zcsr_mix_stop(mixer);
    remove(wav_path);
    printf("\n=== 验证结束 ===\n");
    printf("把上面全部输出复制给审查员。\n");
    return 0;
}
#endif
