/* raylib worst-case bench — same three scenarios as zcsr bench_worst.c.
 *
 * PURPOSE: cross-engine comparison. Compile this against raylib and run
 * alongside zcsr_glrender_worst_bench to compare worst-case 2D throughput.
 *
 * COMPILE (Windows, MSVC):
 *   cl bench_worst_raylib.c /I C:\path\to\raylib\include /link C:\path\to\raylib\lib\raylib.lib opengl32.lib gdi32.lib winmm.lib
 *
 * COMPILE (gcc/mingw):
 *   gcc bench_worst_raylib.c -o bench_worst_raylib -lraylib -lopengl32 -lgdi32 -lwinmm
 *
 * Scenarios (identical to zcsr bench_worst.c):
 *   A. 1000 sprites x 1000 unique textures  (texture switch every sprite)
 *   B. 10000 sprites x 1 texture             (all same texture)
 *   C. 1000 sprites x mixed blend modes      (blend mode switch every sprite)
 */
#include "raylib.h"
#include <stdio.h>

#define MAX_TEXTURES 1000
#define ITERS        240

/* --- best-of-N frame time for a scenario --- */
static double bench_scenario(Texture2D* textures, int scenario) {
    double best = 1e9;
    for (int iter = 0; iter < ITERS; ++iter) {
        double t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);

        if (scenario == 0) {
            /* A: 1000 sprites x 1000 unique textures */
            for (int i = 0; i < 1000; ++i) {
                int x = (i * 17) % 760;
                int y = (i * 29) % 560;
                DrawTexture(textures[i], x, y, WHITE);
            }
        } else if (scenario == 1) {
            /* B: 10000 sprites x 1 texture */
            for (int i = 0; i < 10000; ++i) {
                int x = (i * 17) % 760;
                int y = (i * 29) % 560;
                DrawTexture(textures[0], x, y, WHITE);
            }
        } else {
            /* C: 1000 sprites x mixed blend modes
             *    raylib: BeginBlendMode / EndBlendMode around each draw.
             *    Frequent mode switches force driver state changes. */
            for (int i = 0; i < 1000; ++i) {
                int x = (i * 17) % 760;
                int y = (i * 29) % 560;
                BlendMode mode = (i % 3 == 0) ? BLEND_ALPHA :
                                  (i % 3 == 1) ? BLEND_ADDITIVE : BLEND_MULTIPLIED;
                BeginBlendMode(mode);
                DrawTexture(textures[0], x, y, WHITE);
                EndBlendMode();
            }
        }

        EndDrawing();
        double dt = (GetTime() - t0) * 1000.0;
        if (dt < best) best = dt;
    }
    return best;
}

int main(void) {
    InitWindow(800, 600, "raylib worst-case bench");
    SetTargetFPS(0); /* uncap */

    /* create 1000 unique 2x2 textures, each a distinct color */
    Texture2D textures[MAX_TEXTURES];
    for (int i = 0; i < MAX_TEXTURES; ++i) {
        Color c = {
            (unsigned char)(i & 0xFF),
            (unsigned char)((i * 2) & 0xFF),
            (unsigned char)((i * 3) & 0xFF),
            255
        };
        Image img = GenImageColor(2, 2, c);
        textures[i] = LoadTextureFromImage(img);
        UnloadImage(img);
    }

    /* warmup: let the driver settle */
    for (int i = 0; i < 60; ++i) {
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
    }

    double ms_A = bench_scenario(textures, 0);
    double ms_B = bench_scenario(textures, 1);
    double ms_C = bench_scenario(textures, 2);

    printf("=== raylib worst-case bench ===\n");
    printf("A. 1000 sprites x 1000 textures: %.4f ms\n", ms_A);
    printf("B. 10000 sprites x 1 texture:    %.4f ms\n", ms_B);
    printf("C. 1000 sprites x mixed blend:   %.4f ms\n", ms_C);
    printf("================================\n");

    for (int i = 0; i < MAX_TEXTURES; ++i)
        UnloadTexture(textures[i]);
    CloseWindow();
    return 0;
}
