# zcsr 最小可玩 2D 游戏：打砖块（Breakout）技术设计与测试计划

> 版本：v1.0 设计稿（待评审）
> 日期：2026-10-19
> 状态：设计阶段，未开始编码

## 1. 目标

用 zcsr 现有模块组合出一个真正可玩的单屏打砖块游戏，验证模块协作能力。
不新增引擎模块，不修改公开 API 签名，不引入第三方依赖。

## 2. 文件清单

| 文件 | 类型 | 说明 |
|---|---|---|
| `integration/demo_breakout.c` | 新增 | 游戏主程序（单文件，~500 行） |
| `integration/breakout_hit.wav` | 新增 | 砖块碰撞音效（生成，~100ms） |
| `integration/breakout_paddle.wav` | 新增 | 挡板碰撞音效（生成，~80ms） |
| `integration/breakout_lose.wav` | 新增 | 失球音效（生成，~300ms） |
| `CMakeLists.txt` | 修改 | 新增 `zcsr_breakout_demo` 目标 |
| `docs/archive/BREAKOUT_GAME.md` | 新增 | 验收记录与设计存档 |

**不修改**：任何 `include/zcsr/*.h` 公开头文件、任何模块内部实现。

## 3. 模块使用映射

| 模块 | 用途 | 关键 API |
|---|---|---|
| platform | 窗口、事件泵 | `zcsr_surface_create`, `zcsr_surface_pump_events`, `zcsr_surface_destroy` |
| input | 键盘输入 | `zcsr_input_init`, `zcsr_input_pump`, `zcsr_input_poll`, `zcsr_input_is_down` |
| glrender | 渲染 | `zcsr_gl_create`, `zcsr_gl_clear`, `zcsr_gl_begin`, `zcsr_gl_fill_rect`, `zcsr_gl_draw_text`, `zcsr_gl_flush`, `zcsr_gl_present` |
| hsm | 游戏状态机 | `zcsr_hsm_create`, `zcsr_hsm_dispatch`, `zcsr_hsm_current` |
| audiomix | 音效播放 | `zcsr_mix_start`, `zcsr_mix_register_wav`, `zcsr_mix_play`, `zcsr_mix_stop` |
| signal | 信号路由 | `ZCSR_DEFINE_ROUTER`, `xxx_emit` |
| collision | AABB 碰撞 | `zcsr_aabb_intersect` |
| state_buffer | HSM 持久化 | `zcsr_state_init` |

**不使用**：culling（单屏全可见）、atlas（纯几何形状）、image（无贴图）、overlay（用 glrender 直接画 UI）。

## 4. 游戏规格

### 4.1 窗口与坐标
- 窗口：800 × 600 像素
- 坐标系：左上角 (0,0)，y 向下
- 固定逻辑分辨率，不做 DPI 缩放（后续可扩展）

### 4.2 游戏对象

| 对象 | 尺寸 | 初始位置 | 颜色 |
|---|---|---|---|
| 挡板 (paddle) | 100 × 15 | 底部居中，y=560 | 白色 (1,1,1,1) |
| 球 (ball) | 10 × 10 | 挡板上方居中 | 青色 (0.3,1,1,1) |
| 砖块 (brick) | 70 × 24 | 顶部网格，10列×5行 | 按行渐变（红→紫） |
| 墙壁 | 左/右/上三边 | 窗口边缘 | 不可见（逻辑碰撞） |

### 4.3 砖块布局
- 列数：10，行数：5，共 50 块
- 砖块宽 70，间距 5，左边距：(800 - 10*70 - 9*5) / 2 = 2.5 → 取 3
- 顶部边距：60（留出分数显示空间）
- 行间距：5
- 每块 10 分，满分 500

### 4.4 物理参数
- 球速：初始 280 px/s（约 4.67 px/frame @60fps）
- 挡板速度：420 px/s（左右键）
- 球与挡板碰撞：根据击中挡板的水平位置改变反弹角度（中心垂直，边缘斜射）
- 球与砖块碰撞：简单反弹（根据碰撞边决定 x/y 速度取反）
- 固定时间步长：dt = 1/60s，不做插值

### 4.5 生命与胜负
- 初始生命：3
- 球落出底部：生命 -1，球重置到挡板上方
- 生命归零：游戏失败状态
- 所有砖块清除：游戏胜利状态
- 最高分：本次会话内记录（不持久化到磁盘）

