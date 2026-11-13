#include <stdio.h>
#include "zcsr/audio_mix.h"

int main(void) {
    zcsr_mix_device_info devs[16];
    int n = zcsr_mix_list_output_devices(devs, 16);
    printf("output devices: %d\n", n);
    for (int i = 0; i < n; ++i) {
        printf("  [%d]%s %s\n", i, devs[i].is_default ? "*" : "", devs[i].name);
    }
    if (zcsr_mix_list_output_devices(devs, 0) != 0) {
        printf("FAIL: max=0 must return 0\n");
        return 1;
    }
    return 0;
}
