# ZCSR 颜色 / 光照 / 阴影冻结契约（R0）

状态：已冻结。变更必须同时更新本文件、`docs/RENDER2D_CONTRACT.md` 和对应模块测试。

## 1. 三句话定义

1. **Path A 与 Path B 都统一在线性域**；显示域只是输入/输出编码，不是渲染运算域。两条路径都写同一个 RGBA16F 线性帧，最终只编码一次。
2. **Alpha 是 coverage，不是颜色**；不得把阴影、遮挡、AO 塞进 alpha/ARGB。
3. **阴影是逐光可控的标量 visibility**；没有直接光就没有直接投影，但 AO、烘焙阴影、贴花可以独立存在。

## 2. 颜色域

| 数据 | 输入域 | 解码点 | 存储/运算 | 输出 |
|---|---|---|---|---|
| Path A sprite/UI | display | DISPLAY 纹理采样器解码；tint/常量 shader 解码 | RGBA16F linear frame | 全帧 LUT 一次编码 |
| Path B flat albedo | display | CPU 提交边界 | G0 RGBA16F linear | 线性 composite → frame → LUT |
| Path B DISPLAY texture | display | 采样器硬件解码（sRGB8） | G0 linear | 同上 |
| Path B SRGB texture | sRGB | 采样器硬件解码 | G0 linear | 同上 |
| Path B LINEAR texture | linear | 不解码 | G0 linear | 同上 |
| Path B tint DISPLAY | display | shader 显式解码 | linear 相乘 | 同上 |
| Path B tint LINEAR | linear | 不解码 | linear 相乘 | 同上 |
| Emissive | linear HDR | 无 | G3 RGBA16F | 同上 |
| 光颜色 | linear | 无 | 光累积 RGBA16F | 同上 |

规则：

- 每个颜色通道只允许一次解码。DISPLAY 纹理由采样器解码，DISPLAY tint/常量在 shader/CPU 解码；其他组合分别解码后在线性域相乘。
- G0 是 `GL_RGBA16F` 线性 albedo，采样时没有隐式 sRGB 解码。
- 合成 `lit = albedo * (emissive + light + ambient * ambient_scale)`，再 `exposed = lit * exposure`，再 `mapped = exposed/(exposed+1)`；Light2D composite 只输出线性 `mapped`。
- Path A 与 Path B 共享线性帧和最终 LUT encode；区别只在提交模型、光照和排序。
- 法线图、遮罩、粗糙度是数据，不是颜色；必须声明 LINEAR 且不得做 sRGB 解码。

### 2.1 能力门（R5）

- `zcsr_gl_create` 在上下文 current 时探测一次并缓存：
  - `GL_MAJOR_VERSION/GL_MINOR_VERSION`（桌面 `core_33`、GLES `gles3`）；
  - `GL_MAX_COLOR_ATTACHMENTS` / `GL_MAX_DRAW_BUFFERS`（`mrt4`）；
  - 真实 1×1 `RGBA16F` FBO 完整性（`float_color_buffer`）。**统一线性帧要求它为真**：
  `zcsr_gl_create` 在 frame target 初始化时失败即返回 NULL，不允许 Path A 退回显示域直写。
  - 真实 1×1 `SRGB8_ALPHA8` FBO 完整性（`srgb8_color_buffer`）。路径 A 的窗口帧不依赖它；
  DISPLAY 离屏目标依赖它才能启用 `GL_FRAMEBUFFER_SRGB`。
- `zcsr_light2d_create` 在 `max_quality >= HIGH` 时必须同时满足
  `(core_33 || gles3) && float_color_buffer && mrt4`；否则返回 NULL 并输出
  `[light2d] deferred path requires ...`，不得留下半初始化的 G-buffer。
- 不提供“RGBA8 冒充线性 G-buffer”的自动降级：G1 position、G3 emissive 和光累积都依赖
  float，RGBA8 会静默改变几何/亮度语义。没有 float 颜色附件时，渲染器创建失败，
  不得把 LDR profile 写成“统一线性已完成”。
- `srgb8_color_buffer` 不是“可选诊断”了：它是 DISPLAY 离屏 render target 的能力门；
  窗口 Path A 仍只要求 `float_color_buffer`。
- 公开 target API 也执行同一格式门：`zcsr_gl_l2d_target_create` 只接受恰好 4 个附件，
  且格式精确为 `RGBA16F, RGBA16F, RGBA8, RGBA16F`；任何 RGBA8/SRGB8 albedo 或少于
  4 个附件在 GL 分配前直接返回 false。合成端不再替调用方“猜”格式。
  证据：`glrender_light2d_ext.c` 的 `zcsr_gl_l2d_target_create`；`test_glrender.c` 的
  负向 target 用例。