## 5. 状态机设计

```
menu ──start──> playing ──pause──> paused
  ↑                │                  │
  │                │                  resume
  │                ▼                  ▼
  │             (ball lost)        (quit)
  │                │                  │
  │                ▼                  │
  └──game_over<── lives=0             │
                   │                  │
                   └──restart─────────┘
```

### 5.1 HSM 状态表

```c
static const zcsr_hsm_state states[] = {
    { "menu",       NULL },
    { "playing",    NULL },
    { "paused",     "playing" },  /* 子状态，playing 的暂停 */
    { "game_over",  NULL },
    { "win",        NULL },
};
```

### 5.2 转换表

| from | signal | to | 触发 |
|---|---|---|---|
| menu | start | playing | 空格/回车 |
| playing | pause | paused | P/ESC |
| paused | resume | playing | P/ESC |
| paused | quit | menu | Q |
| playing | lose_life | playing | 球落底（重置球，不切换状态） |
| playing | game_over | game_over | 生命=0 |
| playing | win | win | 砖块全清 |
| game_over | restart | playing | 空格/回车 |
| win | restart | playing | 空格/回车 |
| game_over | quit | menu | Q |
| win | quit | menu | Q |

## 6. 主循环设计

```
while (running):
    1. zcsr_surface_pump_events(surface)
    2. zcsr_input_pump(input, surface)
    3. 处理输入事件（key down → HSM dispatch）
    4. if state == playing:
         a. 挡板移动（is_down 左右键）
         b. 球位置更新（pos += vel * dt）
         c. 墙壁碰撞检测 & 反弹
         d. 挡板碰撞检测 & 反弹（角度调整）
         e. 砖块碰撞检测 & 消除 & 计分 & 音效
         f. 球落底检测 → lose_life / game_over
         g. 胜利检测 → win
    5. 渲染：
         zcsr_gl_clear
         zcsr_gl_begin
         画砖块（fill_rect）
         画挡板（fill_rect）
         画球（fill_rect）
         画 UI 文字（draw_text：分数、生命、状态提示）
         zcsr_gl_flush
         zcsr_gl_present
    6. 帧率控制：vsync on（依赖平台）
```

## 7. 碰撞检测

### 7.1 球 vs 挡板
- AABB 相交检测
- 若相交且球向下运动（vy > 0）：
  - 计算击中位置相对挡板中心的偏移：`offset = (ball_x - paddle_x) / (paddle_w / 2)`，范围 [-1, 1]
  - 反弹角度：`angle = offset * 60°`（最大 60 度）
  - 球速大小保持不变，vx = speed * sin(angle), vy = -speed * cos(angle)
  - 播放挡板音效

### 7.2 球 vs 砖块
- 遍历所有存活砖块，AABB 相交检测
- 若相交：
  - 确定碰撞边：比较球中心与砖块中心的 x/y 偏移
  - 若 |dx| > |dy|：水平碰撞 → vx = -vx
  - 否则：垂直碰撞 → vy = -vy
  - 砖块标记为已消除，分数 += 10
  - 播放砖块音效
  - 每帧只处理一次砖块碰撞（避免穿透多块时速度异常）

### 7.3 球 vs 墙壁
- 左墙（x < 0）：vx = -vx，x = 0
- 右墙（x > 800 - ball_w）：vx = -vx，x = 800 - ball_w
- 上墙（y < 0）：vy = -vy，y = 0
- 底部（y > 600）：失球

## 8. 音频设计

### 8.1 音效清单

| ID | 触发 | 时长 | 频率 | 说明 |
|---|---|---|---|---|
| `brick_hit` | 砖块被消除 | 100ms | 660Hz → 880Hz 上升 | 短促"叮" |
| `paddle_hit` | 球碰挡板 | 80ms | 440Hz | 短促"嗒" |
| `wall_hit` | 球碰墙壁 | 60ms | 330Hz | 极短"哒" |
| `lose_life` | 球落底 | 300ms | 440Hz → 110Hz 下降 | 低沉"嗡" |
| `win` | 全部清除 | 500ms | 523→659→784 琶音 | 胜利旋律 |

### 8.2 生成方式
用 Python 脚本生成 WAV 文件（PCM 16-bit 44100Hz stereo），提交到仓库。
不运行时生成，不引入代码生成依赖。

