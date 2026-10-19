# B 组封存报告

## 完成状态

macOS AudioUnit 后端代码完整落地。三平台（Windows/Linux/Android）零警告编译回归通过，Windows 19/19，耦合守卫 PASS。macOS 编译与真机验证待 Mac 环境。

## Commit 链

| 项 | Commit | 内容 |
|---|---|---|
| B 组 | 4b2e1dd | macOS AudioUnit 后端完整实现 |

tag: b-group

## 落地文件

| 文件 | 改动 |
|---|---|
| modules/audiomix/audiomix.c | Apple include、backend 字段、PCM ring 条件扩展、AudioUnit render callback、CoreAudio 监听器、open/close/write、open_ex/close_ex、start 初始化、stop 隔离保护 |
| CMakeLists.txt | CoreAudio/AudioToolbox/AudioUnit 链接、deployment target 12.0、zcsr_demo bundle、verify_macos target |
| cmake/Info.plist.in | 新建 bundle 元信息模板 |
| .github/workflows/macos.yml | 新建 macOS CI |
| README.md | 追加 macOS 构建段落 |
| integration/verify_macos.c | 新建 30 秒人工设备切换验证程序 |
| tools/collect_macos_logs.sh | 新建 CoreAudio 日志+设备信息收集脚本 |

## 架构裁决

| 问题 | 裁决 |
|---|---|
| DefaultOutput vs HALOutput | DefaultOutput（系统默认输出，设备变化由 CoreAudio 监听捕获） |
| PCM ring | 复用 Android pcm_ring，条件扩展为 `__ANDROID__ \|\| __APPLE__` |
| 设备移除事件 | 默认输出变化发 CHANGED；无默认设备发 LOST |
| 停止顺序 | 不在 stop() 里停 AudioUnit，worker 退出时 close |
| prefill | 不需要，callback 无数据时输出静音 |
| 链接框架 | 显式链接 CoreAudio/AudioToolbox/AudioUnit |

## 平台支持层

| 平台 | 音频后端 | 状态 |
|---|---|---|
| Windows | WASAPI + IMMNotificationClient | 完整实现，A3 封存 |
| Linux | ALSA 动态加载 + /proc 轮询 | 完整实现，A3 封存 |
| Android | AAudio + error callback | 完整实现，Phase 1 + A3 封存 |
| macOS | AudioUnit DefaultOutput + CoreAudio listener | 代码落地，待编译验证 |

## 未测项（环境限制，非代码缺陷）

| 未测项 | 原因 |
|---|---|
| macOS 编译与链接 | 无 Mac 构建环境 |
| AudioUnit interleaved stereo PCM 格式 | 待 macOS 12+ 真机 |
| CoreAudio 默认输出属性变化时序 | 待真机 |
| AudioUnit stop/uninitialize/dispose 退出时序 | 待真机 |
| CoreAudio listener 注销失败隔离路径 | 待故障注入 |
| verify_macos 30 秒人工验证 | 待真机 |
| collect_macos_logs.sh 运行 | 待真机 |
| GitHub Actions macOS CI | 待 push 后触发 |

## 已知限制

1. macOS 代码未经编译验证，可能存在 API 名称、枚举值、头文件路径等编译错误
2. ZCSR_MIX_POOL=1 时，CoreAudio listener 注销失败会永久占用唯一槽位
3. AudioUnit render callback 假设单 buffer 双声道 interleaved 格式，若 DefaultOutput 不支持该格式需后续适配
4. verify_macos 是交互式程序，不纳入 ctest 自动化

## 下一步

- C 组（物理/光照）：等真游戏需求驱动
- D 组（文档）：v4.0 启动指令已更新，可按需补充 B 组状态
- E 组（低优先）：iOS / 多点触控 / Android 键盘
- macOS 验证：待 Mac 环境就绪后执行编译 + verify_macos + collect_macos_logs.sh

## 封存判定

B 组目标"macOS AudioUnit 后端代码完整落地，三平台回归不受影响"达成。

不宣称 macOS 后端生产可用（编译与真机验证未做）。
