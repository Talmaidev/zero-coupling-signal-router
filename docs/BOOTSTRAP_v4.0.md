# zcsr 新 AI 启动指令 v4.0

生成：2026-10-18
基线：tag `platform-edge`（代码 e259457，文档 bec1b84）

## 一、项目定位

C11 跨平台 2D 游戏引擎。零耦合模块设计，契约头驱动，无运行时堆。

支持平台：Windows 10+ / Linux 桌面 / Android API 24+ / macOS 12+（未做）/ iOS 15+（不做）
不做：3D、手柄作为移动端一等输入、旧设备兼容、厂商 SDK

## 二、当前进度

已完成：
- Windows / Linux 主线封存，ctest 19/19
- Android Phase 0 + Phase 1（1A–1E），真机验证，tag `android-phase1`
- A 组平台边缘情况（A6/A1/A2/A4/A5/A3），tag `platform-edge`

未做：
- B 组 macOS 接入（B1 AudioUnit / B2 .app bundle / B3 CI / B4 README / B5 真机验证）
- C 组扩展层（C2 physics / C3 light2d），等真游戏需求驱动
- D 组文档（本组进行中）
- E 组低优先（iOS / 多点触控 / Android 键盘）

## 三、关键数据

契约头：`include/zcsr/*.h` 共 24 个
最新 commit：`bec1b84`（A 组封存报告）
代码基线：`e259457`（tag `platform-edge`）
已有 tag：`android-phase1`、`platform-edge`

测试基线：Windows Debug/Release 19/19，Linux 19/19，耦合守卫 PASS

## 四、约束（贯穿所有阶段）

1. 契约头 additive 优先，breaking 先提议
2. 每阶段独立 commit（Conventional Commits）
3. 每阶段跑 Windows/Linux 回归 + 耦合守卫 PASS
4. 不 git push
5. 失败保留原始日志，不猜
6. 外部库只在后端模块 include，契约头不暴露第三方类型
7. 引擎不依赖第三方数学库，自带最简 vec/mat
8. 优先复用现有代码，不重写整个文件
9. 平台门控必须 `#if defined(__ANDROID__)` 优先于 `#elif defined(__linux__)`（NDK 同时定义两者）

## 五、测试环境

Windows 主机：手动跑，输出贴回
Linux Kali 物理机：真 X11 + 真 WM
Linux WSL2：跑大部分编译验证
Linux VMware：只做 WSL2 覆盖不到的
Android 真机：Redmi K60 Pro (socrates, arm64-v8a)

## 六、A 组未测项（环境限制，非代码缺陷）

- A2：Windows 跨屏 DPI 切换（单屏机器）
- A4：Windows 拖拽被拒真机（用户跳过）；Linux xprop hints 生效（WSLg WM 可能忽略）
- A5：Windows 真实 Alt+Tab 焦点（overlay 窗口形态）；Linux override_redirect 焦点；Android reset 计数
- A3：Windows COM 设备切换（环境无音频设备）；Linux 声卡插拔触发（WSLg 无真实声卡）；Android 断连触发；各平台写失败重建

详见 `docs/PHASE_A_COMPLETE.md`。

## 七、A 组已知限制

1. Windows COM 注册失败时设备通知能力降级（写失败和 open 重试仍可用）
2. Windows 注销失败时固定池槽位永久占用（ZCSR_MIX_POOL=1 时后续通知不可用）
3. Linux overlay 窗口（override_redirect=True）可能永远收不到焦点事件
4. Linux /proc/asound/cards 首次不存在时只建基线，极端情况需后端重试

## 八、新 AI 不要做的事

- 不要重问已完成的工作（看 `docs/PHASE_A_COMPLETE.md`）
- 不要复核已封存的 commit（直接信 commit 链）
- 不要自行扩展未决方案（未决写入待决清单）
- 不要凭空发明 API（查契约头）
- 不要重写整个文件（用精确 Edit）
- 不要跳过编译和测试就宣称完成
- 不要 git push

## 九、当前下一步

B 组（macOS）待 Mac 环境就绪后启动。
D 组（文档）在本指令产出后由用户决定是否更新。
C 组（物理/光照）等真游戏需求驱动。

## 十、多 AI 分工

GPT：主攻代码与控制核心技术
DeepSeek：写方案、审核
豆包：本地执行（改文件、编译、ctest、设备验证）
用户：终审、测试操作
