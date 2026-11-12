#ifndef ZCSR_PLATFORM_COCOA_H
#define ZCSR_PLATFORM_COCOA_H
/* Thin C-ABI shim over the Cocoa (NSWindow) backend. Implemented in platform_cocoa.m.
 * Pure C signatures so platform.c stays C and no Objective-C leaks across the boundary.
 * Same-module header (no cross-module/relative include) — zero-coupling preserved. */

/* Create an NSWindow；返回 retained NSWindow* 作为 void*，失败返回 NULL。
 * 初始样式是 borderless + 透明 + 置顶（overlay 语义，与 Win32 的 BORDERLESS 默认一致）。
 * 要变成正常应用窗口，调用方必须再调 zcsr_cocoa_window_set_mode(WINDOWED)。 */
void* zcsr_cocoa_window_create(const char* title, int x, int y, int w, int h);
void  zcsr_cocoa_window_destroy(void* ns_window);
/* Drain pending Cocoa events for the app (non-blocking). */
void  zcsr_cocoa_window_pump(void* ns_window);

/* 窗口样式切换。mode 取值与 zcsr_window_mode 一致：
 *   0 = WINDOWED（带标题栏、可缩放、普通层级）
 *   1 = BORDERLESS（无边框、透明、置顶 overlay）
 *   2 = FULLSCREEN（原生 fullscreen，不动物理分辨率）
 * 返回 false 表示窗口句柄无效或 mode 越界。 */
int   zcsr_cocoa_window_set_mode(void* ns_window, int mode);

/* 设置客户区尺寸（point）。在 FULLSCREEN 下返回 false。 */
int   zcsr_cocoa_window_set_content_size(void* ns_window, int w, int h);

/* 读客户区尺寸（point）与 Retina backing scale。窗口无效时 *w/*h 不写。 */
void  zcsr_cocoa_window_get_content_size(void* ns_window, int* w, int* h);
float zcsr_cocoa_window_backing_scale(void* ns_window);

/* 把窗口居中移到第 index 个显示器（0 = 主屏）。失败返回 0。 */
int   zcsr_cocoa_window_move_to_screen(void* ns_window, int index);

/* 显示器枚举：count >= 1；index 越界或 out==NULL 返回 false。 */
int   zcsr_cocoa_screen_count(void);
int   zcsr_cocoa_screen_info(int index, int* out_id, char* out_name, int name_cap,
                             int* out_x, int* out_y, int* out_w, int* out_h,
                             int* out_refresh, float* out_scale, int* out_primary);

#endif /* ZCSR_PLATFORM_COCOA_H */
