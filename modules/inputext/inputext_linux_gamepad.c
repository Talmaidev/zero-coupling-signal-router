/* inputext_linux_gamepad.c — real Linux evdev gamepad backend.
 *
 * Shared by the X11 and Wayland builds. The platform window backend only owns
 * surfaces and event dispatch; gamepad is a separate /dev/input/event*
 * producer, exactly like XInput on Windows and the Android joystick source.
 *
 * Zero zcsr heap: the fd/path/range tables live in zcsr_input_ext. libc's
 * opendir/readdir allocations are outside the engine's zero-heap rule.
 */
#if defined(__linux__) && !defined(__ANDROID__)

#include "inputext_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum {
    ZCSR_LPAD_LX = 0,
    ZCSR_LPAD_LY,
    ZCSR_LPAD_RX,
    ZCSR_LPAD_RY,
    ZCSR_LPAD_LT,
    ZCSR_LPAD_RT,
    ZCSR_LPAD_AXIS_COUNT
};

static const int zcsr_linux_axis_code[ZCSR_LPAD_AXIS_COUNT] = {
    ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ
};

static bool zcsr_linux_bit(const unsigned char* bits, int bit) {
    return (bits[bit / 8] & (unsigned char)(1u << (bit % 8))) != 0u;
}

static void zcsr_linux_pad_clear(zcsr_pad_state* pad) {
    if (pad) *pad = (zcsr_pad_state){ 0 };
}

static void zcsr_linux_close_slot(zcsr_input_ext* in, int slot) {
    if (!in || slot < 0 || slot >= ZCSR_PAD_MAX) return;
    if (in->gamepad_fd[slot] >= 0) {
        close(in->gamepad_fd[slot]);
        in->gamepad_fd[slot] = -1;
    }
    in->gamepad_path[slot][0] = '\0';
    zcsr_linux_pad_clear(&in->pads[slot]);
}

static bool zcsr_linux_path_is_open(const zcsr_input_ext* in, const char* path) {
    int i;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        if (in->gamepad_fd[i] >= 0 && strcmp(in->gamepad_path[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static void zcsr_linux_abs_range(int fd, int code, int* out_min, int* out_max) {
    struct input_absinfo info;
    int fallback_min = 0;
    int fallback_max = 255;

    if (code == ABS_X || code == ABS_Y || code == ABS_RX || code == ABS_RY) {
        fallback_min = -32768;
        fallback_max = 32767;
    }
    if (ioctl(fd, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum) {
        *out_min = info.minimum;
        *out_max = info.maximum;
    } else {
        *out_min = fallback_min;
        *out_max = fallback_max;
    }
}

static bool zcsr_linux_open_slot(zcsr_input_ext* in, int slot, const char* path) {
    unsigned char key_bits[(KEY_MAX + 1) / 8];
    unsigned char abs_bits[(ABS_MAX + 1) / 8];
    struct input_absinfo info;
    int fd;
    int axis;

    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;

    memset(key_bits, 0, sizeof key_bits);
    memset(abs_bits, 0, sizeof abs_bits);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key_bits), key_bits) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_bits), abs_bits) < 0) {
        close(fd);
        return false;
    }

    /* A gamepad has at least a face button and a stick. This rejects
     * keyboards, mice, touchpads and power buttons that share /dev/input. */
    if (!zcsr_linux_bit(key_bits, BTN_SOUTH) &&
        !zcsr_linux_bit(key_bits, BTN_GAMEPAD) &&
        !zcsr_linux_bit(key_bits, BTN_A)) {
        close(fd);
        return false;
    }
    if (!zcsr_linux_bit(abs_bits, ABS_X) || !zcsr_linux_bit(abs_bits, ABS_Y)) {
        close(fd);
        return false;
    }

    in->gamepad_fd[slot] = fd;
    {
        size_t path_len = strlen(path);
        if (path_len >= sizeof(in->gamepad_path[slot])) {
            close(fd);
            in->gamepad_fd[slot] = -1;
            return false;
        }
        memcpy(in->gamepad_path[slot], path, path_len + 1u);
    }

    for (axis = 0; axis < ZCSR_LPAD_AXIS_COUNT; ++axis) {
        int code = zcsr_linux_axis_code[axis];
        if (axis == ZCSR_LPAD_LT && !zcsr_linux_bit(abs_bits, ABS_Z)) {
            code = zcsr_linux_bit(abs_bits, ABS_GAS) ? ABS_GAS : ABS_Z;
        } else if (axis == ZCSR_LPAD_RT && !zcsr_linux_bit(abs_bits, ABS_RZ)) {
            code = zcsr_linux_bit(abs_bits, ABS_BRAKE) ? ABS_BRAKE : ABS_RZ;
        }
        if (ioctl(fd, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum) {
            in->gamepad_abs_min[slot][axis] = info.minimum;
            in->gamepad_abs_max[slot][axis] = info.maximum;
        } else {
            zcsr_linux_abs_range(fd, code, &in->gamepad_abs_min[slot][axis],
                                &in->gamepad_abs_max[slot][axis]);
        }
    }

    zcsr_linux_pad_clear(&in->pads[slot]);
    in->pads[slot].connected = true;
    return true;
}

void zcsr_scan_gamepads(zcsr_input_ext* in) {
    DIR* dir;
    struct dirent* entry;
    int slot;

    if (!in) return;
    if (!in->gamepad_init) {
        for (slot = 0; slot < ZCSR_PAD_MAX; ++slot) {
            in->gamepad_fd[slot] = -1;
            in->gamepad_path[slot][0] = '\0';
            zcsr_linux_pad_clear(&in->pads[slot]);
        }
        in->gamepad_init = true;
    }

    for (slot = 0; slot < ZCSR_PAD_MAX; ++slot) {
        if (in->gamepad_fd[slot] >= 0) continue;
        break;
    }
    if (slot == ZCSR_PAD_MAX) return;

    dir = opendir("/dev/input");
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        char path[32];
        int free_slot = -1;
        int i;

        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        if (strlen(entry->d_name) >= 18u) continue; /* eventNNNN fits this tree */
        snprintf(path, sizeof path, "/dev/input/%s", entry->d_name);
        if (zcsr_linux_path_is_open(in, path)) continue;

        for (i = 0; i < ZCSR_PAD_MAX; ++i) {
            if (in->gamepad_fd[i] < 0) { free_slot = i; break; }
        }
        if (free_slot < 0) break;
        if (zcsr_linux_open_slot(in, free_slot, path)) {
            /* Continue: one directory scan can fill both slots. */
        }
    }
    closedir(dir);
}

