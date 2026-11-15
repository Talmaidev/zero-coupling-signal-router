# ZCSR Primitive API Map

本文档是 ZCSR UI Runtime 全部基础 Primitive 的公开 API 清单。
基于实际头文件和实现，标注每个 Primitive 的当前状态。
状态：IMPLEMENTED=已实现可编译，PARTIAL=部分实现有缺口，MISSING=未实现。

## 1. Widget Type 枚举

现有（ui_node.h:41-50）：

```c
typedef enum {
    ZCSR_UI_PANEL = 1,
    ZCSR_UI_BUTTON,
    ZCSR_UI_TOGGLE,
    ZCSR_UI_SLIDER,
    ZCSR_UI_DROPDOWN,
    ZCSR_UI_LABEL,
    ZCSR_UI_SEPARATOR,
    ZCSR_UI_IMAGE
} zcsr_ui_widget_type;
```

计划新增（DESIGNED，尚未写入头文件）：

```c
    ZCSR_UI_TEXTINPUT,    /* UTF-8 文本输入 */
    ZCSR_UI_SCROLLVIEW    /* 滚动容器 */
```

## 2. 事件类型枚举

现有（ui_panel.h:54-63）：

```c
typedef enum {
    ZCSR_UI_EVENT_NONE = 0,
    ZCSR_UI_EVENT_ACTION,
    ZCSR_UI_EVENT_VALUE_CHANGED,
    ZCSR_UI_EVENT_FOCUS_CHANGED,
    ZCSR_UI_EVENT_APPLY_REQUEST,
    ZCSR_UI_EVENT_APPLY_FAILED,
    ZCSR_UI_EVENT_CANCEL,
    ZCSR_UI_EVENT_RESET
} zcsr_ui_event_type;
```

计划新增事件（DESIGNED）：

```c
    ZCSR_UI_EVENT_TEXT_CHANGED,    /* TextInput 编辑中 */
    ZCSR_UI_EVENT_TEXT_SUBMIT,     /* TextInput Enter 提交 */
    ZCSR_UI_EVENT_TEXT_CANCEL,     /* TextInput Esc 取消 */
    ZCSR_UI_EVENT_SCROLL_CHANGED   /* ScrollView offset 变化 */
```

## 3. Event Payload

现有（ui_panel.h:65-72）：

```c
typedef struct {
    zcsr_ui_event_type type;
    zcsr_ui_id         target_id;
    zcsr_ui_key        action_key;
    zcsr_ui_key        binding_key;
    zcsr_ui_value      value;
    uint32_t           serial;
} zcsr_ui_event;
```

计划扩展字段（DESIGNED）：
- TextInput 事件需要 text_ptr + text_len（UTF-8 字节）
- ScrollView 事件需要 offset_x + offset_y

## 4. Primitive 逐个清单

### 4.1 Panel (ZCSR_UI_PANEL=1)
状态：PARTIAL
属性：
  rect, visible, enabled, label_key
缺口：
  - parent_id（扁平数组无父子）
  - clip rect（无裁剪）
  - z-order（无显式层级）
  - inherited visible/enabled
事件：无
绘制：fill rect + border
文件：modules/ui/ui_panel.c, ui_gl_backend.c

### 4.2 Button (ZCSR_UI_BUTTON=2)
状态：PARTIAL
属性：
  rect, label_key, action_key
缺口：
  - 禁用态绘制（enabled=false 时仍响应）
  - repeat 激活（长按重复触发）
事件：ACTION (mouse release 时)
文件：ui_panel.c:182

### 4.3 Toggle (ZCSR_UI_TOGGLE=3)
状态：PARTIAL
属性：
  rect, binding_key, initial(bool)
缺口：
  - 键盘 Space 切换
  - 禁用态
事件：VALUE_CHANGED + ACTION
文件：ui_panel.c:108,182

### 4.4 Slider (ZCSR_UI_SLIDER=4)
状态：PARTIAL
属性：
  rect, minimum, maximum, step, binding_key, initial(float)
缺口：
  - 键盘 ←→ 调整
  - 滚轮调整
  - int/float 区分
