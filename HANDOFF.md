# zcsr 零耦合信号路由器 — 项目移交手册
生成：2026-10-20
分支：claude/image-agent4-tests（82 commits 未 push，禁止 push）

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

游戏/Demo █████████████████░░ 90%
  ├── 2048 游戏 ......................... ✓ 可玩（bfee234 修复输入）
  ├── Box2D 物理 demo ................... ✓ 碰撞→信号→音频（15af4e0 修复输入）
  ├── 打砖块设计文档 .................... ✓ 设计完成，未实现
  ├── 窗口功能测试 demo ................. ✓ zcsr_window_demo（7e9df23）
  ├── 通用 menu UI 骨架 ................. ✓ menu_ui.h/c + demo（170cbb7）
  └── 游戏设置面板骨架 .................. ✓ settings_ui.h/c + demo（aa70332）

跨平台验证 ████████████████████ 100%
  ├── Windows ctest ..................... ✓ 19/19
  ├── 耦合守卫 .......................... ✓ PASS
  ├── Android NDK 编译 .................. ✓ 通过
  ├── Android 真机（Redmi K60 Pro）...... ✓ GLES 3.2 + 文字 PASS
  └── waveOut 回退（FxSound 环境）....... ✓ 已解决

整体进度：约 95%（核心引擎+窗口测试+menu UI+设置面板完成，剩字体小字号目视确认和打砖块实现）

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
三-B、踩坑记录（新对话必读，避免重复浪费时间）
================================================================================

1. Git 不在系统 PATH
   - 现象：git / ctest / cmake FetchContent 报"找不到命令"
   - 原因：系统未安装独立 Git，只有 GitHub Desktop 内置
   - 解决：$gitDir = "C:\Users\chen\AppData\Local\GitHubDesktop\app-3.6.4\resources\app\git\cmd"
          $env:PATH = "$gitDir;$env:PATH"
   - 注意：cmake -DZCSR_ENABLE_BOX2D=ON 配置时必须先设 PATH，否则 FetchContent clone 失败

2. ctest / ninja 不在 PowerShell 直接可用
   - 现象：直接运行 ctest 报"找不到命令"
   - 原因：需要 vcvars64.bat 设置 MSVC 环境
   - 解决：所有编译/测试命令必须用 cmd /c 'vcvars64.bat >nul && ...' 包裹

3. Windows input pump 顺序（最常见 bug）
   - 现象：窗口能打开但所有按键无响应
   - 原因：surface_pump_events 用 PeekMessageA(...,0,PM_REMOVE) 移除所有消息，
           input_pump 用 PeekMessageA(...,hwnd,PM_REMOVE) 只取该窗口，
           如果 surface_pump_events 先运行，input_pump 收不到任何按键
   - 解决：必须 input_pump 先于 surface_pump_events
   - 已修复：demo_2048.c (bfee234), demo_physics.c (15af4e0), demo_window.c, demo_menu.c, demo_settings.c
   - 新写任何带窗口+输入的 demo 都必须遵守此顺序

4. 2048 按空格没反应
   - 现象：菜单界面按 SPACE/ENTER 无法开始游戏
   - 排查：代码逻辑正确（初始状态 menu，SPACE 触发 hsm_dispatch start）
   - 可能原因：窗口未获得焦点 / 运行的是旧编译版本
   - 解决：确保点击窗口获得焦点后再按键；重新编译 zcsr_2048_demo

5. PowerShell 中 cmd /c 不能用 Select-String 等 cmdlet
   - 现象：cmd /c '... && ninja -t targets | Select-String demo' 报 Select-String 不是内部命令
   - 原因：管道在 cmd 上下文中执行，PowerShell cmdlet 不可用
   - 解决：先用 cmd /c 输出到变量，再用 PowerShell 的 Select-String 过滤

