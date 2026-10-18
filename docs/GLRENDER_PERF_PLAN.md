# GLRENDER 性能审计与优化计划

状态：v1.0（DS 方向复审通过，豆包落盘）
基线：HEAD `8ba9873`，代码基线 `02d429b`
生成：2026-10-19
证据纪律：每条结论标 SOURCE / MEASURED / HYPOTHESIS / MISSING

---

## 1. 现状

### 1.1 基线状态

| 项 | 值 | 证据 |
|---|---|---|
| HEAD | 8ba9873 | SOURCE |
| 代码基线 | 02d429b | SOURCE |
| glrender 文件数 | 10（含 .h） | SOURCE |
| 契约头 | 24 个 | SOURCE |
| 纹理句柄 | `uint32_t`，0=invalid | SOURCE (`gl_render.h:18`) |
| MAX_SPRITES | 10000 | SOURCE (`glrender_internal.h:46`) |
| MAX_VERTICES | 60000 (= 10000×6) | SOURCE (`glrender_internal.h:47`) |
| MAX_TEXTURES | 128 | SOURCE (`glrender_internal.h:48`) |
| MAX_INSTANCES | 10000 | SOURCE (`glrender_internal.h:49`) |
| VBO usage | GL_DYNAMIC_DRAW | SOURCE (`glrender_core.c:166`) |
| Windows ctest | 19/19，EXIT=0 | MEASURED |
| 耦合守卫 | PASS，EXIT=0 | MEASURED |
| 工作树 | 干净（build-box2d/、*.pdb 已加入 .gitignore） | SOURCE |

### 1.2 已测性能（MEASURED）

环境：Windows 10 Pro / Ryzen 7 7700 / Intel Arc B580 / 1920×1080 / Debug Ninja / VSync off / 真实显示

| 场景 | 输入 | 耗时 (best-of-240) |
|---|---|---|
| A | 1000 sprites × 128 textures | 0.6373 ms |
| B | 10000 sprites × 1 texture | 1.4361 ms |
| C | 1000 sprites × 同纹理 mixed mode | 0.2423 ms |

归一化：A=0.637 μs/sprite，B=0.144 μs/sprite，A/B=4.44x。

**重要限定**：
- 计时只包围 `zcsr_gl_submit_batch` 的 CPU 时间，不含 GPU 完成和 swap
- B 场景 60000 vertices = MAX_VERTICES，**正好贴上限零余量**（SOURCE）
- C 场景同纹理被合并为 1 draw，测的不是 blend 切换（SOURCE：`glrender_sprite.c:122-134`）

---

## 2. 瓶颈表

### P1 纹理查找线性扫

- **SOURCE**：`glrender_core.c:134-141`，线性扫 128 槽
- **调用点**：`glrender_sprite.c:94`（submit 每个 sprite 调一次）、`glrender_instanced.c:155`（draw_instances 每 run）；submit_batch 循环调 submit，故 B 场景 10000 sprites = 10000 次 find_tex
- **裁决**：确认存在且调用频率已确认；128 万次比较是理论上界（每次扫满 128 槽），实际比较次数取决于命中槽位，**不确认是主瓶颈**
- **优先级**：中（调用频率覆盖普通 sprite 全路径，但实际 CPU 成本未测）
- **前置工作**：先在 bench 统计实际比较次数和 CPU 耗时；句柄语义（uint32_t, 0=invalid）已确认，O(1) 索引不改变公开句柄
- **改动估计**：测量 5-15 行；真正改索引 30-60 行（需内部映射结构，见 O5）

### P2 instanced 每 run 重复状态设置

- **SOURCE**：`glrender_instanced.c:152-180`，每 run 重复 framebuffer/viewport/program/2×uniform/VAO/VBO
- **裁决**：确认重复工作结构存在；收益未测
- **优先级**：中高
- **方向**：批次内不变的状态提到 run 循环外，保留逐 run 纹理绑定+数据上传+draw
- **改动估计**：15-35 行，内部辅助函数，不新增公共 API
- **注意**：P2/P4/P5 收益重叠，不能简单相加