void zcsr_close_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        zcsr_linux_close_slot(in, i);
        in->gamepad_abs_min[i][0] = 0;
        in->gamepad_abs_max[i][0] = 0;
    }
    in->gamepad_init = false;
    in->gamepad_scan_tick = 0;
}

static void zcsr_linux_set_button(zcsr_pad_state* pad, zcsr_pad_button button, int down) {
    uint16_t bit;
    if (!pad || (unsigned)button >= (unsigned)ZCSR_PAD_BUTTON_COUNT) return;
    bit = (uint16_t)(1u << (unsigned)button);
    if (down) pad->buttons = (uint16_t)(pad->buttons | bit);
    else pad->buttons = (uint16_t)(pad->buttons & (uint16_t)~bit);
}

static zcsr_pad_button zcsr_linux_key_button(int code) {
    switch (code) {
    case BTN_SOUTH:      return ZCSR_PAD_A;
    case BTN_EAST:       return ZCSR_PAD_B;
    case BTN_NORTH:      return ZCSR_PAD_X;
    case BTN_WEST:       return ZCSR_PAD_Y;
    case BTN_TL:         return ZCSR_PAD_LB;
    case BTN_TR:         return ZCSR_PAD_RB;
    case BTN_SELECT:     return ZCSR_PAD_BACK;
    case BTN_START:      return ZCSR_PAD_START;
    case BTN_THUMBL:     return ZCSR_PAD_LSTICK;
    case BTN_THUMBR:     return ZCSR_PAD_RSTICK;
    case BTN_DPAD_UP:    return ZCSR_PAD_DUP;
    case BTN_DPAD_DOWN:  return ZCSR_PAD_DDOWN;
    case BTN_DPAD_LEFT:  return ZCSR_PAD_DLEFT;
    case BTN_DPAD_RIGHT: return ZCSR_PAD_DRIGHT;
    default:             return ZCSR_PAD_BUTTON_COUNT;
    }
}

static float zcsr_linux_norm_stick(int value, int min, int max) {
    float mid;
    float half;
    float v;
    if (max <= min) return 0.0f;
    mid = (float)(min + max) * 0.5f;
    half = (float)(max - min) * 0.5f;
    if (half <= 0.0f) return 0.0f;
    v = ((float)value - mid) / half;
    if (v < 0.0f) v = -v;
    if (v < 0.15f) return 0.0f;
    if (v > 1.0f) v = 1.0f;
    return ((float)value - mid) < 0.0f ? -v : v;
}

