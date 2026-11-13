#include <stdio.h>
#include <string.h>
#include "zcsr/assets.h"

int main(void) {
    char out[ZCSR_ASSET_PATH_CAP];
    if (!zcsr_assets_set_base_dir("/base/dir")) return 1;
    if (!zcsr_assets_resolve("tex/hero.png", out, (int)sizeof(out))) return 2;
    if (strcmp(out, "/base/dir/tex/hero.png") != 0) {
        printf("FAIL relative: %s\n", out);
        return 3;
    }
    if (!zcsr_assets_resolve("/abs/x.png", out, (int)sizeof(out))) return 4;
    if (strcmp(out, "/abs/x.png") != 0) {
        printf("FAIL absolute: %s\n", out);
        return 5;
    }
    if (zcsr_assets_resolve("x", out, 3)) return 6;   /* too small for base+rel */
    if (!zcsr_assets_set_base_dir(NULL)) return 7;
    if (!zcsr_assets_resolve("rel.wav", out, (int)sizeof(out))) return 8;
    printf("assets path ok: %s\n", out);
    return 0;
}