### P3 VBO 无 orphaning

- **SOURCE**：`glrender_sprite.c:118`、`glrender_instanced.c:175` 用 `glBufferSubData`；VBO 分配用 `GL_DYNAMIC_DRAW`（`glrender_core.c:166`）
- **裁决**：确认无显式 orphaning；stall 为 HYPOTHESIS
- **优先级**：中
- **方向**：对比现有上传 / orphaning（glBufferData NULL）/ 双缓冲轮换
- **改动估计**：原型 5-20 行
- **注意**：GL_DYNAMIC_DRAW 只是提示，不等于已避免同步

### P4 多路径调 update_size

- **SOURCE**：调用点 3 处——`glrender_core.c:220-230`（clear）、`glrender_sprite.c:106`（flush target==0）、`glrender_instanced.c:161`（draw_instances target==0）
- **裁决**：确认多调用点；平台实现是否每次做系统查询为 MISSING
- **优先级**：中，待取证
- **方向**：先读三平台实现测成本；若昂贵，设计 resize 事件驱动的缓存+失效
- **改动估计**：跨平台 15-50 行
- **注意**：不可直接删除查询，须覆盖窗口缩放/高DPI/surface重建/Android生命周期

### P5 无条件 make_current

- **SOURCE**：调用点 `glrender_sprite.c:103`、`glrender_instanced.c:157`
- **裁决**：调用路径确认；驱动开销为 HYPOTHESIS
- **优先级**：中低
- **方向**：优先在 P2 批次准备阶段合并重复调用；不直接做全局 current 缓存（外部代码可能切换上下文，有正确性风险）
- **改动估计**：5-20 行/平台

### P6 radix 4 pass 全量 memcpy

- **SOURCE**：`glrender_instanced.c:188-208`，每 pass 末尾 `memcpy(items, scratch, count*sizeof(...))`
- **调用点**：`sort_instances`（1 次）、`submit_state`（2 次：opaque 按 tex + translucent 按 z_desc）
- **裁决**：确认冗余内存搬运；代码结构最明确，最容易做对照实验
- **优先级**：高候选
- **方向**：ping-pong 交替 items/scratch 作为源和目标，4 pass 结束回到 items
- **改动估计**：10-25 行
- **验收**：逐元素对照原排序结果，空/单元素/重复键/极端数量/scratch 容量测试

### G1 bench 计时口径（A/B/C 同属）

- **SOURCE**：`bench_worst.c:43-53`，A/B/C 三个场景计时均只包围 `zcsr_gl_submit_batch` 的 CPU 调用区间，不含 flush/GPU/swap
- **裁决**：三个场景的现有耗时都不能解释为完整 GPU 绘制成本或完整渲染流水线基准
- **B 额外限制**：10000 sprites × 6 vertices = 60000 = MAX_VERTICES，容量恰好用满；若 flush 触发且进入计时区间，1.4361ms 不是干净的纯提交基线
- **C 额外限制**：同纹理不同 mode 可合并为 1 draw，不能解释为 blend state 切换或 shader 分支性能
- **优先级**：基准有效性阻塞项
- **方向**：分离 CPU 提交 / flush CPU / GPU（可用时）/ swap 四段计时；GPU 不可用时标 MISSING，不用 CPU 代替

### G2 radix 小分区快速路径

- **HYPOTHESIS**：count<2 时可能仍走 4 pass 完整流程
- **优先级**：低成本候选
- **方向**：Read 完整 radix 确认无早退；加 `if (count < 2) return;` 约 1-3 行

### G3 best-of-240 统计不足

- **SOURCE**：现有统计只报 best-of-240
- **裁决**：best 不代表典型/尾延迟
- **方向**：保留全部样本，报 p50/p95/best，记录预热和波动

---

## 3. bench_worst.c 效度核对