事件：VALUE_CHANGED
文件：ui_panel.c:459,467

### 4.5 Dropdown (ZCSR_UI_DROPDOWN=5)
状态：PARTIAL
属性：
  rect, options[], option_count, binding_key, initial(int)
缺口：
  - 键盘 ↑↓ 选择
  - 滚轮滚动选项
  - 超长列表裁剪
事件：VALUE_CHANGED + ACTION
文件：ui_panel.c:108,182

### 4.6 Label (ZCSR_UI_LABEL=6)
状态：PARTIAL
属性：
  rect, label_key
缺口：
  - 对齐（左/中/右）
  - 多行换行
  - 截断
事件：无
文件：ui_gl_backend.c

### 4.7 Separator (ZCSR_UI_SEPARATOR=7)
状态：PARTIAL
属性：
  rect
缺口：
  - 水平/垂直方向
  - 粗细
事件：无
文件：ui_gl_backend.c

### 4.8 Image (ZCSR_UI_IMAGE=8)
状态：PARTIAL
属性：
  rect, resource_key
缺口：
  - 宽高比保持
  - 九宫格拉伸
事件：无
文件：ui_gl_backend.c

### 4.9 TextInput (ZCSR_UI_TEXTINPUT)
状态：MISSING
属性：
  rect, binding_key, buffer_ptr (caller-owned), buffer_cap,
  max_len, read_only, placeholder_key
行为：
  - UTF-8 光标移动（←→）
  - Home/End
  - 插入字符（通过 key_pressed + IME 预留）
  - Backspace/Delete
  - 选区（Shift+方向键，P1）
  - Enter 提交 → TEXT_SUBMIT
  - Esc 取消 → TEXT_CANCEL
  - 编辑中 → TEXT_CHANGED
事件：TEXT_CHANGED / TEXT_SUBMIT / TEXT_CANCEL
文件：待新建 modules/ui/ui_textinput.c（或并入 ui_panel.c）

### 4.10 ScrollView (ZCSR_UI_SCROLLVIEW)
状态：MISSING
属性：
  rect (viewport), content_w, content_h
状态：
  offset_x, offset_y
行为：
  - 滚轮垂直滚动
  - 滚动条 thumb 拖动
  - content 超出 viewport 时裁剪
  - bounds clamp
事件：SCROLL_CHANGED
文件：待新建 modules/ui/ui_scrollview.c（或并入 ui_panel.c）

## 5. Context 扩展（DESIGNED）

现有 context（ui_panel.h:83-98）：
  nodes, node_count, events, focus_id, pressed_id, popup_id, hot_id,
  apply_serial, apply_observed_serial, apply_pending, apply_failed

计划新增：
  - scroll_id（当前拖动滚动条的节点）
  - textinput_id（当前编辑的 TextInput）
  - caret_pos（TextInput 光标字节位置）

## 6. Input 扩展（DESIGNED）

现有 input（ui_panel.h:41-52）：
  mouse_x/y, mouse_down/pressed/released, wheel, key_pressed[320]

计划新增：
  - text_input_buf[64]（本帧 IME 提交的 UTF-8 字符）
  - text_input_len
  - （方向键已有 ZCSR_UI_KEY_LEFT/RIGHT/UP/DOWN/HOME/END）

## 7. 与 GPT Receiver 的对接边界

Receiver 接收：
  - VALUE_CHANGED (binding_key + value)
  - ACTION (action_key)
  - TEXT_CHANGED / TEXT_SUBMIT / TEXT_CANCEL (target_id + text)
  - SCROLL_CHANGED (target_id + offset)
  - APPLY_REQUEST / APPLY_FAILED / CANCEL / RESET

Receiver 不接收：
  - FOCUS_CHANGED（Primitive 内部状态）
  - hover/pressed 状态
  - ScrollView 内部滚动条拖动细节

## 8. 差异说明

GPT 曾报告 17 个 Primitive。当前仓库实际只有 8 个 widget type 枚举值。
Shared Contract (contract_registry.json) 不在仓库。
其余 9 个 Primitive ID 未确认，不自行编造。
待 GPT 提供 contract 后补充。
