# macOS backend contract

## Scope

本文定义 macOS Cocoa backend 的补偿层：`platform_cocoa.m`、`glrender_cocoa.m`、`inputext_cocoa.m` 如何把 AppKit/NSOpenGL 接到平台无关的 engine core。它是实现契约与设计图纸。

Core GL renderer 的 program、VAO/VBO、texture pool、batching、linear frame 全部留在平台无关模块。Cocoa 只提供 window、NSOpenGLContext、backing scale、event pump、swap 与平台 capability。Cocoa 不复制第二套 renderer，不为 renderer 另建 window。

## Architecture compensation

| layer | platform-neutral contract | Cocoa compensation |
|---|---|---|
| surface | `zcsr_surface` lifecycle、client pixel size | `NSWindow` + `NSView`，point/pixel 换算 |
| GL context | `zcsr_gl_renderer`、GL 3.3 Core 子集 | `NSOpenGLContext` 4.1 Core profile、glad loader |
| pixel space | viewport、FBO、readback 使用 client pixels | `convertRectToBacking` 得到 backing pixels |
| present | tier selection、LUT encode | `flushBuffer`、swap interval |
| input | `zcsr_input_ext` 状态机 | `NSEvent` monitor、`convertPointToBacking` |
| display | `zcsr_display_info` 枚举 | `NSScreen`、左上原点 y 换算 |
| capability | `ZCSR_PLATFORM_CAP_*` | 未实现项返回 false |

补偿边界固定：Cocoa 不进入 draw entry 的 layout、sort、cull、select、capacity、tier 决策，也不改变 core 的 texture/attribute/VAO 布局。

## Ownership

| object | owner | lifetime |
|---|---|---|
| `NSWindow` | `zcsr_surface` | `zcsr_cocoa_window_create` 以 owning reference 返回；`zcsr_cocoa_window_destroy` 释放 |
| `NSOpenGLContext` | `zcsr_gl_renderer::ctx` | `zcsr_gl_platform_init` 以 `__bridge_retained` 存入 `void*`；`zcsr_gl_platform_destroy` 以 `__bridge_transfer` 释放 |
| `NSView` | `NSWindow` 的 `contentView` | 不单独持有；context 通过 `setView:` 绑定 |
| renderer state | caller buffer | `zcsr_gl_create` 不分配 heap |
| GL function table | glad | `NSOpenGLGetProcAddress` 在 context current 后加载 |

## Frame path

```
runtime host
  zcsr_surface_pump_events
    └── zcsr_cocoa_window_pump
          ├── drain non-key NSEvents
          └── leave keyDown/keyUp for input module

  zcsr_gl_make_current
    └── NSOpenGLContext currentContext check
          ├── already current: return true
          └── otherwise makeCurrentContext

  draw entries
    └── platform-independent glrender

  zcsr_gl_present
    ├── linear tier: one LUT encode into display-encoded RGBA8
    └── zcsr_gl_swap -> [NSOpenGLContext flushBuffer]
```

## Coordinate contract

Public surface size 是 client pixels；AppKit content rect 是 points。所有换算必须经过 window 的 `backingScaleFactor`。

| point | conversion | contract |
|---|---|---|
| window creation | `w / scale`, `h / scale` | client pixel 请求转换为 AppKit point |
| mixed-DPI landing | `[window backingScaleFactor]` re-fit | window 落到不同 scale 的 display 时保持 client pixel 精确 |
| surface client size | `zcsr_cocoa_window_get_content_size` × scale | 返回 client pixels |
| surface set size | `w / scale`, `h / scale` | fullscreen 下返回 false |
| surface size limits | `min/max / scale` | min/max 以 client pixels 定义 |
| GL drawable | `[view convertRectToBacking:bounds]` | viewport、FBO、readback 使用 backing pixels |
| mouse | `[view convertPointToBacking:p]` | `y = backingHeight - backing.y`；UI、viewport、click 共用同一 pixel space |
| capture | `zcsr_gl_read_rgba` | 输出尺寸必须等于 backing size |

`zcsr_gl_update_size` 在 backing size 变化时写入 `r->width` / `r->height`，并调用 `[NSOpenGLContext update]`。`convertRectToBacking` 不可替换为 `bounds`：Retina 下会得到半分辨率 drawable，并使 `glReadPixels` 与 viewport 不一致。

## GL context contract

```objc
NSOpenGLPixelFormatAttribute attrs[] = {
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
    NSOpenGLPFAColorSize, 32,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAAccelerated,
    0
};
```

macOS 只暴露 3.2 Core 与 4.1 Core profile；backend 请求 4.1 Core，engine 使用 GL 3.3 Core 子集。`NSOpenGLGetProcAddress` 作为 glad loader；加载失败时 `zcsr_gl_platform_init` 返回 false，context 解绑并释放。

