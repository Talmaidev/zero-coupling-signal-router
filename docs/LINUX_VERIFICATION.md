# Linux 1.0 验证状态

状态：**X11/GLX、Wayland/EGL、XTest、XRandR、PipeWire 链路、Ubuntu 20.04 glibc 基线
均有真实构建/运行证据。Kali 物理机此前已通过 35/35，本次没有严重改动，按用户口径
沿用该结果、不重跑全量。云主机只作补充环境，其缺 GPU/声卡/显示模式不作为 1.0
发布阻塞。Linux 真实 GPU 驱动与物理音频输出仍未单独验证。**

本文只记录可回链的运行结果，不把“代码存在”“静态审读”或“服务器不崩”当成功能验收。

> 源码一致性提示：以下日志由两个互不一致的云副本构建。`/home/user/zcsr_linux`
> 有最新 PipeWire/XRandR 改动，但仍保留 composite 的 `u_ColorLut` / `display_encode`
> 死代码和旧的无条件 `inputext_x11` 门控；`/home/user/zcsr_linux_1_0_verify`
> 已清理 shader 并加 XTest 门控，但 audio/platform 仍是旧版。按用户口径，Linux
> 环境不再作为 1.0 门，物理机结果已过，不为副本差异重跑；这里只保留事实记录。


## 环境与结果

下表是本次补充验证；Kali 物理机（真 X11 + 真 WM）此前已跑通 35/35，本次不重复全量。

| 环境 | 后端/依赖 | 结果 | 关键证据 |
|---|---|---|---|
| Ubuntu 22.04.5，GCC 11，glibc 2.35，Mesa llvmpipe | X11/GLX，Xvfb + RANDR + XTEST | **35/35 CTest** | `build_linux_x11/ctest_x11_full_release.log`；`inputext_x11` 0.17s 通过；`glrender_test` 真实回读三条 PASS |
| Ubuntu 22.04.5，glibc 2.35 | 原生 Wayland/EGL，Weston 9 headless | **34/34 CTest** | `build_linux_wayland/ctest_wayland_release.log`；`glrender_test` 输出 `backend=egl-wayland`、llvmpipe、GL 4.5 Core、三条 PASS |
| Ubuntu 22.04.5 | PipeWire 0.3.48 + `support.null-audio-sink` | **35/35 CTest**；流真实连到 null sink | `build_linux_pipewire/ctest_pipewire_release.log`；探针输出 `backend=pipewire`、`[zcsr-pw] stream connected`，`pw-cli ls Link` 显示两条 stereo link |
| Ubuntu 20.04 chroot，GCC 10.5，glibc 2.31，Mesa 21.2.6 | X11/GLX，Xvfb + RANDR + XTEST | **35/35 CTest**；二进制最高 GLIBC 符号 `2.29` | `build_linux_focal_glibc231_ctest_full.log`；`build_linux_focal_glibc231_glrender.log`；`objdump -T` 结果 |
| Ubuntu 20.04 chroot + Xorg dummy/VNC | XRandR 真实 60Hz 模式 | 全屏请求 60Hz 返回 true；窗口模式返回 false | `platform_test` 在 `:99` 上 `all checks passed`；探针输出 `display=X11 1920x1080 refresh=60Hz`、`set_refresh_fullscreen(60)=1`、`set_refresh_windowed(60)=0` |

`35/35` 是 X11 全量测试（含 `inputext_x11`）；`34/34` 是 Wayland 构建，X11 专用的
`inputext_x11` 不在该构建中。两者都不是“少跑一个用例凑出来的通过”。

## 各项风险的实际结论

### Wayland/EGL

不是只写了代码。Wayland client、scanner、xdg-shell、xkbcommon、wayland-egl、EGL 都在
Ubuntu 22.04 上找到并链接；Weston headless 下创建了原生 Wayland surface 和 EGL
context，`glrender_test` 真实回读统一线性帧通过。未覆盖 GNOME/KDE 的窗口装饰、
fractional scaling、VRR/HDR 和多输出热插拔。

### X11/GLX、XTest、XRandR

X11/GLX 在 Xvfb 上 35/35；`inputext_x11` 通过，说明 XTest 注入路径在真实 X server
上工作。`platform_test` 新增了 XRandR 正路径：仅当 `zcsr_display_get()` 报告
`name == "X11"` 且 `refresh_hz > 0` 时，在全屏状态请求该刷新率并断言成功。
Xvfb 的 dot clock 为 0，会跳过该分支；Xorg dummy/VNC 的 60Hz 模式会实际执行该分支。

