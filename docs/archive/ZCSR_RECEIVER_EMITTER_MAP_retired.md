# ZCSR Receiver Emitter Map

本文档列出 ZCSR UI Runtime 当前实际发出的所有语义事件，供 UI Receiver（Layer 2）编写接收端。
所有引用均指向仓库真实文件和行号。状态：IMPLEMENTED=已落盘可编译，DESIGNED=仅规划未实现。

## 1. 事件基础设施

事件队列实现：
- 环形缓冲，调用方提供存储，零 malloc
- 队列满时丢弃并计数 dropped（不阻塞）

公开头文件：
- `include/zcsr/ui_panel.h` — zcsr_ui_event / zcsr_ui_event_type / zcsr_ui_context
- `include/zcsr/ui_node.h`  — zcsr_ui_event payload 里的 value / key / id

事件产生入口（静态函数）：
- `modules/ui/ui_panel.c:33`  `static void emit(ctx, event)` — 入队
- `modules/ui/ui_panel.c:48`  `static void emit_node(ctx, type, node)` — 从 node 构造事件并 emit

出队接口：
- `bool zcsr_ui_poll_event(zcsr_ui_context* ctx, zcsr_ui_event* out)`
  （ui_panel.h:110）

## 2. 事件类型枚举

定义位置：`include/zcsr/ui_panel.h:54-63`

| 枚举值 | 数值 | 含义 |
|--------|------|------|
| ZCSR_UI_EVENT_NONE | 0 | 无 |
| ZCSR_UI_EVENT_ACTION | 1 | 按钮激活（点击完成） |
| ZCSR_UI_EVENT_VALUE_CHANGED | 2 | 可编辑节点 pending 值变化 |
| ZCSR_UI_EVENT_FOCUS_CHANGED | 3 | 焦点节点切换 |
| ZCSR_UI_EVENT_APPLY_REQUEST | 4 | 调用方请求 Apply |
| ZCSR_UI_EVENT_APPLY_FAILED | 5 | Apply 被引擎拒绝 |
| ZCSR_UI_EVENT_CANCEL | 6 | Cancel All |
| ZCSR_UI_EVENT_RESET | 7 | Reset All |

## 3. Event Payload 结构

定义位置：`include/zcsr/ui_panel.h:65-72`

```c
typedef struct {
    zcsr_ui_event_type type;       // 事件类型
    zcsr_ui_id         target_id;  // 事件源节点 id
    zcsr_ui_key        action_key; // 节点 action_key（按钮语义）
    zcsr_ui_key        binding_key;// 节点 binding_key（属性语义）
    zcsr_ui_value      value;      // 节点 pending 当前值
    uint32_t           serial;     // apply 协议序列号
} zcsr_ui_event;
```

## 4. Primitive → 事件映射

### 4.1 Button (ZCSR_UI_BUTTON=2)
- 产生事件：ZCSR_UI_EVENT_ACTION
- 触发位置：`modules/ui/ui_panel.c:182`（mouse release 且 hover 命中时 activate）
- payload：
  - target_id = button id
  - action_key = button desc.action_key
  - binding_key = 0（按钮不绑定值）
  - value = none
- 状态：IMPLEMENTED

### 4.2 Toggle (ZCSR_UI_TOGGLE=3)
- 产生事件：
  1. ZCSR_UI_EVENT_VALUE_CHANGED — set_pending 成功后
     位置：`modules/ui/ui_panel.c:108`
     payload：target_id, binding_key, value=bool pending
  2. ZCSR_UI_EVENT_ACTION — mouse release 时 activate
     位置：`modules/ui/ui_panel.c:182`
     payload：target_id, action_key, binding_key, value=bool pending
- 状态：IMPLEMENTED

### 4.3 Slider (ZCSR_UI_SLIDER=4)
- 产生事件：ZCSR_UI_EVENT_VALUE_CHANGED
- 触发位置：
  - `modules/ui/ui_panel.c:459`（按下时）
  - `modules/ui/ui_panel.c:467`（拖动时）
- 触发函数：slider_from_pointer() 计算新值 → set_pending() → emit VALUE_CHANGED
- payload：target_id, binding_key, value=float pending
- 状态：IMPLEMENTED

### 4.4 Dropdown (ZCSR_UI_DROPDOWN=5)
- 产生事件：
  1. ZCSR_UI_EVENT_VALUE_CHANGED — 选项被选中
     位置：`modules/ui/ui_panel.c:108`（set_pending 成功后）
     payload：target_id, binding_key, value=int pending（选中 option.value）
  2. ZCSR_UI_EVENT_ACTION — mouse release 时 activate
     位置：`modules/ui/ui_panel.c:182`
- 状态：IMPLEMENTED

### 4.5 Label (ZCSR_UI_LABEL=6)
- 不产生任何事件
- 状态：IMPLEMENTED

