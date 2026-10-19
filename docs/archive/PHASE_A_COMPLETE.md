# A 组封存报告

## 完成状态

6/6 项完成，三平台零警告，Windows/Linux 19/19，耦合守卫 PASS。

## Commit 链

| 项 | Commit | 内容 |
|---|---|---|
| A6 | 49da54e | 统一日志 log.h |
| A1 | 1e02b2f | 显示器枚举 + 窗口模式 + 移屏 |
| A2 | ec1c0b0 | DPI 变化回调 |
| A4 | 60aa5a3 | 窗口尺寸约束 |
| A5 | 8495814 | 焦点丢失/恢复 |
| A3 | 4152333 → e259457 | 音频设备恢复（5 commit） |

tag: platform-edge

## A3 内部块

| 块 | Commit | 内容 |
|---|---|---|
| 块1+2+3+7+8 | 4152333 | 状态机基础设施 |
| 块4 | 8de0ee5 | Windows COM 通知 |
| 块5 | f6e53ea | Linux /proc/asound/cards 轮询 |
| 块6 | b0ee896 | Android AAudio error callback |
| 块9 | e259457 | 后端错误统一接线 |

## 契约头变化

22（Phase 0 前）→ 23（Phase 1 1E 加 input_ext_android.h）→ 24（A 组 A6 加 log.h）

## 平台支持层

| 平台 | 功能 |
|---|---|
| Windows | 全部真实实现（EnumDisplayMonitors/WM_DPICHANGED/WM_GETMINMAXINFO/WM_SETFOCUS/IMMNotificationClient） |
| Linux | 全部真实实现（XRRGetMonitors/Xft.dpi/RandR/XSizeHints/FocusIn-FocusOut/ALSA prepare） |
| Android | API 就绪，no-op 明确返回 false，AAudio error callback 接入 |
| macOS | 占位，B 组实现 |

## 未测项（环境限制，非代码缺陷）

| 阶段 | 未测项 | 原因 |
|---|---|---|
| A2 | Windows 跨屏 DPI 切换 | 单屏机器 |
| A4 | Windows 拖拽被拒真机 | 用户授权跳过 |
| A4 | Linux xprop hints 生效 | WSLg WM 可能忽略 |
| A5 | Windows 真实 Alt+Tab 焦点 | overlay 窗口形态 |
| A5 | Linux override_redirect 焦点 | 窗口形态限制 |
| A5 | Android reset 恰好 1 次 | 需设备验证 |
| A3 | Windows COM 设备切换 | 环境无音频设备（WASAPI Activate E_NOINTERFACE） |
| A3 | Linux 声卡插拔触发 | WSLg 无真实声卡 |
| A3 | Android 断连触发 | 需 USB 音频设备 |
| A3 | 各平台写失败重建 | 难模拟 |

## 已知限制

1. A3 块4：Windows COM 注册失败时设备通知能力降级（写失败和 open 重试仍可用）
2. A3 块4：注销失败时固定池槽位永久占用（ZCSR_MIX_POOL=1 时后续通知不可用）
3. A5：Linux overlay 窗口（override_redirect=True）可能永远收不到焦点事件
4. A3 块9：Linux /proc 首次不存在时只建基线，极端情况需后端重试

## 下一步

- B 组（macOS）：待 Mac 环境就绪
- D 组（文档）：v4.0 启动指令更新
- C 组（物理/光照）：等真游戏需求驱动

## 封存判定

A 组目标"Windows/Linux 平台边缘情况达到验收标准，Android 明确 no-op，macOS 占位"达成。

不宣称整个跨平台层生产可用（macOS B 组未做，物理/光照 C 组未做）。