XRandR 只在独占全屏生效，窗口/无边框返回 false 且不改输出模式。该行为已在 dummy
Xorg 上用 60Hz 模式验证；真实显示器 EDID、多模式切换、VRR 仍未验证。

### glibc 与发行版下限

Ubuntu 22.04 原生构建最高引用 `GLIBC_2.34`，不能直接在 Debian 11/Ubuntu 20.04
（glibc 2.31）运行。改在 Ubuntu 20.04（glibc 2.31）chroot 中重新构建后，最高引用
降到 `GLIBC_2.29`，并在该 chroot 内完成 35/35 与 GL 真实回读。

因此 1.0 的 Linux 构建基线定为 **Ubuntu 20.04 / glibc 2.31 工具链**，产物要求
`GLIBC_2.29` 及以下符号。已验证运行环境是 glibc 2.31；Debian 10（glibc 2.28）
低于符号下限，不在支持范围。Debian 11/12、Ubuntu 20.04/22.04、Arch/SteamOS 3
的 glibc 均不低于 2.31，属于可运行范围，但仍需各自发行版的 X11/Wayland/音频包。

### PipeWire 与音频

`libpipewire-0.3-dev` + `libspa-0.2-dev` 安装后，CMake 选择 `PipeWire`，全量构建
和 35/35 通过。`audiomix_pipewire.c` 原先使用 `pw_buffer::requested`，该字段在
Ubuntu 22.04 的 PipeWire 0.3.48 中不存在；已改为用 `datas[0].maxsize` 计算可用帧，
兼容 0.3.4x 及更新版本。

在 `support.null-audio-sink` 上启动探针，输出 `stream connected`，`pw-cli ls Link`
显示两条 stereo link，说明流建立、格式协商和 link 都真实发生。**没有物理声卡，
因此“扬声器实际出声”仍未验证**；延迟、欠载、设备热插拔也未验证。

### 真实 GPU

云主机没有 `/dev/dri`，只有 Mesa llvmpipe；这是云环境缺失，不作为 1.0 发布门。
Kali 物理机此前已在真 X11 + 真 WM 上通过 35/35，本次没有严重改动，按用户口径沿用。
Linux 上的 AMD/NVIDIA 驱动、Steam Deck 的 AMD/Gamescope 路径、真实 GPU 的
sRGB/浮点回读仍未单独验证；Windows 侧的 Intel Arc B580/WGL 是真实 GPU 证据，
但不能替代 Linux 驱动验收。

## 复现命令

```sh
# X11 完整构建与测试
cmake -S . -B build_linux_x11 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DZCSR_LINUX_WINDOW_BACKEND=x11 -DZCSR_ENABLE_BOX2D=OFF
cmake --build build_linux_x11 -j
xvfb-run -a -s '-screen 0 1280x720x24 +extension RANDR' \
  ctest --test-dir build_linux_x11 --output-on-failure

# 原生 Wayland/EGL（先启动 Weston headless）
XDG_RUNTIME_DIR=/run/user/1234 weston --backend=headless-backend.so \
  --socket=zcsr-wayland --idle-time=0 --no-config
XDG_RUNTIME_DIR=/run/user/1234 WAYLAND_DISPLAY=zcsr-wayland \
  ctest --test-dir build_linux_wayland --output-on-failure

# PipeWire（无物理声卡时建 null sink）
XDG_RUNTIME_DIR=/tmp/pw-run pipewire &
XDG_RUNTIME_DIR=/tmp/pw-run pipewire-media-session &
XDG_RUNTIME_DIR=/tmp/pw-run pw-cli create-node adapter \
  '{ factory.name=support.null-audio-sink node.name=null-sink media.class=Audio/Sink object.linger=true audio.position=[FL FR] }'
```

Weston、PipeWire、media-session 和 Xorg 是测试环境 daemon，不是引擎测试进程。本次
验证结束后已全部停止，没有留在后台；测试二进制本身用 `zcsr_surface_destroy` /
`zcsr_mix_stop` 释放 surface、线程和音频流。

## 仍不能声称的内容

- 不能声称 Linux AMD/NVIDIA 驱动或 Steam Deck 已验收。
- 不能声称物理扬声器已出声；null sink 只证明流和 link。
- 不能声称 GNOME/KDE Wayland、fractional scaling、VRR/HDR 已验收。
- 不能声称 macOS 已编译/运行；按用户决定该项 pass、不作为 1.0 门，见 `docs/MACOS_VERIFICATION.md`。