6. 不透明结构体外部不能访问成员
   - 现象：demo_settings.c 中 g_settings->items 报 "left of 'items' specifies undefined struct"
   - 原因：zcsr_settings 是不透明结构体，定义在 .c 中，外部只能通过 API 访问
   - 解决：设置项数组由调用者拥有（文件级静态变量），apply_settings 直接访问该数组，
           不从 settings 对象中获取

7. Box2D 默认关闭
   - 现象：ninja demo_physics 报 "unknown target"
   - 原因：ZCSR_ENABLE_BOX2D 默认 OFF，demo_physics target 不生成
   - 解决：cmake -DZCSR_ENABLE_BOX2D=ON .. 重新配置（需 git 在 PATH）
   - Box2D 源码已缓存于 build/_deps/box2d-src，不会重复下载

8. 字体小字号 scale<1 渲染
   - 现象："BEST" 显示成 "DESI" 等错误字符
   - 第一版修复（b88b089）：整数像素对齐 round(x/y/glyph_size/advance)
   - GPT 建议更优方案：CPU 点阵缩放 + white_tex 画像素块，避免 GPU 缩小采样
   - 状态：待落地 GPT 方案并目视验证

9. LF/CRLF 警告
   - 现象：git commit 时 warning: LF will be replaced by CRLF
   - 原因：Windows git 自动换行符转换
   - 影响：无害，可忽略

================================================================================
四、构建与验证命令速查
================================================================================

【环境前置】
- Git 不在系统 PATH，使用 GitHub Desktop 内置 git：
  $gitDir = "C:\Users\chen\AppData\Local\GitHubDesktop\app-3.6.4\resources\app\git\cmd"
  $env:PATH = "$gitDir;$env:PATH"
- cmake FetchContent（如 Box2D）需要 git 在 PATH，否则配置失败
- ctest / ninja 需要 vcvars64 环境，直接在 PowerShell 运行会报"找不到命令"

【Windows 编译（单 target）】
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build && ninja <target>'

【Windows 编译（全量）】
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build && ninja 2>&1'

【Windows ctest（必须 vcvars 环境）】
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build && ctest --output-on-failure 2>&1'
# 预期 19/19 通过

【重新配置 cmake（如新增 target 或开启 BOX2D）】
$gitDir = "C:\Users\chen\AppData\Local\GitHubDesktop\app-3.6.4\resources\app\git\cmd"
$env:PATH = "$gitDir;$env:PATH"
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\chen\Documents\GitHub\zero-coupling-signal-router\build && cmake -DZCSR_ENABLE_BOX2D=ON .. 2>&1'
# Box2D 已缓存于 build/_deps/，不会重复下载

【耦合守卫（WSL，避免 Windows 转义问题）】
wsl -d Ubuntu-24.04 -- bash /mnt/c/Users/chen/Documents/GitHub/zero-coupling-signal-router/tools/check_no_cross_includes.sh

【Android NDK 编译（WSL）】
wsl -d Ubuntu-24.04 -- bash -c "cd /mnt/c/Users/chen/Documents/GitHub/zero-coupling-signal-router/build/android/out/arm64-v8a && make -j4"

【Android APK 打包（WSL）】
wsl -d Ubuntu-24.04 -- bash /mnt/c/Users/chen/Documents/GitHub/zero-coupling-signal-router/build/android/pack.sh
# 输出：build/android/zcsr_phase1c.apk

【Android 安装+日志（Windows adb）】
C:\Users\chen\Downloads\scrcpy-win64-v4.1\adb.exe -s 3d16ff0e install -r build\android\zcsr_phase1c.apk
C:\Users\chen\Downloads\scrcpy-win64-v4.1\adb.exe -s 3d16ff0e logcat -d -s zcsr:V

【密码速查】
- WSL (Ubuntu-24.04) root 密码：root
- Android keystore 密码：android，alias：zcsr-debug

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
  integration/demo_settings.c    — 游戏设置面板演示（target: zcsr_settings_demo）
  integration/menu_ui.h/c        — 通用菜单 UI 组件（可复用，零堆分配）
  integration/settings_ui.h/c    — 通用设置面板组件（下拉/滑块/分隔线，Apply/Cancel/Reset）
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

