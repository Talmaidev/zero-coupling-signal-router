# macOS 1.0 验收状态

状态：**按用户 2026-11-23 决定，macOS 真机编译/运行验收从 1.0 门中移除（pass），
不再作为发布阻塞项。代码路径已完成静态审读，但没有真实 Apple 环境证据，不能
声称已在 Mac 上编译或运行。** 仅当将来拿到真实 Mac 或 macOS 云环境时，才用
`verify_macos_linear.sh` 复验。

## 已完成的静态工作

- `modules/platform/platform_cocoa.m`：NSWindow/NSApplication、事件泵、窗口模式、
  Retina backing 尺寸。
- `modules/glrender/glrender_cocoa.m`：`NSOpenGLProfileVersion4_1Core`、
  `NSOpenGLGetProcAddress` 交给 glad、`flushBuffer`、vsync。
- 统一线性改造是纯 core GL（FBO/VAO/texture/GLSL 330），不依赖 WGL/GLX 私有入口；
  Cocoa 后端只需要提供 current context。
- `glrender_cocoa.m` 的尺寸读取改用 `convertRectToBacking`，并在尺寸变化时
  `NSOpenGLContext update`，避免 Retina 上半分辨率/回读尺寸不一致。
- `platform_cocoa.m` 的 client size 在 AppKit point 与 client pixel 之间换算；
  `inputext_cocoa.m` 的鼠标坐标同步用 `convertPointToBacking`，保证 UI、GL viewport
  和点击坐标落在同一像素空间。

本机事实：Windows 工作机没有 `clang`/`xcrun`/`swift`/Apple SDK；Linux 云主机也没有
osxcross 或 Apple SDK。因此以上只是静态审读，**没有任何 macOS 编译产物**。

## 必须在 Mac 上验证的门

1. `cmake -S . -B build_macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0`
   配置成功，Cocoa/OpenGL 框架找到。
2. 全量构建成功，CTest 通过（至少 `glrender` 与 `light2d` 相关用例）。
3. `glrender_test` 在真实 NSOpenGL 窗口上输出：
   - `PASS: unified linear frame round-trip and linear blending`
   - `PASS: GL renderer smoke`
4. 若要在产品层验收，再运行 editor/runtime/PVZ 的 macOS 构建和 `--capture`，
   确认 `--capture` 的 BMP 尺寸等于窗口 backing 尺寸，且画面不是黑屏/半分辨率。

## 一键脚本

在有 GUI 会话的 Mac 上：

```bash
./tools/verify_macos_linear.sh
```

脚本把 configure/build/ctest/glrender 的原始输出写到
`build_macos/verify_macos_linear.log`；缺少两条 PASS 会返回非零。

## 当前不能声称的内容

- 不能声称 macOS 已编译、已运行、已通过真实回读。
- 不能把 Windows/Linux 的 golden 数值直接当作 macOS 的验收证据；macOS 驱动/颜色管理
  仍需独立回读。
- 不能在没有 Mac 的情况下用静态审读替代 `verify_macos_linear.sh` 的 PASS 行。