| 问题 | 裁决 |
|---|---|
| C 场景名不副实 | 同纹理被合并为 1 draw，测的不是 blend 切换。**降级为功能测试** |
| 计时只测 CPU 提交 | 不含 GPU/swap。新基准须分开 4 段计时 |
| B 场景贴 MAX_VERTICES | 60000 = MAX_VERTICES，**零余量确认**（SOURCE） |
| 缺 instanced/text/geometry/FBO | 新基准补 D/E/F/G 四场景 |
| best-of-240 | 补 p50/p95/样本分布 |

---

## 4. SDL2 差异（过 N4 证据门槛）

对照版本：SDL2 release-2.32.8

**不可主张独有**：
- 基础几何（line/rect/fill）——SDL2 有 `SDL_RenderDrawLine/Rect/FillRect`
- 带旋转/flip 的纹理绘制——SDL2 有 `SDL_RenderCopyEx`
- viewport/clip/logical size——SDL2 有 `SDL_RenderSetViewport/ClipRect/LogicalSize`
- 缩放/整数缩放——SDL2 有 `SDL_RenderSetScale/SetIntegerScale`
- 像素读取——SDL2 有 `SDL_RenderReadPixels`
- 顶点几何——SDL2 有 `SDL_RenderGeometry/GeometryRaw`
- 内部 batching——SDL2 有 render command queue + `SDL_RenderFlush`

**可谨慎主张的差异**：
- zcsr 公开实例化提交 + 调用方控制的排序辅助接口（`submit_instance_batch` / `sort_instances` / `instance_run` / `sort_state` / `submit_state`）
- SDL2 公共头未发现以实例数组为参数、同时提供排序控制的对应接口
- **不得**声称 SDL2 后端内部不能做 instancing——那是实现层，不是 API 层

公共 API 形态差异不等于性能优越性，须同设备同负载实测对照。

---

## 5. 优化方向与优先级

| 优先级 | 方向 | 范围 | 收益来源 | 风险 | 验收 |
|---|---|---|---|---|---|
| O1 | P6 radix ping-pong + G2 早退 | glrender_instanced.c，10-25 行 | CPU 内存搬运减少 | 须保持稳定性，最终回到 items | 逐元素对照原结果 |

**O1 排第一的理由**：低风险、纯 CPU、可逐元素对照验证，不是"收益最大"。实际收益须通过 D1 排序基准测量，结果出来前不承诺任何提升比例。

| O2 | P2 instanced 批次状态外提 | glrender_instanced.c，15-35 行 | 减少冗余 GL 状态命令 | 须保持 target/viewport 语义 | 同输入图像对照，GL 调用计数 |
| O3 | P3 上传策略对照 | glrender_sprite.c + instanced.c，5-20 行原型 | 可能减少 GPU/CPU 同步 | 驱动依赖，无稳定收益则不采用 | 同负载对照耗时+图像 |
| O4 | P4/P5 查询与上下文评估 | core + 平台，15-50 行 | 减少重复平台查询 | resize/高DPI/生命周期风险 | 各平台尺寸变化测试 |
| O5 | P1 内部纹理索引 | glrender_core.c + 内部结构，30-60 行 | 若实测查找耗时显著则降成本 | 需内部映射（hash/开放寻址+槽位复用），句柄语义不变，不 breaking | 查找正确性+销毁重建+槽位复用+无效句柄+边界测试 |

**共同验收原则**：
1. 先有可复现对照基线，再谈收益
2. 不改普通 sprite draw order
3. 不加运行时 malloc，不依赖无 fallback 的 GL 扩展
4. 不以 API 调用数/顶点数/理论 draw 数冒充实测
5. 不以 Debug 构建单次 best 承诺 release 百分比收益

---

## 6. 新 bench 设计

### D：Instanced（D1 先行，为 O1 提供决策依据）

**实施顺序**：D1（排序成本）→ D2/D3（绘制）→ D4（state 排序）。先取得 D1 数据再决定是否实施 O1 radix 优化，不先实施再找证明。

- D1 排序成本：sort_instances CPU 时间、实例数、输入分布（O1 的决策依赖此项）
- D2 单纹理绘制：CPU 提交、draw call 数、GPU 时间
- D3 多纹理 run：固定总量，递增纹理数，记录 run 数/纹理绑定数/draw call 数
- D4 state 排序：opaque/translucent 各自排序成本+视觉正确性