- LIT 几何入口只接受当前 FBO 是本渲染器的合法 G-buffer：
  `zcsr_gl_l2d_geo_begin` 查询 `GL_FRAMEBUFFER_BINDING`，不合法直接 false；默认
  framebuffer 下不会静默把 face 画进错误目标。证据：`zcsr_gl_l2d_gbuffer_is_bound`；
  `test_glrender.c` 的未绑定/已绑定用例。
- 颜色管线边界：Light2D 的输入 decode 固定为内置 sRGB，只有输出 `to_display`
  可替换；`zcsr_gl_l2d_set_color_pipeline` 校验 `to_linear` 与默认 sRGB 一致，否则
  拒绝。证据：`glrender_light2d_ext.c` 的 `zcsr_l2d_pipeline_input_is_srgb`；
  `test_glrender.c` 的接受/拒绝用例。

### 2.2 选择性重着色与附加层（Path A 实例）

- `zcsr_color_transform` 是实例级可选变换；零初始化等价于不改变旧 tint 行为。
- 顺序固定：旧 tint/mode → selective HSV hue remap → material-masked OKLab chroma ramp → chroma/value conditioning → additive glow → HDR emissive core → alpha-rim shell。
- selective hue 只改色相，不洗整张图：HSV 饱和度 ≤0.02 的近白/近黑像素不参与；`hue_width` 是中心半宽，
  边缘按 1.5× 羽化。源族内的相对色相按 0.15 系数压缩到目标族（上限 10°），避免绿→红时黄绿像素
  被固定角度 shift 推成洋红。这样高光和描边保持原画，主色仍落到目标色系。
- ramp 是共享 1D 图集的一行，按线性亮度采样；一个图集可同时承载火焰（黑→红→橙→黄）、冰（深蓝→青→白）、
  紫火、黑火等多条色阶。`ramp_mode=0` 是旧的全 RGB 行替换；`ramp_mode=1` 在 OKLab 中只替换 a/b，
  保留源 L，必要时用 `ramp_luma` 微量混合目标 L。**保留源 L 是图层渐变不被洗平的关键**：
  同一材质的高光、暗部、轮廓仍按原画分开，只是色度被材质梯度接管。
  `ramp` 权重还要乘以 hue 源族命中权重：紫色烟雾转冰蓝时，黄绿茎不参与 ice ramp。
  ramp 必须是 Path A DISPLAY 纹理，绑定接口 `zcsr_gl_set_color_ramp`。
- chroma/value conditioning 是低饱和源的美术补偿：`sat_boost` 把 S 推向 1、`sat_floor` 设下限，
  `value_gain` 乘 V（可为负，用于黑火压缩）、`value_contrast` 围绕 0.5 调整 V。四项零初始化即关闭；
  只有 hue 源族命中的像素参与，白色高光不会被染色。大喷菇紫雾源 S≈0.39/V≈0.53，按原版三份冰系资产统计
  H≈197°/S≈0.90/V≈0.81，落到 `sat_boost=0.85 / sat_floor=0.35 / value_gain=0.55 /
  value_contrast=0.16`，色相目标 195°，ramp 只作用于紫色族，并用 `ramp_mode=1` 保留帽体渐变。
- additive glow 在 hue/ramp/chroma 之后按 sprite alpha 叠加显示 RGB，专用于“命中 +B”和火焰/黑火的低强度光晕；
  它不替换贴图，所以轮廓、透明喷雾和明暗结构不变。
- HDR emissive 是独立于 glow 的材质层：按**源贴图的高亮核心**加权，颜色可以 >1，写入 RGBA16F 后
  由最终 `zcsr_gl_frame_encode` 做一次 HDR tonemap 再采样 LUT；不再在各 sprite shader 里 clamp。
  这解决“ADD 只有 0..1、HDR 火光会被压成白块”的问题。
- alpha-rim 是独立的霜边/能量边：从 alpha 轮廓的 1px 内外差提取边缘带，只加边缘，不覆盖内部渐变。
  硬切图只有抗锯齿边会出霜边；需要宽霜边时仍应由美术提供独立霜层/法线层。
- **原版优先**：数据侧有完整原版冰/火变体时，先在资产表解析原版 ID（如 `peashooter_ice → snow_pea`、
  `watermelon_pitcher_ice → winter_melon`、`pea_fire → fire_pea`），命中后 `recolor` 归零，不做任何推导染色。
  推导链只用于没有原版变体的单位（例如大喷菇冰雾），不能用它冒充原版质量。
- 需要“蓝果 + 绿叶”这类同色系分离时，必须由资产侧提供 fruit/leaf mask 或分层图；单靠整图 hue
  无法区分同为绿色的果实与叶子。这是已知能力边界，不假装已支持。