已完成：
  ✓ demo_physics input pump bug（15af4e0）
  ✓ 窗口功能测试 demo（7e9df23，zcsr_window_demo）
  ✓ 通用 menu UI 骨架（170cbb7，menu_ui.h/c + zcsr_menu_demo）
  ✓ 游戏设置面板骨架（aa70332，settings_ui.h/c + zcsr_settings_demo）
  ✓ 字体小字号整数对齐修复（b88b089，待目视确认 / GPT 建议升级为 CPU 点阵缩放）

下一步（按优先级）：
  1. 落地 GPT 的 modern_settings 完整版（标签页+鼠标+真正下拉框）
     - 已发布 txt plan 给 GPT，等返回完整代码后落地
  2. 落地 GPT 的小字 CPU 点阵缩放方案（替换 b88b089 的整数对齐）
  3. 字体小字号目视确认（运行 zcsr_text_demo，观察 scale=0.8/0.6 行）
  4. 实现打砖块游戏（docs/BREAKOUT_DESIGN.md 已就绪，可复用 menu_ui + settings_ui）
  5. 2048 AI 自动玩（expectimax，需 256MB 权重文件）
  6. demo_physics 完整运行验收（需 ZCSR_ENABLE_BOX2D=ON 编译）

每步完成后：Windows ctest 19/19 + 耦合守卫 PASS + 独立 commit + 不 push

================================================================================
七、TXT Plan 发布格式（求助网页版 GPT 时用）
================================================================================

当遇到复杂任务需要 GPT 辅助时，按以下格式发布任务：

1. 用 ``` 代码块包裹完整任务描述
2. 包含：项目背景、具体问题、已确认的 API 签名、约束条件、期望输出
3. 明确要求"一次性输出完整可编译代码，不要分阶段，不要伪代码"
4. 如果 GPT 说"信息不足"，把当前 HEAD 的实际 API 签名补充后再追问
5. GPT 返回代码后，直接落地编译验证，不要只看不做

关键原则：
- GPT 看不到当前仓库，必须把所有 API 签名手动贴给它
- GPT 可能给历史快照的代码，必须核对当前 HEAD 再落地
- 小字渲染 / 设置面板这类 UI 任务，GPT 给方案后必须实际编译运行验证
- 不要让 GPT 重写整个文件，要求局部补丁 + 明确插入位置

================================================================================
八、环境速查
================================================================================

【项目路径】
  根目录：C:\Users\chen\Documents\GitHub\zero-coupling-signal-router
  构建目录：build\
  头文件：include\zcsr\
  引擎模块：modules\
  集成层：integration\

【Git】
  可执行文件：C:\Users\chen\AppData\Local\GitHubDesktop\app-3.6.4\resources\app\git\cmd\git.exe
  仓库管理：GitHub Desktop（图形界面，commit/push 用这个更方便）
  当前分支：claude/image-agent4-tests
  未 push commits：82+（禁止 push，等用户确认）

【工具链】
  编译器：MSVC 19.51 (Visual Studio 2022 / v18)
  构建系统：CMake + Ninja
  vcvars：C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat
  WSL：Ubuntu-24.04（root 密码：root）
  ADB：C:\Users\chen\Downloads\scrcpy-win64-v4.1\adb.exe
  Android 设备：3d16ff0e（Redmi K60 Pro）

【密码】
  WSL root：root
  Android keystore：android（alias: zcsr-debug）

【零耦合规则】
  modules/ 下的文件只能 include include/zcsr/*.h（shared contracts）
  不能 include 其他模块的头文件
  耦合守卫：tools/check_no_cross_includes.sh（WSL 运行）
  integration/ 下的文件可以 include 多个模块（集成层不受限）
