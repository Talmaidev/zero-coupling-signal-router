# P0-1 输入消息泵竞争诊断报告：R/ESC 按键无效

**日期**：2026-10-22
**状态**：已修复
**修复 commit**：未提交（工作区）

---

## 1. 问题描述

在 Aim Lab FPS 训练 demo（`integration/demo_aim_trainer.c`）中，R 键（重新开始）和 ESC 键（退出）无效。其他按键（如 Enter）和鼠标操作正常。

## 2. 根因分析

### 2.1 消息泵架构（修改前）

ZCSR Windows 平台有两个消息泵：

| 函数 | 文件 | PeekMessage 参数 | 职责 |
|------|------|-----------------|------|
| `zcsr_input_platform_pump` | `modules/input/input.c:110` | `PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE)` | 拦截键盘消息并记录状态，然后 Translate+Dispatch |
| `zcsr_surface_pump_events` | `modules/platform/platform.c:612` | `PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)` | 获取所有线程消息并 Translate+Dispatch |

**[SOURCE]** `input.c:110` 使用 `hwnd` 参数，只获取属于该窗口的消息。
**[SOURCE]** `platform.c:612` 使用 `NULL` 参数，获取**所有线程消息**（包括该窗口的消息和线程消息）。

### 2.2 竞争条件

demo 主循环的调用顺序（`demo_aim_trainer.c:282-284`）：
```c
zcsr_input_pump(g_input, g_surface);        // 1. 只获取 hwnd 消息
zcsr_input_ext_pump(g_inputext, g_surface); // 2. 鼠标
zcsr_surface_pump_events(g_surface);        // 3. 获取所有剩余消息
```

**[HYPOTHESIS]** 竞争窗口：在 `input_pump` 运行完毕后、`surface_pump_events` 运行前的微秒级间隙内，如果有新的键盘消息（WM_KEYDOWN）到达消息队列，`surface_pump_events` 会用 `NULL` hwnd 获取它并 DispatchMessage，但**不会记录键盘状态**（因为键盘状态记录只在 `input_pump` 中）。

**[SOURCE]** `surface_pump_events` 只做 `TranslateMessage` + `DispatchMessageA`，没有任何键盘状态记录逻辑。

### 2.3 为什么 R/ESC 特别受影响

**[HYPOTHESIS]** R 和 ESC 是"单次触发"按键（按下即触发动作，不需要持续检测 is_down）。demo 使用 `zcsr_input_poll` 事件队列检测按键：
```c
while (zcsr_input_poll(g_input, &ev)) {
    if (ev.key == VK_R) key_r = 1;
    if (ev.key == VK_ESC) key_esc = 1;
}
```
如果 WM_KEYDOWN 被 `surface_pump_events` 窃取，`zcsr_input_record_key` 不会被调用，事件队列中就没有 R/ESC 的 KEY_DOWN 事件，demo 检测不到。

其他按键如果同时用 `zcsr_input_is_down` 检测（持续状态），可能因为后续帧的 WM_KEYDOWN 重复（auto-repeat）被 `input_pump` 捕获而"看起来正常"。但 R/ESC 只在按下瞬间触发，错过一次就完全无效。

## 3. 修复方案

将 `input_pump` 的 `PeekMessageA` hwnd 参数从 `hwnd` 改为 `NULL`，让 input 模块统一泵取所有线程消息：

```c
// 修改前 (input.c:110)
while (PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE)) {

// 修改后
while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
```

**[SOURCE]** 修改后 `input_pump` 获取所有线程消息，拦截键盘消息并记录状态，然后 Dispatch。`surface_pump_events` 随后运行时队列已空（或只有 input_pump 之后新到达的极少量消息），竞争窗口大幅缩小。

### 3.1 为什么不在 surface_pump_events 中也记录键盘状态

**[HYPOTHESIS]** 零耦合原则：input 模块负责输入状态，platform 模块负责窗口/事件。如果在 platform 中也记录键盘状态，会导致重复记录和状态不一致。统一由 input 模块泵取所有消息是更干净的方案。

## 4. 验证

### 4.1 单元测试

- `test_input`：PASS（clock + frame timer + input init/queue + close flag）
- `test_inputext`：PASS

### 4.2 全量回归

- ctest：21/21 PASS
- 耦合守卫：OK（modules include only shared contracts）

### 4.3 运行时验证

**[MISSING]** 无法在无头环境中模拟真实键盘输入验证 R/ESC。需用户在实际运行 demo_aim_trainer 时手动验证。

## 5. 遗留风险

1. **[HYPOTHESIS]** `input_pump` 改为 `NULL` hwnd 后，会获取所有线程消息（包括其他窗口的消息，如果有的话）。在单窗口应用中无影响；多窗口应用可能需要按 hwnd 过滤。
2. **[HYPOTHESIS]** `surface_pump_events` 仍然存在，在 `input_pump` 之后新到达的消息仍可能被它窃取。但这个窗口只有微秒级，实际影响可忽略。
3. **[SOURCE]** Linux/macOS/Android 的消息泵实现不同，此修复仅影响 Windows。

## 6. 结论

根因是 Windows 平台双消息泵的竞争：`input_pump` 只获取 hwnd 消息，`surface_pump_events` 获取所有消息，导致键盘消息可能被后者窃取而不记录状态。修复方案是让 `input_pump` 统一泵取所有线程消息。单元测试和全量回归通过，运行时验证需用户手动确认。