- 粒子火焰/黑火仍属于 VFX 层：当前已经能给出“暗底 + 紫红边缘 + HDR 自发光”的 sprite 材质，
  但独立粒子发射器、扭曲、寿命曲线仍需要单独 VFX pass。

已知边界：Path A 实例已接入；`zcsr_gl_sprite`/quad 非实例路径、Path B 和离线 `image` 尚未同构；
当前是 HSV + OKLab chroma ramp + S/V conditioning，不是严格逐 RGB 3D LUT/palette remap。

## 3. Alpha

- Alpha 只表示像素覆盖/不透明度，不参与光颜色加法。
- Path B 的 G-buffer 不做通用半透明混合；alpha < 0.5 的纹素直接 discard（cutout v1）。
- 阴影不是 alpha：阴影掩码是独立的 R8/单通道标量缓冲。
- ARGB 打包不得承载“颜色 + 阴影 + 高度”混合语义。

## 4. 光照

- 环境光 `ambient_r/g/b` 是线性 radiance；`ambient_occlusion` 只乘 ambient，不乘直接光。
- 曝光 `exposure` 是正的线性缩放，在 tonemap 前应用。
- 太阳是 scene environment 的一等平行光，不是普通局部点光：
  - 无位置、半径、衰减；
  - 方向 `dir_x/y/z` 是归一化传播方向，向下时 `dir_z < 0`；
  - 颜色 RGB 线性，`intensity` 为强度；
  - 在局部光之前提交，且不占用 Light2D 的光槽。
- LOW/MEDIUM forward 路径明确忽略太阳；HIGH/ULTRA deferred 路径消费太阳。不是静默丢弃。

## 5. 阴影

- 直接光阴影是逐光标量 visibility：`light_visibility = 1 - mask`，只乘该光的贡献。
- 没有直接光就没有直接投影。关闭太阳/点光后，其直接阴影必须一起消失。
- AO 是环境项，独立于直接光；当前只提供全局 `ambient_occlusion`，逐像素 AO 掩码尚未进入契约。
- 烘焙阴影/贴花属于材质或独立贴图层，不占用直接光阴影掩码。
- 平行光阴影长度由 `dir_z` 推导：`L = occluder_height * |dir_xy| / |dir_z|`，`shadow_length_scale` 只做产品缩放。
- `dir_z -> 0`（水平光）时长度退化为无限，使用显式上限；不得把 `dir_z` 只用于 NdotL 而让影长固定常数。
- 逐遮挡体显式 `distance > 0` 时优先于几何推导，用于墙体带状阴影。

## 6. 相机适配

- `zcsr_scene`：x/z 为地面，y 为高度。
- Light2D：x/y 为地面，z 为高度。适配为 `(x, z, y)`。
- Scene camera 支持切片：`yaw = 0`、`roll = 0`、`pitch in (0, 90]`。
- `ppu = view_h / (2 * ortho_size)`；`height_shear = cot(pitch)`；`center_x = target_x`；`center_y = target_z - cot(pitch) * target_y`。
- 2.5D shear 相机只有一个各向同性 ppu，俯视正交相机的深度透视压缩是近似；非零 yaw/roll 必须拒绝，不得静默错投。
- canonical entity quad 由 `zcsr_scene_entity_quad` 唯一生成；Path A、Path B、遮挡体不得各写一套矩形展开。

## 7. 验收

| 编号 | 断言 | 测试 |
|---|---|---|
| C-01 | 默认环境 ambient=0.03、exposure=1、AO=0、sun off | `light2d_environment` |
| C-02 | 非法 exposure/AO/负颜色/零方向被拒绝 | `light2d_environment` |
| C-03 | sun 转 parallel，无位置/衰减，方向归一化 | `light2d_environment` |
| C-04 | scene→Light2D 轴映射与 canonical quad | `scene_submit` |
| C-05 | yaw/roll 拒绝、pitch/cot/ppu 正确 | `scene_submit` |
| C-06 | 全量 engine CTest 不回归 | `ctest -C Release` |
| C-07 | 颜色文档与源码符号一致 | 人工审计 + `RENDER2D_CONTRACT.md` |
| C-08 | 线性帧灰阶往返 + 50% 黑叠白得到显示域 ~188 | `glrender` 的 `unified linear frame round-trip and linear blending` |
| C-09 | 绿→红 selective hue、白→蓝 legacy ramp、黑+蓝 glow、灰紫→高饱和青 chroma conditioning、OKLab chroma ramp 保留明暗、HDR emissive 最终 tonemap 均按预期出像素 | `glrender` 的 `selective hue remap, shared ramp atlas, additive glow, chroma conditioning` |