### 8.3 信号路由
```c
#define BREAKOUT_CONNS(X) \
    X("PLAY_BRICK",  zcsr_mix_slot_play) \
    X("PLAY_PADDLE", zcsr_mix_slot_play) \
    X("PLAY_WALL",   zcsr_mix_slot_play) \
    X("PLAY_LOSE",   zcsr_mix_slot_play) \
    X("PLAY_WIN",    zcsr_mix_slot_play)
ZCSR_DEFINE_ROUTER(breakout, BREAKOUT_CONNS)
```
碰撞检测代码中 `breakout_emit("PLAY_BRICK", "brick_hit")`，payload 为音效 ID。

## 9. 渲染设计

### 9.1 绘制顺序（painter's algorithm）
1. 背景（clear 深色）
2. 砖块（从后到前，同色可合并）
3. 挡板
4. 球
5. UI 文字（分数、生命、状态提示）

### 9.2 文字内容
- 左上角：`SCORE: 000`
- 右上角：`LIVES: 3`
- 菜单状态：屏幕中央 `BREAKOUT` + `Press SPACE to start`
- 暂停状态：屏幕中央 `PAUSED` + `P to resume, Q to quit`
- 失败状态：`GAME OVER` + `Final Score: XXX` + `Press SPACE to restart`
- 胜利状态：`YOU WIN!` + `Score: XXX` + `Press SPACE to play again`

### 9.3 颜色方案
- 背景：(0.05, 0.05, 0.08, 1.0) 深蓝黑
- 挡板：(0.9, 0.9, 0.9, 1.0) 近白
- 球：(0.3, 1.0, 1.0, 1.0) 青色
- 砖块行色（从上到下）：
  - 行0：红 (0.9, 0.2, 0.2)
  - 行1：橙 (0.9, 0.6, 0.2)
  - 行2：黄 (0.9, 0.9, 0.2)
  - 行3：绿 (0.2, 0.9, 0.3)
  - 行4：紫 (0.6, 0.2, 0.9)
- 文字：白色 (1,1,1,1)

## 10. 输入映射

| 按键 | 状态 | 动作 |
|---|---|---|
| LEFT / A | playing | 挡板左移 |
| RIGHT / D | playing | 挡板右移 |
| SPACE / ENTER | menu/game_over/win | 开始/重开 |
| P / ESC | playing | 暂停 |
| P / ESC | paused | 继续 |
| Q | paused/game_over/win | 返回菜单 |
| 任意键 | （无） | 不退出（与 demo_physics 不同） |

## 11. 数据结构（文件内 static，无堆）

```c
#define BRICK_COLS 10
#define BRICK_ROWS 5
#define BRICK_W    70
#define BRICK_H    24
#define BRICK_GAP  5
#define MAX_LIVES  3

typedef struct {
    float x, y, w, h;
    bool  alive;
} Brick;

typedef struct {
    float x, y, vx, vy;
} Ball;

typedef struct {
    float x, y, w, h;
} Paddle;

/* 全局游戏状态（文件内 static，无 malloc） */
static struct {
    Paddle paddle;
    Ball   ball;
    Brick  bricks[BRICK_COLS * BRICK_ROWS];
    int    score;
    int    lives;
    int    bricks_remaining;
    bool   ball_attached;  /* 球粘在挡板上，等待发射 */
} g_game;
```

## 12. 验收场景

### 12.1 功能验收（Windows 真机）

| # | 场景 | 预期 |
|---|---|---|
| F1 | 启动游戏 | 显示菜单，"BREAKOUT" + "Press SPACE" |
| F2 | 按空格开始 | 进入游戏，球粘在挡板上 |
| F3 | 左右键移动挡板 | 挡板平滑移动，不超出窗口 |
| F4 | 按空格发射球 | 球向上飞出 |
| F5 | 球碰左右墙 | 水平反弹，不穿墙 |
| F6 | 球碰顶墙 | 垂直反弹 |
| F7 | 球碰挡板中心 | 近似垂直反弹 |
| F8 | 球碰挡板边缘 | 大角度斜射 |
| F9 | 球碰砖块 | 砖块消失，分数+10，音效播放 |
| F10 | 球落底 | 生命-1，球重置到挡板 |
| F11 | 生命归零 | 显示 GAME OVER，最终分数 |
| F12 | 全清砖块 | 显示 YOU WIN |
| F13 | 游戏中按 P | 暂停，显示 PAUSED |
| F14 | 暂停按 P | 继续游戏 |
| F15 | 暂停按 Q | 返回菜单 |
| F16 | GAME OVER 按空格 | 重新开始（分数重置） |
| F17 | 关闭窗口 | 程序正常退出，无崩溃 |