### 4.6 Separator (ZCSR_UI_SEPARATOR=7)
- 不产生任何事件
- 状态：IMPLEMENTED

### 4.7 Panel (ZCSR_UI_PANEL=1)
- 不产生任何事件
- 状态：IMPLEMENTED

### 4.8 Image (ZCSR_UI_IMAGE=8)
- 不产生任何事件
- 状态：IMPLEMENTED

### 4.9 TextInput (ZCSR_UI_TEXTINPUT)
- 尚未定义（P0 新增）
- 计划事件：VALUE_CHANGED（commit 时）
- 状态：DESIGNED

### 4.10 ScrollView (ZCSR_UI_SCROLLVIEW)
- 尚未定义（P0 新增）
- 计划事件：SCROLL_CHANGED（滚动 offset 变化时）
- 状态：DESIGNED

## 5. 全局事件（非节点产生）

### 5.1 ZCSR_UI_EVENT_APPLY_REQUEST
- 产生位置：`modules/ui/ui_panel.c:531`（zcsr_ui_request_apply 内部）
- payload：
  - type = APPLY_REQUEST
  - serial = 新 apply_serial
  - 其他字段 0
- 用途：通知 Receiver "用户点了 Apply，请落引擎"
- 状态：IMPLEMENTED

### 5.2 ZCSR_UI_EVENT_APPLY_FAILED
- 产生位置：`modules/ui/ui_panel.c:572`（confirm_apply(success=false)）
- payload：serial
- 用途：通知 Receiver "引擎拒绝了这次 Apply"
- 状态：IMPLEMENTED

### 5.3 ZCSR_UI_EVENT_CANCEL
- 产生位置：`modules/ui/ui_panel.c:595`（zcsr_ui_cancel_all）
- payload：无
- 用途：通知 Receiver "用户点了 Cancel，pending 已回滚"
- 状态：IMPLEMENTED

### 5.4 ZCSR_UI_EVENT_RESET
- 产生位置：`modules/ui/ui_panel.c:616`（zcsr_ui_reset_all）
- payload：无
- 用途：通知 Receiver "所有节点 pending 重置为 initial"
- 状态：IMPLEMENTED

### 5.5 ZCSR_UI_EVENT_FOCUS_CHANGED
- 产生位置：`modules/ui/ui_panel.c:223`
- payload：target_id = 新 focus 节点 id
- 用途：Receiver 可据此高亮/显示描述
- 状态：IMPLEMENTED

## 6. Composite 现状

当前仓库**没有真正的 Composite**。所有节点都是 Primitive 独立存在。
所谓 "Settings Page" 是组装层硬编码布局（integration/ui_settings_page.c），不是可复用 Composite。

计划中的 Composite（DESIGNED，未实现）：

| Composite ID | 名称 | 输入 Primitive | 对外事件 | 状态 |
|--------------|------|---------------|----------|------|
| COMP_NUMERIC_SLIDER | NumericSlider | Slider + TextInput | VALUE_CHANGED (float) | DESIGNED |
| COMP_CHECKBOX | Checkbox | Toggle + Label | VALUE_CHANGED (bool) | DESIGNED |
| COMO_TABS | Tabs | Button 组 + Panel | TAB_CHANGED (int) | DESIGNED |
| COMP_SETTINGS_ROW | SettingsRow | Label + 任意 Primitive | 透传子 Primitive 事件 | DESIGNED |
| COMP_SETTINGS_PAGE | SettingsPage | Panel + ScrollView + SettingsRow 组 | APPLY_REQUEST/CANCEL/RESET | DESIGNED |

## 7. Receiver 应接收的事件清单

Receiver 只应接收以下"对外有意义"的事件：

1. ZCSR_UI_EVENT_VALUE_CHANGED
   - 来自 Toggle/Slider/Dropdown/TextInput
   - payload：binding_key + value
   - Receiver 据此把值转发给 Game/Develop

2. ZCSR_UI_EVENT_ACTION
   - 来自 Button
   - payload：action_key
   - Receiver 据此路由到 Game 动作（打开设置/关闭设置/确认对话框）

3. ZCSR_UI_EVENT_APPLY_REQUEST / APPLY_FAILED / CANCEL / RESET
   - Receiver 管理 Settings Window 的 Apply 三段式

Receiver 不应接收：
- FOCUS_CHANGED（这是 Primitive 内部状态，Receiver 若需要可从 poll 取但不作为核心信号）
- 节点 hover/pressed 状态（纯 Primitive 内部）

## 8. 当前调用方（参考，不是 Receiver）

当前 integration/demo_ui.c 直接 poll_event 并 switch action_key。
这是旧 host 模式，未来 Receiver 取代这个角色。
位置：`integration/demo_ui.c` 事件循环段。