Context current 是 backend 内部缓存的 GL state。`zcsr_gl_make_current` 先比较 `[NSOpenGLContext currentContext]` 与 renderer 的 context；相同则直接返回，不执行 `makeCurrentContext`。每 draw 重复 `makeCurrentContext` 会产生 driver round-trip，不属于 draw entry 的职责。

Swap 与 vsync：

| function | behavior |
|---|---|
| `zcsr_gl_swap` | `[ctx flushBuffer]` |
| `zcsr_gl_platform_set_vsync` | `NSOpenGLCPSwapInterval = on ? 1 : 0` |

## Event and window contract

`zcsr_cocoa_window_pump` 只 drain 非 key 事件：

```objc
NSEventMaskAny & ~(NSEventMaskKeyDown | NSEventMaskKeyUp)
```

Key down/up 留在队列中，由 `zcsr_cocoa_input_pump` 消费。若 surface pump 先吞掉 key events，input module 永远看不到键盘。

Window 设计：

- `NSWindowStyleMaskBorderless`
- `NSBackingStoreBuffered`
- `releasedWhenClosed = NO`；lifetime 由 `zcsr_cocoa_window_destroy` 管理
- `opaque = NO`；clear background
- `movableByWindowBackground = YES`
- `level = NSStatusWindowLevel`
- fullscreen 通过 `toggleFullScreen:`；fullscreen 下 `set_client_size` 返回 false

Focus observer 把 `NSWindowDidBecomeKeyNotification` / `NSWindowDidResignKeyNotification` 转为 `ZCSR_FOCUS_GAINED` / `ZCSR_FOCUS_LOST`。Close delegate 设置 `forceClose`，由 `zcsr_platform_cocoa_take_close` 消费。

## Capability contract

Cocoa backend 对未实现能力返回 false，不伪造 capability：

| API | Cocoa result | reason |
|---|---|---|
| `zcsr_surface_set_drag_region` | false | `movableByWindowBackground` 不是 region 语义 |
| `zcsr_surface_set_cursor_bitmap` | false | Cocoa backend 不提供 custom RGBA cursor |
| `zcsr_surface_reset_cursor` | false | standard cursor 由 `set_cursor_shape` 管理 |

Standard cursor shapes 支持 `ARROW`、`IBEAM`、`CROSSHAIR`、`HAND`、`HIDDEN`；`set_cursor_visible` 直接调用 `NSCursor hide/unhide`。

## Public surface API

| signature | behavior |
|---|---|
| `zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds)` | 创建 borderless overlay surface；失败返回 NULL |
| `zcsr_surface* zcsr_surface_create_opaque(const char* title, zcsr_rect bounds)` | 创建 surface 并切换到 opaque `ZCSR_WINDOW_WINDOWED` |
| `void zcsr_surface_destroy(zcsr_surface*)` | 销毁 window，释放 surface slot |
| `void* zcsr_surface_native_handle(const zcsr_surface*)` | 返回 `NSWindow*` |
| `void* zcsr_surface_native_display(const zcsr_surface*)` | Cocoa 返回 NULL；`NSWindow*` 已包含 display 信息 |
| `void zcsr_surface_pump_events(zcsr_surface*)` | 调用 `zcsr_cocoa_window_pump` |
| `bool zcsr_surface_attach_native(zcsr_surface*, void* native_window)` | Cocoa 返回 false；Android lifecycle binding 不属于 Cocoa |
| `void zcsr_surface_detach_native(zcsr_surface*)` | Cocoa no-op |
| `bool zcsr_surface_set_mode(zcsr_surface*, zcsr_window_mode)` | `WINDOWED` / `BORDERLESS` / `FULLSCREEN`；fullscreen 读回 AppKit size |
| `bool zcsr_surface_set_client_size(zcsr_surface*, int w, int h)` | fullscreen 返回 false；其余按 backing scale 设置 |
| `bool zcsr_surface_set_drag_region(zcsr_surface*, int x, int y, int w, int h)` | Cocoa 返回 false |
| `bool zcsr_surface_move_to_display(zcsr_surface*, int display_id)` | 移动到指定 screen index |
| `void zcsr_surface_get_client_size(const zcsr_surface*, int* w, int* h)` | 返回 client pixels |
| `float zcsr_surface_get_dpi_scale(const zcsr_surface*)` | 返回 backing scale；无 window 返回 1.0 |
| `bool zcsr_surface_set_cursor_shape(zcsr_surface*, zcsr_cursor_shape)` | standard NSCursor |
| `bool zcsr_surface_set_cursor_visible(zcsr_surface*, bool)` | `NSCursor hide/unhide` |
| `bool zcsr_surface_set_cursor_bitmap(zcsr_surface*, const unsigned char*, int, int, int, int)` | Cocoa 返回 false |
| `bool zcsr_surface_reset_cursor(zcsr_surface*)` | Cocoa 返回 false |