static float zcsr_linux_norm_trigger(int value, int min, int max) {
    float v;
    if (max <= min) return 0.0f;
    v = ((float)value - (float)min) / (float)(max - min);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

static int zcsr_linux_axis_index(int code) {
    int i;
    for (i = 0; i < ZCSR_LPAD_AXIS_COUNT; ++i) {
        if (code == zcsr_linux_axis_code[i]) return i;
    }
    if (code == ABS_GAS) return ZCSR_LPAD_LT;
    if (code == ABS_BRAKE) return ZCSR_LPAD_RT;
    return -1;
}

static void zcsr_linux_handle_abs(zcsr_input_ext* in, int slot,
                                  int code, int value) {
    zcsr_pad_state* pad = &in->pads[slot];
    int axis = zcsr_linux_axis_index(code);

    if (code == ABS_HAT0X) {
        zcsr_linux_set_button(pad, ZCSR_PAD_DLEFT, value < 0);
        zcsr_linux_set_button(pad, ZCSR_PAD_DRIGHT, value > 0);
        return;
    }
    if (code == ABS_HAT0Y) {
        zcsr_linux_set_button(pad, ZCSR_PAD_DUP, value < 0);
        zcsr_linux_set_button(pad, ZCSR_PAD_DDOWN, value > 0);
        return;
    }
    if (axis < 0) return;

    if (axis == ZCSR_LPAD_LX) pad->lx = zcsr_linux_norm_stick(value,
        in->gamepad_abs_min[slot][axis], in->gamepad_abs_max[slot][axis]);
    else if (axis == ZCSR_LPAD_LY) pad->ly = -zcsr_linux_norm_stick(value,
        in->gamepad_abs_min[slot][axis], in->gamepad_abs_max[slot][axis]);
    else if (axis == ZCSR_LPAD_RX) pad->rx = zcsr_linux_norm_stick(value,
        in->gamepad_abs_min[slot][axis], in->gamepad_abs_max[slot][axis]);
    else if (axis == ZCSR_LPAD_RY) pad->ry = -zcsr_linux_norm_stick(value,
        in->gamepad_abs_min[slot][axis], in->gamepad_abs_max[slot][axis]);
    else if (axis == ZCSR_LPAD_LT) pad->lt = zcsr_linux_norm_trigger(value,
        in->gamepad_abs_min[slot][axis], in->gamepad_abs_max[slot][axis]);
    else if (axis == ZCSR_LPAD_RT) pad->rt = zcsr_linux_norm_trigger(value,
        in->gamepad_abs_min[slot][axis], in->gamepad_abs_max[slot][axis]);
}

void zcsr_pump_gamepads(zcsr_input_ext* in) {
    int slot;
    if (!in) return;
    if (!in->gamepad_init) zcsr_scan_gamepads(in);

    in->gamepad_scan_tick++;
    /* Hotplug is not an inotify dependency: rescan at 1 Hz at 60 fps. */
    if ((in->gamepad_scan_tick % 60) == 0) zcsr_scan_gamepads(in);

    for (slot = 0; slot < ZCSR_PAD_MAX; ++slot) {
        struct input_event events[16];
        int fd = in->gamepad_fd[slot];
        if (fd < 0) continue;

        for (;;) {
            ssize_t got = read(fd, events, sizeof events);
            ssize_t i;
            if (got < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == ENODEV || errno == EBADF) {
                    zcsr_linux_close_slot(in, slot);
                    break;
                }
                break;
            }
            if (got == 0) break;
            for (i = 0; i < got / (ssize_t)sizeof events[0]; ++i) {
                struct input_event* ev = &events[i];
                zcsr_pad_state* pad = &in->pads[slot];
                if (ev->type == EV_KEY) {
                    zcsr_pad_button button = zcsr_linux_key_button(ev->code);
                    if (button < ZCSR_PAD_BUTTON_COUNT) {
                        zcsr_linux_set_button(pad, button, ev->value != 0);
                    }
                } else if (ev->type == EV_ABS) {
                    zcsr_linux_handle_abs(in, slot, ev->code, ev->value);
                } else if (ev->type == EV_SYN && ev->code == SYN_DROPPED) {
                    /* Kernel dropped events: clear held state; the next
                     * EV_KEY/EV_ABS stream rebuilds it. */
                    uint16_t connected = pad->connected ? 1u : 0u;
                    zcsr_linux_pad_clear(pad);
                    pad->connected = connected != 0u;
                }
            }
        }
    }
}

#endif /* __linux__ && !__ANDROID__ */
