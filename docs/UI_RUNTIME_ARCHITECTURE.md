# ZCSR UI Runtime 四层隔离架构

本文档是 UI 子系统的权威架构规定。任何新增 UI 功能必须遵守此分层，不得跨层调用。

## 1. 四层隔离

```
┌─────────────────────────────────────────────┐
│  Layer 4: Game / Develop                    │
│  实际业务 + 运行数据。Develop 模拟 Game。   │
│  调用 Receiver，不直接操作 Primitive。       │
├─────────────────────────────────────────────┤
│  Layer 3: Editor                             │
│  编辑 UI 布局/控件位置/组合/预设。           │
│  调用 Receiver，不复制 Primitive 状态机。    │
├─────────────────────────────────────────────┤
│  Layer 2: UI Receiver                        │
│  独立库。接收 ZCSR 对外 Composite 信号。     │
│  内置默认 Settings Window 布局。             │
│  提供 Editor/Game 可调用的 UI 接口。        │
├─────────────────────────────────────────────┤
│  Layer 1: ZCSR (基础库)                     │
│  类似 SDL2。只提供模块和公开 API。           │
│  不定义 main()。                            │
│  提供 Primitive 行为和统一语义事件。         │
│  不含 Settings Window、Editor 业务、Game 业务。│
└─────────────────────────────────────────────┘
```

## 2. 各层职责

### Layer 1: ZCSR
- 纯 C 基础库，类似 SDL2。
- 提供 10 类 Primitive 的行为：
  - 已有 8 类：Panel, Button, Toggle, Slider, Dropdown, Label, Separator, Image
  - P0 新增 2 类：TextInput, ScrollView
- 产生统一语义事件（VALUE_CHANGED / ACTION / FOCUS_CHANGED / APPLY_REQUEST 等）。
- 不包含 Settings Window。
- 不包含 Editor/Game 业务。
- 不定义 main()。

### Layer 2: UI Receiver
- 独立库，不是 ZCSR 内部的控件实现。
- 接收 ZCSR 对外有意义的 Composite 信号。
- 统一事件接收和分发。
- Settings Window 从旧 UI 迁移至此。
- 内置默认布局：居中 Window + 顶部 Header + 左侧分类导航 + 右侧 ScrollView + 底部 Reset/Cancel/Apply。
- 不硬编码 Game 的真实设置项，设置数据由 Game/Develop 提供。
- Editor 可通过 Receiver API 调整布局及保存预设。
- 不重写 Primitive 算法。

### Layer 3: Editor
- 调用 Receiver，编辑 UI 布局、控件位置、组合和预设。
- 不复制 ZCSR 控件状态机。
- 不自己实现第二套 Settings Window。

### Layer 4: Game / Develop
- Game 调用 Receiver 使用 UI。
- Develop 使用相同 Receiver 接口模拟 Game，承担开发联调。
- Editor 和 Game/Develop 是平级调用方，不互相依赖。

## 3. Main 和工具入口

- 所有模块、工具、Receiver、Editor、Game/Develop 都是库或可调用模块。
- 唯一允许 main() 的是独立测试程序（test main）。
- main.c 是外部启动器，选择并启动一个模块。
- main.c 不参与 UI、不参与游戏逻辑、不包含模块内部实现。
- 过去 Demo 的独立 main 模式不再作为新架构方案。旧 Demo 暂时保留，仅作迁移参考。

## 4. Primitive vs Composite

### Primitive（10 类，行为最小单位）
- 已有 8 类：Panel, Button, Toggle, Slider, Dropdown, Label, Separator, Image
- P0 新增：TextInput, ScrollView
- 其中有交互行为的 6 类：Button, Toggle, Slider, Dropdown, TextInput, ScrollView

### Composite（组合 Primitive，不复制算法）
- NumericSlider = Slider + TextInput（双向绑定同一 binding）
- Checkbox = Toggle + Label
- ComboBox = Dropdown + TextInput（搜索）
- Tabs = Button 组 + Panel 切换
- SettingsRow = Label + 任意 Primitive
- SettingsPage = Panel + SettingsRow 组 + ScrollView
- Dialog = 遮罩 Panel + 内容 Panel + Button 组

### Receiver 接收边界
Receiver 只接收 Composite 对外语义信号，不接收：
- Primitive 内部状态变化（hover/pressed/focus）
- Composite 内部子控件协调事件

## 5. 当前状态（2026-11-02）

- Layer 1 ZCSR Primitive：8/10 已实现，TextInput/ScrollView 待做
- Layer 2 Receiver：未开始。旧 Settings Window 在 integration/，待迁移
- Layer 3 Editor：未开始
- Layer 4 Game/Develop：未开始
- Shared Contract：仓库中不存在，待 GPT 提供

## 6. 禁止事项

- 不删除旧 UI 文件（暂作迁移参考）
- 不重写现有 8 类 Primitive
- 不将 Editor Tool Overlay 混入 Game Runtime
- 不实现 SaveSlot/ItemCard/RadialMenu（PVZ 阶段再说）
- 不实现虚拟化列表
- 不实现完整 Flexbox
- 每个模块不得自行定义 main()