### Compensation helpers

| signature | visibility | behavior |
|---|---|---|
| `void zcsr_surface_apply_size_limits(zcsr_surface*)` | internal | min/max client pixels 转为 AppKit points |
| `bool zcsr_platform_cocoa_take_close(zcsr_surface*)` | internal | 消费 close delegate 的 close 标志 |

## Cocoa backend API

| signature | behavior |
|---|---|
| `void* zcsr_cocoa_window_create(const char* title, int x, int y, int w, int h)` | 创建 borderless NSWindow；返回 owning reference |
| `void zcsr_cocoa_window_destroy(void* ns_window)` | delegate detach、close、release |
| `void zcsr_cocoa_window_pump(void* ns_window)` | drain non-key NSEvents |
| `int zcsr_cocoa_window_set_mode(void* ns_window, int mode)` | 0 windowed / 1 borderless / 2 fullscreen |
| `int zcsr_cocoa_window_set_content_size(void* ns_window, int w, int h)` | AppKit point content size；fullscreen 返回 0 |
| `void zcsr_cocoa_window_get_content_size(void* ns_window, int* w, int* h)` | content size × backing scale，输出 client pixels |
| `float zcsr_cocoa_window_backing_scale(void* ns_window)` | `backingScaleFactor`；无效返回 1.0 |
| `int zcsr_cocoa_window_move_to_screen(void* ns_window, int index)` | 居中到 `NSScreen` index；失败返回 0 |
| `int zcsr_cocoa_screen_count(void)` | `[NSScreen screens] count` |
| `int zcsr_cocoa_screen_info(int index, int* out_id, char* out_name, int name_cap, int* out_x, int* out_y, int* out_w, int* out_h, int* out_refresh, float* out_scale, int* out_primary)` | screen 枚举；`out_id = index`；`out_y` 转为左上原点；成功返回非 0 |

## GL backend hooks

| signature | behavior |
|---|---|
| `bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface)` | 创建 NSOpenGLContext、绑定 contentView、加载 glad、更新 size |
| `void zcsr_gl_platform_destroy(zcsr_gl_renderer* r)` | 解绑 view、clear current、释放 context |
| `bool zcsr_gl_make_current(zcsr_gl_renderer* r)` | current context 相同则跳过，否则 `makeCurrentContext` |
| `void zcsr_gl_update_size(zcsr_gl_renderer* r)` | `convertRectToBacking` 更新 drawable size；变化时 `[ctx update]` |
| `void zcsr_gl_swap(zcsr_gl_renderer* r)` | `flushBuffer` |
| `void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on)` | swap interval 0/1 |

## Linear workspace contract

Cocoa backend 不实现第二套 color pipeline。Core 的 `RGBA16F` linear frame、shader、LUT encode 保持平台无关：

- `linear` tier：draw 写入 linear frame；`zcsr_gl_present` / `zcsr_gl_read_rgba` 执行唯一一次 encode。
- `direct` tier：draw 写 default framebuffer；Cocoa 不追加 encode。
- Cocoa context 只提供 drawable 和 current state，不参与 layout、sort、texture 算法选择。
- `verify_macos_linear.sh` 的 linear frame gate 读取真实 backbuffer，验证 round-trip 与 blend。

## Verification gates

macOS build 的验收条件：

1. configure 成功，build tree 必须在 source tree 之外；`verify_macos_linear.sh` 拒绝 in-tree `BUILD_DIR`：

```bash
cmake -S . -B ../zcsr-build-macos/verify_linear \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
```

Cocoa、OpenGL framework 找到。

2. build 与 CTest 通过，至少覆盖 `glrender`、`light2d`。

3. `glrender_test` 在 GUI session 的真实 NSOpenGL window 输出：

```text
PASS: unified linear frame round-trip and linear blending
PASS: GL renderer smoke
```

4. 产品层 `--capture` 的 BMP 尺寸等于 window backing size；画面不是黑屏、不是半分辨率。

5. `verify_macos_linear.sh` 产出 `${ZCSR_MACOS_BUILD_DIR}/verify_macos_linear.log`；`ZCSR_MACOS_BUILD_DIR` 默认 `../zcsr-build-macos/verify_linear`；缺少任一 PASS 返回非零。

## Failure contract

| condition | result |
|---|---|
| `NSWindow` 或 `contentView` 缺失 | `zcsr_gl_platform_init` false |
| `NSOpenGLPixelFormat` 创建失败 | false |
| `NSOpenGLContext` 创建失败 | false |
| glad loader 失败 | false，context 解绑 |
| fullscreen 下 `set_client_size` | false |
| `set_drag_region` | false |
| custom cursor bitmap | false |
| invalid screen index | false / 0 |
| `backingScaleFactor <= 0` | 使用 1.0 |
