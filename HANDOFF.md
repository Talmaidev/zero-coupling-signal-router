# zcsr 零耦合信号路由器 — 项目移交手册
生成：2026-10-20
分支：claude/image-agent4-tests（78 commits 未 push，禁止 push）

================================================================================
一、总进度条
================================================================================

引擎核心功能 ████████████████████ 100%
  ├── platform（窗口/输入/显示器/DPI）.... ✓ A1+A2 完成
  ├── glrender（纹理/精灵/实例化/文字）.. ✓ 1C.0-1C.3 完成
  ├── audiomix（WASAPI+waveOut+AAudio）.. ✓ 1D+1E 完成
  ├── input / inputext .................. ✓ 完成
  ├── hsm（层次状态机）.................. ✓ 完成
  ├── signal（零耦合信号路由）........... ✓ 完成
  ├── culling（空间剔除）................ ✓ 完成
  └── core（日志/状态缓冲）.............. ✓ 完成

性能优化 ███████████████████░░ 90%
  ├── 排序优化（radix ping-pong）........ ✓ O1 完成
  ├── instanced 批次状态外提 ............ ✓ O2 完成
  ├── VBO 上传策略实验 .................. ✓ O3 完成
  ├── 上下文开销测量 .................... ✓ O4 完成
  ├── audiomix atomic_load 优化 ......... ✓ 8192→16
  ├── audiomix Q8 定点音量 .............. ✓ 已落地+精度验证保留
  ├── glrender 细粒度剖析 ............... ✓ flush=98% GL API
  ├── culling/atlas/input/hsm 基线 ...... ✓ 均无瓶颈
  └── 文字批处理优化 .................... ✓ 整串1 draw call

游戏/Demo ████████████████░░░░ 85%
  ├── 2048 游戏 ......................... ✓ 可玩（bfee234 修复输入）
  ├── Box2D 物理 demo ................... ✓ 碰撞→信号→音频（15af4e0 修复输入）
  ├── 打砖块设计文档 .................... ✓ 设计完成，未实现
  ├── 窗口功能测试 demo ................. ✓ zcsr_window_demo（7e9df23）
  └── 通用 menu UI 骨架 ................. ✓ menu_ui.h/c + demo（170cbb7）

跨平台验证 ████████████████████ 100%
  ├── Windows ctest ..................... ✓ 19/19
  ├── 耦合守卫 .......................... ✓ PASS
  ├── Android NDK 编译 .................. ✓ 通过
  ├── Android 真机（Redmi K60 Pro）...... ✓ GLES 3.2 + 文字 PASS
  └── waveOut 回退（FxSound 环境）....... ✓ 已解决

整体进度：约 93%（核心引擎+窗口测试+menu UI 完成，剩字体小字号验证和打砖块实现）

================================================================================
二、已知问题 / 未完成项（新对话必须处理）
================================================================================

【P0 - 已知 bug】
1. ~~demo_physics.c 有同样的 Windows input pump 顺序 bug~~ ✓ 已修复（commit 15af4e0）
   - 修复：调换顺序，input_pump 先于 surface_pump_events
   - 参考：demo_2048.c 已修复（commit bfee234）

2. ~~字体 "BEST" 在小字号下显示异常（疑似 "DESI"）~~ ✓ 已修复（commit b88b089，待目视确认）
   - 根因：scale<1.0 时浮点像素位置导致 GL_NEAREST 采样相位不一致
   - 修复：仅对 scale<1.0 启用整数像素对齐（round x/y/glyph_size/advance/line_advance）
   - scale>=1.0 保持原有行为；字体数据/UV/行序/位序均未改动
   - text_demo 已添加 scale=0.8/0.6/0.5 测试行
   - 注意：scale=0.5 为 4x4 像素，信息损失是预期的，不保证所有字形可辨

【P1 - 已完成】
3. ~~窗口功能测试 demo~~ ✓ 已完成（commit 7e9df23，target: zcsr_window_demo）
   - 覆盖：set_mode / set_client_size / display_count+get / dpi_scale / move_to_display / size_limits / dpi_callback / focus_callback
   - 键盘：1/2/3 模式，+/- 缩放，Tab 切显示器，L 限制，R 重置，ESC 退出

4. ~~通用 menu UI 骨架~~ ✓ 已完成（commit 170cbb7，integration/menu_ui.h/c）
   - 基于 glrender + input，零堆分配，调用者提供缓冲区
   - 支持：键盘导航（Up/Down/W/S）、Enter/Space 确认、ESC 取消、禁用项、高亮背景、自定义颜色
   - demo: zcsr_menu_demo（主菜单→选项子菜单，含切换项和禁用项）

【P2 - 可选】
5. 打砖块游戏实现（设计文档 docs/BREAKOUT_DESIGN.md 已就绪，可复用 menu_ui）
6. 2048 AI 自动玩（expectimax，需 256MB 权重文件）
7. demo_physics.c 完整运行验收（Box2D 碰撞→信号→音频，需 ZCSR_ENABLE_BOX2D=ON）
8. 字体小字号修复目视确认（运行 zcsr_text_demo，观察 scale=0.8/0.6 行）

================================================================================
三、关键技术决策记录
================================================================================

