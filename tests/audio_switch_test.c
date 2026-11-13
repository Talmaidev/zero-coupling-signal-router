#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "zcsr/audio_mix.h"

int main(void) {
    zcsr_mix_device_info devs[4];
    zcsr_mixer* m;
    int n, i;
    n = zcsr_mix_list_output_devices(devs, 4);
    printf("devices=%d\n", n);
    if (n < 2) {
        printf("SKIP: need >=2 endpoints to verify a switch (session has %d)\n", n);
        return 0;
    }
    m = zcsr_mix_start();
    if (!m) return 2;
    Sleep(1200);
    printf("initial request='%s'\n", zcsr_mix_get_output_device(m));

    if (!zcsr_mix_set_output_device(m, devs[1].id)) return 3;
    printf("requested switch -> '%s' (%s)\n", devs[1].name, zcsr_mix_get_output_device(m));
    Sleep(2000);

    zcsr_mix_set_output_device(m, "");
    printf("restored request='%s'\n", zcsr_mix_get_output_device(m));
    Sleep(1200);
    zcsr_mix_stop(m);
    return 0;
}