### 12.2 模块协作验收

| # | 验证点 | 方法 |
|---|---|---|
| M1 | input → 游戏逻辑 | 按键后挡板移动/状态切换 |
| M2 | collision → 游戏逻辑 | 球与砖块/挡板/墙壁正确反弹 |
| M3 | 游戏逻辑 → signal → audiomix | 碰撞时音效播放 |
| M4 | hsm → 游戏逻辑 | 状态转换正确（菜单/游戏/暂停/结束） |
| M5 | glrender → 显示 | 所有对象正确渲染，文字清晰 |
| M6 | 资源清理 | 退出时 mixer/renderer/surface 正确释放 |

### 12.3 性能验收

| 指标 | 预期 |
|---|---|
| 帧率 | 稳定 60fps（vsync on） |
| 每帧 CPU 耗时 | < 2ms（游戏逻辑 + 渲染提交） |
| draw call 数 | ≤ 3（砖块批 + 挡板球 + 文字） |
| 内存 | 无运行时堆分配（全部 static） |

### 12.4 跨平台验收

| 平台 | 验收内容 |
|---|---|
| Windows | 全部 F1-F17 + M1-M6 + 性能 |
| Android NDK 编译 | 编译通过（demo_breakout 排除在 Android 构建外，因为需要键盘） |
| 耦合守卫 | PASS（demo 只 include 公开头文件） |
| ctest 回归 | 18/18 不退化 |

**注意**：Android 真机运行不在本轮范围（打砖块需要键盘，Android 需触屏控制，作为后续扩展）。Android 仅验证编译不破坏。

## 13. 实现阶段划分

### 阶段 A：骨架（~150 行）
- 窗口 + renderer + input 初始化
- 主循环 + 清空 + present
- HSM 创建 + 菜单状态显示
- 退出清理

### 阶段 B：游戏对象 + 渲染（~100 行）
- 挡板/球/砖块数据结构初始化
- 砖块网格生成
- 所有对象的 fill_rect 渲染
- 分数/生命文字渲染

### 阶段 C：输入 + 物理（~150 行）
- 挡板移动
- 球移动 + 墙壁碰撞
- 挡板碰撞（角度调整）
- 砖块碰撞 + 消除 + 计分
- 失球 + 生命 + 胜负判定

### 阶段 D：音频 + 信号（~50 行）
- 生成 WAV 音效文件
- audiomix 初始化 + 注册
- signal router 定义
- 碰撞时 emit 信号

### 阶段 E： polish + 验收（~50 行）
- 暂停/菜单/结束状态的完整逻辑
- 球粘挡板等待发射
- 边界条件修复
- 验收测试

## 14. 不做的事项（明确边界）

- 不做触屏控制（Android 适配为后续任务）
- 不做背景音乐（仅音效）
- 不做粒子特效
- 不做关卡系统（单关卡）
- 不做最高分持久化（内存内记录）
- 不做 DPI 缩放
- 不做游戏手柄支持
- 不修改任何公开 API
- 不新增引擎模块
- 不引入第三方依赖
- 不做性能优化（基线已证明充足）

## 15. 风险与对策

| 风险 | 对策 |
|---|---|
| 球穿透砖块（高速时一帧跨越多块） | 限制球速上限 ≤ 350 px/s，每帧最多消除一块 |
| 球卡在挡板内 | 碰撞后将球位置修正到挡板上方，再改速度 |
| 角度反弹导致球水平速度为 0（死循环） | 限制最小水平速度 ≥ 50 px/s |
| audiomix 在某些环境无声 | 音频失败不阻塞游戏，打印 WARN 继续 |
| 窗口关闭事件未处理 | 检查 surface 有效性，running = false |

## 16. 提交计划

- 独立 commit：`feat(demo): breakout game — playable single-screen breakout`
- 包含：demo_breakout.c + 3 个 WAV + CMakeLists 修改 + 设计文档
- 不 git push
- 保留测试日志和截图

---

**评审请求**：请确认设计方向、模块选择、游戏规格和验收标准。评审通过后按阶段 A→E 分阶段实现。