1. 字体纹理垂直方向（commit 5bcbb02）
   - OpenGL 约定：data row 0 = texture bottom = v=0 = quad top edge
   - 修复：font[ch][gy] 直接存，不要 7-gy 翻转
   - 位序：LSB 在左（bits & (1<<gx)）

2. Windows 输入 pump 顺序（commit bfee234）
   - surface_pump_events 用 PeekMessageA(&msg, 0, PM_REMOVE) 移除所有消息
   - input_pump 用 PeekMessageA(&msg, hwnd, PM_REMOVE) 只取该窗口
   - 必须 input_pump 先运行，否则收不到按键

3. Q8 定点音量精度（commit fd49745 验证）
   - 最极端场景误差 2.54%，真实混合 0.82%
   - 人耳阈值 ~12%（1dB），不可闻，保留 Q8 优化

4. 性能结论
   - glrender CPU 时间 98% 在 GL API 调用，zcsr 自有代码 ~2%
   - culling/atlas/input/hsm 四模块合计 <2% 帧预算
   - 当前引擎 CPU 层面无明显瓶颈

5. 音频后端
   - WASAPI 在 FxSound 环境下 Activate 失败（E_NOINTERFACE）
   - 已实现 waveOut 回退（commit 39163b4 + d7b9ebc）
   - Android 用 AAudio（commit 1D 系列）

================================================================================
四、构建与验证命令速查
================================================================================

【Windows 编译】
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build && ninja <target>'

【Windows 测试】
cd build && foreach t in test_*.exe { & $t }  # 18/18 expected

【Android NDK 编译】
wsl -d Ubuntu-24.04 -- bash -c "cd /mnt/c/Users/chen/Documents/GitHub/zero-coupling-signal-router/build/android/out/arm64-v8a && make -j4"

【Android APK 打包】
wsl -d Ubuntu-24.04 -- bash /mnt/c/Users/chen/Documents/GitHub/zero-coupling-signal-router/build/android/pack.sh
# 输出：build/android/zcsr_phase1c.apk

【Android 安装+日志】
C:\Users\chen\Downloads\scrcpy-win64-v4.1\adb.exe -s 3d16ff0e install -r build\android\zcsr_phase1c.apk
C:\Users\chen\Downloads\scrcpy-win64-v4.1\adb.exe -s 3d16ff0e logcat -d -s zcsr:V

【耦合守卫】
wsl -d Ubuntu-24.04 -- bash /mnt/c/Users/chen/Documents/GitHub/zero-coupling-signal-router/tools/check_no_cross_includes.sh

================================================================================
五、关键文件位置
================================================================================

引擎核心：
  modules/platform/platform.c    — 窗口管理（A1/A2）
  modules/glrender/glrender_*.c  — 渲染（纹理/精灵/实例化/文字）
  modules/audiomix/audiomix.c    — 音频混音（Q8优化在此）
  modules/input/input.c          — 输入（Windows pump 在此）
  modules/hsm/hsm.c              — 层次状态机
  modules/signal/signal.c        — 信号路由

Demo/游戏：
  integration/demo_2048.c        — 2048 游戏（已修复输入）
  integration/demo_physics.c     — Box2D 物理 demo（已修复输入，需 ZCSR_ENABLE_BOX2D=ON）
  integration/demo_window.c      — 窗口功能测试 demo（target: zcsr_window_demo）
  integration/demo_menu.c        — menu UI 演示（target: zcsr_menu_demo）
  integration/menu_ui.h/c        — 通用菜单 UI 组件（可复用，零堆分配）
  integration/game_core.c/h      — 2048 核心逻辑（位板+预计算表）
  integration/verify_windows.c   — Windows 后端验证

文档：
  docs/BREAKOUT_DESIGN.md        — 打砖块设计 v1.0（未实现）
  docs/BOX2D_INTEGRATION.md      — Box2D 集成文档
  AI_ONBOARDING.md               — AI 协作指南

构建：
  build/android/pack.sh          — APK 打包脚本
  android/keystore/zcsr-debug.keystore — 签名密钥（密码: android, alias: zcsr-debug）

================================================================================
六、新对话建议执行顺序
================================================================================

已完成（本次对话）：
  ✓ 第1步：修 demo_physics.c input pump bug（commit 15af4e0）
  ✓ 第2步：窗口功能测试 demo（commit 7e9df23，zcsr_window_demo）
  ✓ 第3步：通用 menu UI 骨架（commit 170cbb7，menu_ui.h/c + zcsr_menu_demo）

下一步可选：
  第4步：验证字体小字号 scale<1 渲染（P0 残留，运行 zcsr_text_demo 观察）
  第5步：实现打砖块游戏（docs/BREAKOUT_DESIGN.md 已就绪，可复用 menu_ui）
  第6步：2048 AI 自动玩（expectimax，需 256MB 权重文件）
  第7步：demo_physics 完整运行验收（需 ZCSR_ENABLE_BOX2D=ON 编译）

每步完成后：Windows ctest 19/19 + 耦合守卫 PASS + 独立 commit + 不 push

Git 路径（系统 PATH 无 git）：
  C:\Users\chen\AppData\Local\GitHubDesktop\app-3.6.4\resources\app\git\cmd\git.exe