### E：Text
- 输入字节数、换行数、实际 glyph 数、生成顶点数、纹理 run 数
- ASCII 与多字节 UTF-8 分开测（当前实现按字节处理）
- 换行、控制字节、大输入

### F：Geometry
- fill_rect / draw_line / draw_rect 分别测
- 记录 API 调用数、逻辑图元数、生成顶点数、flush 数、**实测 GL draw call 数**（不推算）

### G：FBO
- 离屏绘制与回窗口合成分开计时
- 遵守 set_target 与 clear 真实语义（clear 只清窗口并重置 target=0，不能清离屏目标）

### C：功能测试（原性能位降级）
- 同纹理不同 mode 合并 + 视觉正确 + 重叠时 draw order

### 共同记录格式
commit / OS / CPU / GPU / driver / build type / resolution / VSync / workload / sprite count / vertex count / texture count / run count / GL draw call count / warm-up / sample count / CPU submit p50/p95/best / CPU flush p50/p95/best / GPU p50/p95/best(或 MISSING) / raw samples / exit code

---

## 7. 正确性隐患（独立于性能优化）

### N1：instanced 透明纹理分类（SOURCE 机制，运行影响待测试）
- 位置：`glrender_instanced.c` `zcsr_is_translucent` + `zcsr_sort_state`；`gl_render.h` 中 `zcsr_sort_state` 注释明写 "opaque instances first (sorted by texture)"
- 机制（SOURCE）：分类只看实例字段（alpha<0.9999 或 mode∈{MIX,ADD}），不读取纹理内部像素透明信息
- 风险：alpha=1 实例配带透明像素的纹理 → 归 opaque → 可能影响重叠时的混合结果
- 复现：透明纹理（含全透明/半透明/不透明区域）+ alpha=1 实例 + 重叠 + 改变深度次序
- 性质：正确性隐患，不进性能表；机制有源码依据，实际视觉错误是否出现须实测确认

### N2：submit_instance_batch 排序契约缺口（SOURCE）
- 位置：`gl_render.h` API 注释 + `glrender_instanced.c:257-271`
- 缺口（SOURCE）：`submit_instance_batch` 和 `sort_instances` 的契约注释均未声明排序可能改变重叠实例绘制顺序；同一头文件中 `zcsr_gl_draw_instances` 明说 "drawn as-is (no sorting)"
- 与实现对照：实现按纹理排序，公共契约未充分说明排序行为
- 方向：补注释（additive，不 breaking，不改语义）；是否改 API 语义由用户决定
- 验收：头文件注释与实现一致，重叠实例+不同纹理+不同输入顺序的测试验证调用方理解契约

### N3：create 失败路径资源清理
- 位置：`glrender.c` `zcsr_gl_create` + core_init / instanced_init
- 风险：core_init 部分资源成功后后续失败，已创建的 GL 资源可能未完整清理
- 方向：Read 完整 init/destroy，列每步失败出口，失败注入测试确认释放
- 性质：资源生命周期问题，单独修复，不伪装为性能优化

---

## 8. 落地前待补齐材料（MISSING）

1. 三平台 `zcsr_gl_update_size` / `zcsr_gl_make_current` 完整实现（P4/P5）
2. `zcsr_gl_create` 完整失败路径（N3）
3. `tests/test_glrender.c` 覆盖范围
4. `bench_worst.c` 完整 flush 调用链（G1）
5. Release 构建性能基线（当前只有 Debug）

---

## 9. 建议落地顺序

1. **先修基准**：C 降级功能测试 + 补 p50/p95 + 分开 CPU/GPU 计时（G1/G3）——没有可信基准，优化收益无法验证
2. **O1 radix ping-pong**：最明确、最低风险、最容易对照
3. **O2 instanced 状态外提**：中高优先级，与 P4/P5 协同
4. **O3 上传策略对照**：实验性，无收益不采用
5. **O4/O5**：待取证后决定

每步独立 commit，独立验证，用户终审后才合入。
