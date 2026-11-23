# RENDER2D_CONTRACT.md — 2D 渲染基础契约

基线：当前工作树（GPU-only 运行时 + 保留 CPU 后端接口）；不再引用已漂移的 `1911e3b` 行号。
本文是 2D 渲染的唯一事实源；与 `LIGHT2D_INTEGRATION.md`、`LIGHT2D_MATH_MODEL.md`、`LIGHT2D_PIPELINE_MAP.md` 交叉引用，冲突时以本文 + 当前符号实现为准。源码位置用于核对，符号/函数名优先于行号。

状态标签（每条必须挂一个）：

- FROZEN：已有实现与公开承诺一致，改它要动基线，不许顺手改。
- CURRENT-LIMITED：行为明确但能力有限，限制写在同一条里。
- PROPOSED：为后续 Sprite/Tilemap/材质接入建议的目标规范，**现在没有实现**。
- UNSUPPORTED：当前明确不支持，别当有。
- UNKNOWN：源码/调用方/文档不足以判定，先挂着，不猜。

行号对应 `1911e3b` 工作树；源码改动会漂移，符号/函数名优先于行号。

---

## 0. 两条渲染路径并存（先读这个，再看下面九节）

仓库里同时存在两条互不相同的 2D 渲染提交路径，但它们共享同一个线性工作空间：

- 路径 A — 立即精灵/UI 路径：`modules/glrender/glrender_core.c` + `glrender_sprite.c` + `glrender_instanced.c`。
  屏幕像素空间，左上原点 dst rect（`include/zcsr/gl_render.h`）。纹理在采样器
  解码到线性，tint/常量在 shader 解码，按 painter order 写入 **RGBA16F 线性帧目标**，
  再按 straight-alpha 在线性域混合。**不进 Light2D G-buffer，不受光照。**
- 路径 B — Light2D 延迟光照路径：`modules/glrender/glrender_l2d_geo.c` + `modules/light2d/light2d.c`
  + `glrender_light2d_ext.c`。世界空间凸四边形进 MRT G-buffer，blend/depth 全关，
  painter's order。合成结果先写线性 composite（RGBA16F），再写同一个线性帧目标。

- [FROZEN] **全引擎统一线性工作空间**：Path A 和 Path B 都写入
  `zcsr_gl_renderer.frame_tex`（RGBA16F 线性）；`zcsr_gl_present` / `zcsr_gl_read_rgba`
  通过 `zcsr_color_pipeline` LUT 只编码一次到显示域。任何路径都不再直写默认
  framebuffer，也不在合成 FS 里各自 encode。证据：`glrender_core.c`
  `zcsr_gl_frame_bind/encode`、`glrender.c` `zcsr_gl_present/read_rgba`、
  `glrender_light2d_ext.c` composite 输出 `vec4(mapped,1.0)`。
- [CURRENT-LIMITED] 统一线性帧要求设备能创建 `RGBA16F` 颜色附件；不满足时
  `zcsr_gl_create` 直接失败，不退回显示域直写。1.0 的最低 GPU 契约因此包含
  float color buffer。
- [FROZEN] 后端选择接口：`zcsr_render_backend_available(GPU)` 为 true，
  `zcsr_render_backend_available(CPU)` 为 false；
  `zcsr_gl_create_with_backend(..., CPU)` 返回 NULL 并输出诊断。
  1.0 只有 GPU 一个可用运行时；CPU 只是同名接口位，不做第二套软件光栅器。
  两者若都存在，也必须共用同一个 RGBA16F 线性帧和同一次最终 LUT encode。
  证据：`gl_render.h` 的 `zcsr_render_backend`；`glrender.c`
  `zcsr_gl_create_with_backend`；`test_glrender.c` 的 GPU-only 断言。

`modules/sprite/sprite.c` 的 `zcsr_sprite_draw` 是个桥：把世界 (x,y) 在 wz=0 投成屏幕像素后
喂路径 A（`sprite.c:23-48`）。所以**现有地面精灵画在合成结果之上，不受点光照射**——这是现状，
不是 bug。首款游戏如果要"被光照的精灵"，必须走路径 B 的 `geo_tex_face` 显式提交世界角点，
不能指望路径 A 自动进 G-buffer。

---

## 1. 颜色空间与材质语义

- [FROZEN] 默认纹理上传 = `GL_SRGB8_ALPHA8`（`ZCSR_COLOR_SPACE_DISPLAY`），
  采样器硬件解码到线性；shader 不再对 DISPLAY texel 二次解码。需要线性数据时用
  `zcsr_gl_texture_create_ex(..., ZCSR_COLOR_SPACE_LINEAR, ...)` 上传为 `GL_RGBA8`；
  `ZCSR_COLOR_SPACE_SRGB` 同样使用 sRGB8 存储。
  证据：`glrender_core.c` 的 `zcsr_gl_texture_create_ex` / `zcsr_gl_internal_format`。
- [CURRENT] 渲染器固定纹理池为 256 个槽（`ZCSR_GL_MAX_TEXTURES`），纹理 handle→槽索引
  是 512 槽开放寻址表。池满时 `zcsr_gl_texture_create_ex` 返回 0；调用方必须显式处理上传失败。
  证据：`glrender_internal.h`、`glrender_core.c`。
- [FROZEN] Light2D 路径 B 的 Albedo 线性化点 = 几何提交边界：flat 常量由 CPU
  在提交边界显式 sRGB 解码；DISPLAY/SRGB 纹理由采样器硬件解码；LINEAR 数据不解码；
  DISPLAY tint 由 geometry shader 显式 sRGB 解码；G0 是 `GL_RGBA16F` 线性附件，
  合成 FS 采样时不再有隐式解码。
  证据：`light2d.c` 的 `zcsr_l2d_gbuffer_formats`；`glrender_l2d_geo.c` 的
  `geo_fs` / `geo_tex_fs` / `geo_nmap_fs`。
- [FROZEN] 全帧输出编码 = `zcsr_gl_present/read_rgba` 采样
  `zcsr_color_pipeline` 生成的 LUT，一次写显示域 RGBA8；采样 LUT 前先对 RGBA16F 做一次
  HDR tonemap（`m<=1` 时不改，`m>1` 时按最大通道归一压缩），再 encode。Light2D composite FS
  只输出线性结果 `vec4(mapped,1.0)`，不再自己 encode。
  证据：`glrender_core.c` `zcsr_gl_frame_encode`、`glrender_light2d_ext.c`
  `zcsr_l2d_make_deferred_program` / `zcsr_l2d_make_bloom_program`。
- [FROZEN] 路径 B 的 tint 默认仍是显示域乘子（`zcsr_gl_l2d_geo_tex_face`），
  shader 显式解码到线性后写入 G0；要线性 tint 用
  `zcsr_gl_l2d_geo_tex_face_ex(..., ZCSR_L2D_TINT_LINEAR, ...)` 配 LINEAR 源纹理。
  新增的可选 per-object 3x3 矩阵也在该 extended 入口，按行主序传入，shader 内在线性域应用。
  证据：`glrender_l2d_geo.c` `geo_tex_fs` / `geo_tex_face_impl`。
- [FROZEN] flat albedo 仍按显示域常量接收，由 CPU 在提交边界解码；G0 支持 >1，
  但显示域输入解码后被 clamp 到 1.0。3x3 矩阵输出可超过 1 并由 RGBA16F 保留。
  证据：G0 = `GL_RGBA16F`（`light2d.c` 的 `zcsr_l2d_gbuffer_formats`）；
  头文件 `gl_render_l2d_geo.h` 的 face 语义。
- [FROZEN] HDR>1 在 RGBA16F 附件上保留：G0 Albedo、G1 Position、G3 Emissive、
  光累积缓冲。
  证据：`glrender_light2d_ext.c:726`（G1/G3 用 RGBA16F）、`:1141`（光缓冲 RGBA16F）。
- [CURRENT-LIMITED] `zcsr_light.rgb` 注释声明为 linear light 颜色，`a` 为逐光强度，允许 >1。
  证据：`include/zcsr/light2d.h:39-42`。限制：进入光缓冲后仍走同一合成公式，
  不提供逐光 gamma 校正。
- [FROZEN] Emissive 当前语义：`lit = albedo_lin * (emissive + light + ambient)`——自发光先与光照相加，
  **再被 albedo 逐通道调制**，不是教科书式独立加性 `albedo*light + emissive`。
  证据：`glrender_light2d_ext.c:127`（deferred）、`:177-178`（bloom 对 E 也做 9-tap 后放进同一括号）。
- [PROPOSED] 独立加性自发光 `C = albedo_lin*(light+ambient) + emissive_lin`——未实现，触发条件=真实自发光需求，
  届时必须重跑合成回归。依据：`LIGHT2D_MATH_MODEL.md` §4.4。
- [CURRENT-LIMITED] 法线贴图已有 `zcsr_gl_l2d_geo_nmap_face`（albedo + tangent-space
  法线，TBN 在 FS 内构建）；粗糙度/金属度/遮罩图仍未支持。
  证据：`glrender_l2d_geo.c` `geo_nmap_*`。
- [FROZEN] 纹理存储格式由 `zcsr_gl_texture_create_ex` 显式选择：DISPLAY/SRGB →
  `GL_SRGB8_ALPHA8`（采样器解码），LINEAR → `GL_RGBA8`。路径 A 只接受
  `ZCSR_COLOR_SPACE_DISPLAY`；路径 B 可用 SRGB/LINEAR。默认
  `zcsr_gl_texture_create` 仍是 DISPLAY + alpha-capable，兼容旧调用。
  证据：`glrender_core.c`、`gl_render.h`。
- [FROZEN] 环境光默认 `vec3(0.03)`，可通过 `zcsr_light2d_set_environment` 改为任意
  非负线性 RGB；`ambient_occlusion` 只乘环境光，`exposure` 在 tonemap 前相乘。
  证据：`modules/light2d/light2d_environment.c`、`light2d.c` 合成 uniform。
- [FROZEN] 太阳是 scene environment 的一等平行光，不占光槽；HIGH/ULTRA 消费，
  LOW/MEDIUM 明确忽略。平行光影长由 `dir_z` 推导，`cast_shadow=false` 关闭其掩码。
  证据：`include/zcsr/light2d.h`、`light2d.c` `zcsr_l2d_draw_high`。

---

## 2. 单位、坐标轴与相机

- [FROZEN] 世界系：X 东、Y 南、Z 上；地面 z=0。几何角点、G-buffer Position、光源、遮挡体同一世界系。
  证据：`include/zcsr/camera.h:3-11`；`LIGHT2D_INTEGRATION.md` §4。
- [FROZEN] PPU = `px_per_unit`（像素/世界单位），zoom 钳位 [8, 1024]。
  证据：`camera.c:37-39`（`set_zoom` clamp）；头文件 `camera.h:23`。
- [FROZEN] `height_shear` k 是唯一权威剪切常量，任何 shader 不许写第二份。
  证据：`camera.h:8-9`；geo VS 从 uniform `u_k` 取（`glrender_l2d_geo.c:23,29`），无硬编码。
- [FROZEN] 投影公式三处一致（camera.c / geo VS / testmain 镜像），由 ctest 29 断言守着：
  `px = (x-cx)*ppu + W/2`；`py = (y-cy - k*z)*ppu + H/2`。
  证据：`camera.c:55-59`；`glrender_l2d_geo.c:28-30`；`tests/test_light2d_math.c`。
- [FROZEN] 屏幕坐标 y-down。证据：`camera.h:5`；VS 输出 `1.0 - sy/view_h*2.0`（`glrender_l2d_geo.c:30`）。
- [FROZEN] 逆变换 `px_to_world` 恒为地面 z=0 反投影；灯高不来自鼠标，独立设置。
  证据：`camera.h:31-34`；`camera.c:63-74`。已知高度 z 的通用逆变换公式见 `LIGHT2D_MATH_MODEL.md` §1.3。
- [FROZEN] 视口尺寸取调用时 GL viewport（渲染目标权威），不是相机 view 字段。
  证据：`glrender_l2d_geo.c:295` 读 `GL_VIEWPORT` 后喂 u_view；`light2d.h:63-66` 注释。
- [FROZEN] 相机活指针契约：`set_camera` 存指针不拷贝，宿主 struct 必须活到下次 set 或 destroy。
  证据：`light2d.h:59-62`。
- [FROZEN] 未 set 相机时的默认值：原点居中、128 px/unit（=一个 128px 参考瓦片）。
  证据：`light2d.h:66-68`。
- [FROZEN] UI/立即路径坐标 = 屏幕像素、左上原点，不过相机。证据：`gl_render.h:41`（dst top-left origin）；
  `zcsr_gl_draw_text` 也是屏幕像素（`gl_render.h:70-72`）。
- [CURRENT-LIMITED] "Tile 尺寸"目前不是引擎概念；唯一参考量是默认 128px/unit。
  没有 Tilemap、没有 tile-pixels-per-unit 配置。证据：全仓无 tile 模块；默认相机注释 `light2d.h:67`。
- [FROZEN] 现有 GL 精灵带显式高度入口：`zcsr_sprite_draw_at_height`；
  `zcsr_sprite_draw` 是它的 `ground_z = 0` 兼容包装，lit 路径用同名 `ground_z`。
  证据：`sprite.c`、`sprite.h`。
- [UNSUPPORTED] 相机旋转、透视、矩阵栈——camera.h 明说 "No rotation, no perspective, no matrices"。
  证据：`camera.h:4`。

---

## 3. Pivot、锚点与变换

- [FROZEN] `zcsr_transform`：x/y = 世界位置 = 旋转中心；rotation 弧度逆时针；scale_x/scale_y 独立。
  证据：`include/zcsr/transform.h:3-11`。
- [FROZEN] 现有 GL 精灵 pivot = **中心**：`dst.x = px - pw*0.5; dst.y = py - ph*0.5`。
  证据：`sprite.c:30-31`（整纹理）、`:45`（图集子矩形）。transform 的 x/y 就是精灵中心。
- [FROZEN] 软件 sprite 路径 `zcsr_rectf` = 左上原点。证据：`include/zcsr/sprite.h:10`。
  两条路径原点不同，不要混用。
- [FROZEN] Light2D geo 接口不收 pivot/局部原点——宿主自己把精灵局部坐标乘缩放旋转换成**世界角点**
  `corners_xyz[12]` 再提交。证据：`gl_render_l2d_geo.h:20-22`；geo_face 直接吃世界角点（`glrender_l2d_geo.c:351-359`）。
- [FROZEN] 旋转：整纹理路径绕中心旋转；图集子矩形用
  `zcsr_gl_queue_atlas_sprite_rot`，与整纹理路径同一旋转语义。
  证据：`sprite.c`、`glrender_sprite.c`。
- [FROZEN] 镜像/翻转：负 `scale_x/scale_y` 由 `zcsr_sprite_draw*` 转成显式
  `flip_x/flip_y`，渲染器只交换 UV，不向 GL 传负宽高；直接提交
  `zcsr_gl_sprite/atlas_sprite` 时负 dst.w/h 也按同规则归一化。
  证据：`glrender_sprite.c` `zcsr_gl_normalize_sprite_rect`。
- [UNSUPPORTED] 父子变换 / 节点层级：transform 是扁平组件数组，无矩阵栈、无 ECS。
  证据：`transform.h:3` "No manager, no ECS — callers own the arrays"。
- [UNSUPPORTED] 九宫格（9-slice）拉伸：全仓无 nine/slice 相关代码；图集 region 只有单矩形 uv0/v0/u1/v1
  （`sprite.h:43`、`texture_atlas.h:31-33`），没有可拉伸边/中心区域字段。
- [PROPOSED] 未来 Sprite Pivot 契约建议固定为：局部原点=transform.x/y（中心），脚底/左上角等锚点由宿主在
  提交角点前自己偏置；引擎不引入 pivot 枚举。理由：geo 接口已只认世界角点，再加 pivot 字段会出现两处
  负责同一换算。触发条件=首款游戏真的要脚底锚定。

---

## 4. 几何与法线

- [FROZEN] 面 = 4 个世界角点（边界顺序），内部拆 (0,1,2)+(0,2,3) 两个三角形。
  证据：`glrender_l2d_geo.c:336`（平色 idx）、`:386`（纹理 idx）；头文件 `gl_render_l2d_geo.h:20-22`。
- [FROZEN] v1 只承诺简单凸平面四边形；非凸/曲面不是契约。证据：`gl_render_l2d_geo.h:20`。
- [FROZEN] 一个面一个世界法线（整面常量），不做 per-vertex normal。证据：`glrender_l2d_geo.c:356-358`。
- [FROZEN] Normal 写入编码 `enc = n*0.5+0.5`（z 分量有值）。
  证据：平色 FS `glrender_l2d_geo.c:48`；纹理 FS `:95`。
- [FROZEN] 光照解码只读 xy：`xy = enc.xy*2-1`；xy==(0,0) → 朝上 (0,0,1)；否则 `normalize(nx,ny,0)`。
  证据：契约注释 `gl_render_light2d_ext.h:67-70`；`LIGHT2D_MATH_MODEL.md` §2.3。
- [CURRENT-LIMITED] D2：Normal.z 写进 G2 但光照时被忽略。当前正确支持 = 朝上面 + 平面内轴对齐立面；
  斜面/曲面会按平面内投影算 N·L，方向错。证据：`glrender_light2d_ext.h:67-70`；`LIGHT2D_PIPELINE_MAP.md` §5 D2 结论。
- [PROPOSED] 完整 3D 法线解码 `n = normalize(2*enc-1)`：G2 格式不用变（z 已经在写），触发条件=真实斜面需求，
  改 light FS 解码 + 放开 geo normal 校验。依据：`LIGHT2D_MATH_MODEL.md` §2.3 末段。
- [UNSUPPORTED] 切线空间、法线贴图、切线/副切线 attribute——geo VS 只声明 a_pos/a_norm/a_uv
  （`glrender_l2d_geo.c:19-20,59-61`），无 tangent。
- [FROZEN] G-buffer Position = 世界 XYZ 直传 varying，禁止 `gl_FragCoord` 反推（历史曾整层方向错）。
  证据：`glrender_l2d_geo.c:31,47`；`LIGHT2D_INTEGRATION.md` §4。
- [FROZEN] 阴影遮挡几何与渲染几何完全分离：遮挡体走 `zcsr_light2d_add_occluder` 注册凸多边形 footprint+height，
  不从渲染面/纹理 alpha 自动生成。证据：`light2d.h:151-183`；`gl_render_l2d_geo.h:89-90`。
- [CURRENT-LIMITED] ray 阴影路径消费 footprint+height（棱柱高度真实参与影子长度）；
  factor/distance/reverse/self_shadow 只被 2D 掩码路径消费，ray 路径硬 1.0。
  证据：`light2d.h:135-138` 注释；`LIGHT2D_PIPELINE_MAP.md` §7 G1 裁决。

---

## 5. 顶点格式与渲染提交

- [FROZEN] Light2D 平色顶点：6 float/vert = pos3 + normal3，stride `6*sizeof(float)`，无索引缓冲。
  证据：VBO 大小 `6*6*sizeof(float)`（`glrender_l2d_geo.c:175`）；attrib pointer `:177-180`；
  每面一次 `glDrawArrays(GL_TRIANGLES,0,6)`（`:373`）。
- [FROZEN] Light2D 纹理顶点：8 float/vert = pos3 + normal3 + uv2，stride `8*sizeof(float)`。
  证据：`:231`；attrib `:233-239`；draw `:433`。
- [FROZEN] attribute location 固定：0=vec3 a_pos，1=vec3 a_norm，2=vec2 a_uv（纹理程序）。
  证据：`glrender_l2d_geo.c:19-20`（平色 VS）、`:59-61`（纹理 VS）。
- [FROZEN] Light2D geo 路径**无 per-vertex color**：颜色走 uniform u_albedo 或 u_tint（`glrender_l2d_geo.c:39,84`）。
- [FROZEN] 立即精灵路径顶点格式是另一套：x2 + uv2 + rgba4 + mode2（`glrender_core.c:252-259`），
  与 Light2D geo 不共享 VAO/VBO/attrib 布局。两条路径各自独立，不要在契约里强行统一。
- [FROZEN] 实例化顶点 `zcsr_gl_instance` = 39 float + uint32 tex + float z + bool has_alpha = 168 字节，
  tex/z/has_alpha 是 CPU-only 排序键。其中 `recolor` 为 `zcsr_color_transform`（hue、ramp、
  S/V、glow、emissive、rim 六组标量），零初始化时等价于旧行为。证据：
  `include/zcsr/gl_render.h` 的 `zcsr_gl_instance`、`include/zcsr/color.h` 的 `zcsr_color_transform`。
- [FROZEN] Path A 实例的着色顺序固定为：旧 tint/mode（MULTIPLY/MIX/ADD）→ selective HSV hue
  remap → shared 1D ramp（按线性 luma，只对 hue 源族命中的像素加权；mode 1 只替换 OKLab a/b，
  保留源 L）→ chroma/value conditioning（S boost/floor、V gain/contrast，同样只对 hue 源族命中的像素加权）
  → additive glow → HDR emissive core → alpha-rim shell。
  hue/ramp/chroma/glow/emissive/rim 只在显式 amount>0 时参与；任一阶段开启都不得走
  multiply-only 快路径。证据：`modules/glrender/glrender_instanced.c` 的实例 fragment shader。
- [FROZEN] ramp 有两种模式：mode 0 是整个 RGB 行替换（legacy）；mode 1 是材质遮罩的
  OKLab 色度梯度，只替换 a/b 并按 `ramp_luma` 可选混合 L。全 RGB 替换会覆盖图层明暗，
  不能用于“保留原画渐变”的材质。ramp 权重额外乘以 hue 命中权重，避免把非同色系的
  绿叶/茎一起洗成目标色；chroma/value 同样按命中权重混合。ramp 绑定是 `zcsr_gl_set_color_ramp`，
  必须是 Path A DISPLAY 纹理，`rows` 读取图集高度内的行。
  证据：同上的 `apply_recolor` 与 `zcsr_gl_set_color_ramp`。
- [FROZEN] HDR 只在最终显示编码点收一次：Path A 的 emissive 允许写入 >1 的 RGBA16F，
  `zcsr_gl_frame_encode` 在采样颜色 LUT 前做 `max(0)` + 单次 HDR tonemap，
  不再在各 sprite shader 里 clamp。证据：`glrender_core.c` 的 `display_encode`。
- [CURRENT-LIMITED] Light2D geo 路径 = 每面一次 draw call，无合并、无实例化、无跨面批处理。
  证据：`glrender_l2d_geo.c:373,433` 每面独立 draw。
- [FROZEN] 立即精灵路径批处理：同纹理图集 sprite 合并成一次 draw；实例化路径按纹理 run 分组。
  证据：`gl_render.h:88-90`、`:110-114`、`:127`（zcsr_sort_instances）。
- [UNSUPPORTED] Sprite/Instance 直接进 G-buffer——geo program 是单输出 MRT program，
  没有"立即精灵顺手写四附件"的入口。证据：`LIGHT2D_INTEGRATION.md` §7；`glrender_l2d_geo.c:41-44` 只写 Light2D 四附件。
- [FROZEN] 批处理拆批规则（Light2D 侧）：换纹理 → 必须重新绑 unit0；换相机 uniform 在 geo_begin 一次性设好，
  期间不动。证据：`glrender_l2d_geo.c:428-429`（每面绑纹理）、`:296-324`（begin 设两 program uniform）。

---

## 6. UV、图集与纹理采样

- [FROZEN] UV 原点：(u0,v0) = 子矩形**图像左上**，v 向图像底增长；角点 0 收 (u0,v0)。
  证据：`gl_render_l2d_geo.h:71-74`；顶点打包 `glrender_l2d_geo.c:415-416`。
- [FROZEN] 不翻转 V：运行证据 rug A/B 探针证伪翻转假设。依据：`LIGHT2D_INTEGRATION.md` §5、§8 探针表。
- [FROZEN] 图集打包 = CPU shelf-pack（next-fit decreasing height），图间留 1px gutter 防 bilinear bleed。
  证据：`include/zcsr/texture_atlas.h:36`、`:42-45`。
- [FROZEN] 图集 region = 归一化 uv0/v0/u1/v1（0,0,1,1 = 整张图）。
  证据：`sprite.h:43`；`texture_atlas.h:31-33`。
- [UNSUPPORTED] 旋转图块（atlas 内 90° 旋转打包）：atlas 打包只做轴对齐 shelf placement，无旋转位。
  证据：`texture_atlas.h:25-28` placement 只有 x/y/w/h。
- [FROZEN] 图集子矩形 + 旋转：`zcsr_gl_queue_atlas_sprite_rot(..., rotation, ..., flip_x, flip_y)`
  与整纹理 `zcsr_gl_submit` 同语义；旧 `zcsr_gl_queue_atlas_sprite` 是 rotation=0、无 flip 的兼容包装。
- [FROZEN] 过滤：创建默认 NEAREST，`zcsr_gl_texture_filter` 可切 LINEAR。
  证据：`glrender_core.c:309-310`（默认创建）、`:322-323`（filter 接口）。
- [FROZEN] Wrap：全部 `GL_CLAMP_TO_EDGE`，无 REPEAT/MIRROR。
  证据：`glrender_core.c:311-312`、`glrender_light2d_ext.c:419-420,1107-1108,1139-1140`。
- [CURRENT-LIMITED] mipmap：`zcsr_gl_texture_create_ex` 在 `tex_quality >= 2` 时生成
  mip 链并用 `GL_LINEAR_MIPMAP_LINEAR`；`zcsr_gl_texture_upload` 重传后会重建，
  生成失败则回退双线性而不是读旧链。`zcsr_gl_texture_filter` 仍可显式切 NEAREST/LINEAR。
  证据：`glrender_core.c` `zcsr_gl_generate_mips`。
- [UNKNOWN] 像素对齐（pixel snapping / 整数 PPU 对齐）：无任何 snap 代码或注释，要做必须新立契约。
- [UNSUPPORTED] 九宫格多区域 UV：见 §3。
- [CURRENT-LIMITED] atlas padding 只有 1px gutter（打包期固定），不支持运行时可调 inset。
  证据：`texture_atlas.h:36`。
- [UNSUPPORTED] 引擎不提供 `GL_TEXTURE_2D_ARRAY` / bindless / 多纹理数组采样路径。图集页（atlas page）
  是资产管线责任：同一页的所有 clip 共用一个 `zcsr_gl_texture`，`zcsr_gl_instance` 的 u0/v0/u1/v1
  直接指向页内子矩形；这样 `zcsr_gl_submit_sorted_runs` 只需一次共享状态设置，按 run 绑纹理并 draw。
  PVZ 的 31 张独立动画纹理若希望减少 draw call，应先在资产侧打成图集页，而不是等待引擎 texture array。

---

## 7. 纹理资源与数据图语义

- [FROZEN] 默认纹理上传 = 8-bit RGBA 源数据，DISPLAY 内部格式
  `GL_SRGB8_ALPHA8`，采样器解码；LINEAR 内部格式 `GL_RGBA8`，原样采样。
  需要显式色彩空间时使用 `zcsr_gl_texture_create_ex`，不能靠默认入口猜。
  **Path A 与 Path B 共享这套解码契约；区别只在提交模型和光照。**
  证据：`glrender_core.c` `zcsr_gl_texture_create_ex` / `zcsr_gl_internal_format`。
- [CONTRACT] “0 堆”范围：引擎 renderer 状态、实例/顶点批、arena 都在调用方提供的固定缓冲区；
  `zcsr_arena_alloc_count` 只统计引擎 arena 的成功分配，不统计 libc malloc、GL 驱动或操作系统内部分配。
  需要严格拆 CPU/GPU 时用 `zcsr_gl_gpu_timer_begin/end/last_ms`（桌面 GL 3.3；Android 返回 false）。
- [FROZEN] 全引擎颜色图处理：Path A 和 Path B 都写入 RGBA16F 线性帧；
  纹理在采样器完成唯一次解码（DISPLAY/SRGB 用 sRGB8 sampler，LINEAR 直通），
  DISPLAY tint/常量在 shader 或 CPU 解码。最终由 `zcsr_gl_present/read_rgba`
  的 LUT 一次编码。路径 A 的立即 Sprite/UI 也遵守这条，不再直写显示域。
  上传入口用 `zcsr_gl_texture_create_ex` 显式区分 DISPLAY/SRGB/LINEAR；数据图
  （法线等）必须用 LINEAR，不能当颜色图。
  证据：`zcsr_gl_texture_create_ex`、`glrender_core.c` `zcsr_gl_frame_encode`。
- [FROZEN] G-buffer 附件格式表：
  - G0 Albedo = `GL_RGBA16F`（线性写入，采样无隐式解码）
  - G1 Position = RGBA16F（`:726`）
  - G2 Normal = RGBA8（`:724`）
  - G3 Emissive = RGBA16F（`:726`）
  枚举同步在 `gl_render_light2d_ext.h:22-25`；实际创建在 `light2d.c` 的
  `zcsr_l2d_gbuffer_formats`。
- [FROZEN] 阴影掩码 = R8 单通道；光累积 = RGBA16F。证据：`glrender_light2d_ext.c:1109`、`:1141`。
- [FROZEN] 清屏值：Albedo/Emissive=0；Position=(0,0,65504,1) 远哨兵（空气不受光）；Normal=(0.5,0.5,0,1) 中性朝上。
  证据：`glrender_light2d_ext.c:861-866`、`:881-883`。
- [UNSUPPORTED] 普通纹理路径的浮点格式 / 压缩格式（BC/ETC/ASTC）：上传入口只接受 uint8 RGBA
  （`gl_render.h:36-37`）。
- [FROZEN] 纹理句柄所有权：宿主 `zcsr_gl_texture_create`/`destroy` 拥有；geo 调用期间只借绑 unit0，不持有。
  证据：`gl_render_l2d_geo.h:67-69`；`LIGHT2D_INTEGRATION.md` §3。
- [FROZEN] 无效句柄（0 或已销毁）→ `tex_face` 在任何 GL 调用前返回 false，无崩溃无副作用。
  证据：`glrender_l2d_geo.c:393-394`（`zcsr_gl_find_tex` 返回 NULL/0 → false）。
- [FROZEN] CPU 侧资源缓存：`zcsr_texture_cache` 按 PNG 路径去重，同路径返回同一 `zcsr_texture*`。
  证据：`include/zcsr/texture.h:17-19`。注意这是 CPU 图像缓存，不是 GPU 纹理去重。
- [FROZEN] 子区域上传 `zcsr_gl_texture_upload_region`（公开头文件 `gl_render.h:191-195` 有声明，
  旧 API 摘要漏列，以头文件为准）。实际 GL 状态行为（`glrender_sprite.c:246-252`）：
  上传前 `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)`、`GL_UNPACK_ROW_LENGTH = src_stride/4`；
  上传后把 `GL_UNPACK_ROW_LENGTH` 设回 0——**不保存、也不恢复调用前原值**；
  `GL_UNPACK_ALIGNMENT=1` 也不还原。`src_stride % 4 != 0` 或平台无 `GL_UNPACK_ROW_LENGTH` 时退化为
  逐行 `glTexSubImage2D`（`:254-263`），不碰 ROW_LENGTH。调用方若此前设过非零 ROW_LENGTH 或
  非 1 ALIGNMENT，本函数不会替你恢复。
- [FROZEN] 线性数据纹理已有受支持入口：`zcsr_gl_texture_create_ex(..., ZCSR_COLOR_SPACE_LINEAR, ...)`
  上传为 `GL_RGBA8` 线性数据；`zcsr_gl_l2d_geo_nmap_face` 可按切线空间法线图提交到 G2。
  证据：`gl_render.h` 的 `ZCSR_COLOR_SPACE_LINEAR`；`gl_render_l2d_geo.h` 的 `nmap_face`。
- [PROPOSED] 法线图之外的额外线性数据纹理（粗糙度/遮罩/分层材质）尚未定义独立
  G-buffer 通道或上传约定。触发条件=首款游戏真的需要这些材质通道。

---

## 8. 深度、排序与透明混合

- [FROZEN] G-buffer 无深度附件；geo 阶段 `GL_DEPTH_TEST` 强制关闭。
  证据：`glrender_l2d_geo.c:292`（disable）、`:447`（恢复）；头文件 `gl_render_l2d_geo.h:37-42`。
- [FROZEN] geo 阶段 `GL_BLEND` 强制关闭——写 G-buffer 是覆盖写，不混合。
  证据：`glrender_l2d_geo.c:291`、`:446`。
- [FROZEN] geo 阶段 `GL_CULL_FACE` 关闭，绕序由宿主负责。证据：`:293`、`:448`。
- [FROZEN] Painter's order 责任在宿主：远到近提交不穿插凸面。
  证据：`gl_render_l2d_geo.h:37-39`。
- [FROZEN] 排序参考键 `q = height_shear*(y-cy) + z`，升序提交；`world_z` 单独不是通用深度。
  scene 路径先把 scene(x,y,z) 映射为 Light2D(x,z,y)：scene `y` 是高度、scene `z` 是地面轴，
  `zcsr_scene_build_lit_order` 用四角平均 q；`zcsr_scene_build_order` 的 Path A 键是四角
  平均相机眼距，降序提交。
  证据：`gl_render_l2d_geo.h:40-42`；`scene_submit.c` 的 `scene_entity_lit_q` /
  `entity_camera_depth`；`test_scene.c` / `test_scene_submit.c` 的同 layer/order 深度与 q
  测试。
- [FROZEN] alpha-cutout 唯一规则：`if (rgba.a < 0.5) discard;`（严格小于；作用于 texel*tint 乘积）。
  证据：`glrender_l2d_geo.c:92`。阈值 0.5、比较符 `<`、先 discard 再写四附件——被丢片元对所有附件零写入。
- [UNSUPPORTED] Light2D G-buffer 路径的任意半透明混合 / 排序：cutout 之外没有半透明通道。
  证据：`glrender_l2d_geo.c:291` blend 关；`LIGHT2D_INTEGRATION.md` §7。
- [FROZEN] 路径 A（立即精灵）有 straight alpha 混合：
  `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`，混合发生在 RGBA16F
  线性帧内，因此 50% 黑叠白得到线性 0.5、显示域约 188，而不是旧显示域的 128。
  证据：`glrender_core.c` 初始化 blend + `zcsr_gl_frame_bind`；`test_glrender.c`
  的 `unified linear frame round-trip and linear blending` 断言。
- [FROZEN] 实例透明分类只看 instance 字段：alpha<1.0 或 mode=MIX/ADD 视为透明；**不读 GPU 纹理像素 alpha**。
  纹理带透明像素时宿主必须自己把实例标透明。证据：`gl_render.h:143-149`。
- [FROZEN] 实例排序：opaque 按纹理 run 合并 draw；translucent 按 z 远到近（大 z=远=先画）。
  证据：`gl_render.h:150-157`（`zcsr_sort_state`）。
- [UNSUPPORTED] Premultiplied alpha：混合公式是 straight `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`，
  无 `ONE, ONE_MINUS_SRC1_ALPHA` 之类 premul 路径。证据：`glrender_core.c:265`。
- [FROZEN] 光累积混合 = `GL_ONE, GL_ONE`（真加，即使 app 留了别的 equation 也强制 FUNC_ADD）。
  证据：`glrender_light2d_ext.c:1001-1002`。
- [FROZEN] 阴影掩码混合 = `GL_MAX`。证据：`glrender_light2d_ext.c:1201`。
- [FROZEN] 渲染阶段固定顺序：
  begin_geometry → 宿主提交世界几何 → begin_lighting → add_light×N → end_lighting
  （合成到线性 frame）→ 宿主 HUD/路径 A 精灵（同一个线性 frame）→ flush →
  present/read_rgba（唯一一次 LUT encode）。
  证据：`light2d.h`；`glrender_light2d_ext.c` `zcsr_gl_l2d_finalize_composite`；
  `glrender.c` `zcsr_gl_present`。
- [CURRENT-LIMITED] UI 没有独立层概念：HUD 就是 end_lighting 之后用路径 A
  在同一个线性帧上画的屏幕像素，最后统一 encode。证据：`glrender_core.c`
  `zcsr_gl_frame_encode`。

---

## 9. 生命周期、状态与批处理

- [FROZEN] renderer 本体住在宿主 caller buffer 里（无堆），`zcsr_gl_create`/`zcsr_gl_destroy` 成对。
  证据：`gl_render.h:23-27`。
- [FROZEN] G-buffer MRT 由 light2d ctx 拥有，resize 必须事务式：失败保留旧目标可用。
  证据：`light2d.h:103-107`；`gl_render_light2d_ext.h:47-48`。
- [CURRENT-LIMITED] resize/FBO 重建失败回退 = 接口承诺事务式，但**没有真实场景运行证据**，已登记为 v1.1 未验证项。
  依据：`LIGHT2D_INTEGRATION.md` v1.1 冻结节"未验证"。
- [FROZEN] geo program/VAO/VBO×2 是 renderer 内部资源，首次 `geo_begin` 惰性创建，
  `zcsr_gl_core_destroy` 统一释放，宿主不创建不销毁。
  证据：`glrender_l2d_geo.c:124-129`（ensure 门卫）、`:250-265`（cleanup）；`LIGHT2D_INTEGRATION.md` §3。
- [FROZEN] v1.1 R1：惰性初始化要求平色+纹理两 program 同时成功，任一失败整组 cleanup 回滚、下次 begin 重试，
  杜绝"部分初始化后 tex_face 静默无渲染"。证据：`glrender_l2d_geo.c:130-132`、`:190`、`:197`、`:215`。
- [FROZEN] GL 状态保存/恢复精确配对：begin 存 program/VAO/ARRAY_BUFFER/blend/depth/cull/ACTIVE_TEXTURE/unit0 绑定，
  end 逐项还原。证据：保存 `glrender_l2d_geo.c:280-289`；恢复 `:440-448`。
- [FROZEN] geo begin/end 不碰 FBO、不改 draw-buffer、不写 viewport（只读 viewport 喂 uniform）。
  证据：`gl_render_l2d_geo.h:44-46`；`glrender_l2d_geo.c:295` 只读。
- [FROZEN] 失败语义：begin 惰性失败/嵌套 begin/相机参数非法 → 返回 false 且不置 active；
  非 active 时 face/tex_face/end 一律 false；顺序错误不破坏 GL 状态。
  证据：`glrender_l2d_geo.c:270-276`（门卫）、`:338,389,438`（非 active false）。
- [CURRENT-LIMITED] "只在 light2d geometry 阶段内调用 geo API" 是文档契约，**无机制强制**——
  错相位调用会画进当时绑定的任意 FBO。接入者自律。证据：`LIGHT2D_INTEGRATION.md` §3。
- [FROZEN] 相机所有权：宿主栈/成员变量持有，活指针，生命周期须覆盖 ctx 使用期。
  证据：`light2d.h:60-62`。
- [FROZEN] 遮挡体数据：宿主调用方缓冲持有，`add_occluder` 期间拷贝进 ctx 列表，
  `clear_occluders`/ctx destroy 释放。容量上限 64 个多边形 × 16 顶点。
  证据：`light2d.h:167-183`。
- [FROZEN] 质量档位：只实现 LOW/HIGH，MEDIUM/ULTRA 在 set_quality 时返回 false 不动当前档。
  证据：`light2d.h:83-87,109-112`。
- [FROZEN] 光数量无上限（数组上限已移除），多光 ADD 交换律无排序问题。
  证据：`LIGHT2D_PIPELINE_MAP.md` §6。
- [PROPOSED] Sprite/Tilemap 与 Light2D 的桥接应放在显式适配层（宿主或独立模块），
  不改写 geo/light2d 核心契约；触发条件是首款游戏出现需要被光照的 Sprite/Tilemap。

---

## 10. 跨模块交界（谁负责哪层换算，不许重复）

- 局部 → 世界：宿主/适配层负责（pivot/scale/rotate 全在这一层做完），提交世界角点。证据：§3 FROZEN 条。
- 世界 → 屏幕：唯一权威 `zcsr_camera` + `height_shear`；geo VS 经 u_k/u_ppu 同式。证据：§2。
- UV ↔ 翻转/图集：图集 region 是归一化子矩形，uv 直接交给 FS；翻转靠宿主改 uv 角点顺序，引擎不做。
  证据：`gl_render_l2d_geo.c:415-416` 直接透传 uv_st。
- 法线空间：当前只在世界空间；局部→世界法线变换、切线空间都没有。证据：§4。
- 颜色图/数据图 → G-buffer：geo FS 写四附件；数据图未来走独立入口，不混 G0 gamma 路径。证据：§7。
- G-buffer → 光照 → 合成：light FS 采 G1/G2/Shadow 进光缓冲，deferred/bloom FS 采 G0/G3/光缓冲出窗口。
  证据：`glrender_light2d_ext.c:116-129`。
- 世界几何 / 透明特效 / UI：世界几何走 G-buffer（路径 B）；透明特效与 UI 走路径 A 屏幕像素，
  **不自动混入 G-buffer**。证据：§0、§8。
- 所有权：纹理=宿主；相机=宿主；遮挡体=宿主缓冲；geo 内部 program/VAO/VBO=renderer；G-buffer=ctx。
  证据：§9 各条。

---

## 11. 首款游戏最小支持基线（v1.1 冻结口径）

首款 2D 游戏**只允许依赖**下面这些已落地项：

1. 共享 `zcsr_camera`（center/ppu/height_shear），世界系 X 东 Y 南 Z 上、地面 z=0。
2. 平色凸四边形（`geo_face`）+ 纹理凸四边形（`geo_tex_face`）进 G-buffer，轴对齐法线。
3. 颜色语义：Path A/B 都写 RGBA16F 线性帧；纹理在采样器解码，tint/常量按域解码，
   最终 `present/read_rgba` 用 `zcsr_color_pipeline` LUT 一次编码；alpha-cutout <0.5。
4. 点光（高度/半径/decay/逐光强度）+ ray 阴影（间接 GPU 证据，非同像素因果）。
5. Painter's order：宿主按 q 键远到近提交，不穿插。
6. 固定渲染目标 1280×720 已验证；动态 resize 不做保证。
7. HUD/屏幕 UI/透明精灵走路径 A（straight alpha，屏幕像素），写入同一线性帧，
   不要求光照；最终与 Path B 一起 encode。

**不要依赖**（v1.1 明确未验证或不支持，详见各节标签）：

- 平行光完整场景运行；2D 掩码阴影独立视觉验收。
- 任意半透明混合进入 Light2D G-buffer。
- 斜面/曲面法线、法线贴图、切线空间。
- Sprite 实例直进 G-buffer、图集旋转、九宫格、mipmap、premultiplied alpha。
- 动态 resize / FBO 失败回退的实际行为。
- GPU 射线阴影的同像素因果证明（ENV-BLOCKED，已登记）。
- nl 左右不对称根因（已登记，不追查）。

扩展规则：未来 Sprite/Tilemap/新材质格式一律**扩本契约**，由显式适配层接入，不改 Light2D 核心约定。

---

## 12. 状态统计

各领域状态计数（按本文实际条目逐条数，FROZEN / CURRENT-LIMITED / PROPOSED / UNSUPPORTED / UNKNOWN）：

- §0 路径模型：3 / 1 / 0 / 0 / 0
- §1 颜色空间与材质：8 / 1 / 1 / 2 / 0
- §2 单位相机：10 / 2 / 0 / 1 / 0
- §3 Pivot 变换：4 / 1 / 1 / 2 / 1
- §4 几何法线：7 / 2 / 1 / 1 / 0
- §5 顶点提交：8 / 1 / 0 / 1 / 0
- §6 UV 图集：6 / 1 / 0 / 4 / 1
- §7 纹理资源：9 / 0 / 1 / 1 / 0
- §8 深度混合：12 / 1 / 0 / 2 / 0
- §9 生命周期：11 / 2 / 1 / 0 / 0

合计：FROZEN 78 / CURRENT-LIMITED 12 / PROPOSED 5 / UNSUPPORTED 14 / UNKNOWN 2。
UNKNOWN 两项（负 scale 镜像、像素对齐）源码无证据，未猜。

---

## 13. 源码核对补记

以下条目来自 `modules/glrender/glrender_light2d_ext.c` 的 light FS 逐行核对，
状态一律 FROZEN（已读代码，非运行证据）。这一节只补精度，不改 §1-§11 的既有结论。

- [FROZEN] 衰减公式里的 "1.0" 不是硬编码常数，是 uniform `u_dist_scale`：
  `atten *= 1.0 / (dist / max(u_dist_scale, 1e-4) + 1.0)`（`:324`）；半径也带门卫
  `r = dist / max(u_radius, 1e-4)`（`:322`）。旧文档写的 "1.0=一个参考瓦片" 是它的默认语义，
  实际可配置。`u_radius` 为 0 时不会除零。
- [FROZEN] dist=0 保护：`ldir = (tl < 1e-6) ? vec3(0,0,1) : to_light/tl`（`:336`）；
  法线零长保护 `n = (dot(n_raw,n_raw) < 1e-6) ? up : normalize(n_raw,0)`（`:333-334`）。
  此前标为 UNKNOWN 的两个除零点都已在 shader 内保护，不会出 NaN。
- [FROZEN] 法线解码口径：`n_raw = texture(u_Normal).xy * 2.0 - 1.0`（`:332`）。
  "xy=(0,0) 取朝上"指的是**解码后**的 n_raw=(0,0)，对应 G2 存储值 (0.5,0.5)（=清屏值）。
  不是存储值 (0,0)。此前的歧义已澄清。
- [FROZEN] ray slab 测试完整式：d = P - L（光→片元方向）；逐轴 t_enter/t_exit 取 max/min，
  z slab 范围是 `[0, hgt]`（地面到遮挡顶，不是 `[L.z, L.z+H]`，`:295-301`）；
  某轴 |d|<1e-6 时判 L 是否落在该轴 slab 内，不在则 `continue` 跳过该盒（`:280,288,296`）；
  相交判据 `t0 < t1`（`:303`）。t 初值窗口 `(1e-4, 1-1e-4)`（`:277-278`）。
- [FROZEN] **ray 路径遮挡盒硬上限 = 8**：`u_Boxes[16]`（2 vec4/盒），FS 循环 `i<u_BoxCount` 但数组只够 8 盒
  （`:273-275`）。虽然 `zcsr_light2d_add_occluder` 注册容量是 64（light2d.h:170），
  ray 路径上传时超过 8 盒的部分被截断——这是 §9 遮挡体条目的隐藏限制，ray 模式下宿主最多注册 8 个。
- [FROZEN] 2D 掩码 5-tap：`bias = vec2(1.5,1.5) / shadow_res`，中心 + 4 个对角偏移 ±bias 取 MAX
  （`:345-350`）。不是 5-tap 网格，是中心+四角。
- [FROZEN] 阴影可见性怎么进光：`o_light = u_color * (u_gain * atten * nl * (1.0 - shadow))`（`:353`）。
  ray 命中时 shadow=1；掩码路径 shadow=R8 采样值。`u_use_shadow=0` 时整段跳过，shadow=0。
- [FROZEN] 路径选择精确表（`:317-325, :341-351`）：
  `u_parallel != 0` → 无衰减、to_light=-u_dir3、走掩码（5-tap）；
  点光 + `u_ray_shadows != 0` → slab 射线、不走 5-tap；
  点光 + `u_ray_shadows == 0` → 掩码 5-tap。
- [FROZEN] height_shear k 量纲：投影式 `sy = H/2 + (y-cy-k*z)*ppu`，k*z 与 y-cy 同量纲（世界 y 单位），
  所以 k 是**无量纲比值**（世界 y 单位 / 世界 z 单位），k*ppu 才是每世界 z 的屏幕像素位移。
  不是 PPU 单位、不是像素。证据：`camera.c:58`。
- [FROZEN] G-buffer UV 采样翻转：light FS 用 `gb_uv = (v.x/view.x, 1.0 - v.y/view.y)` 采样
  （`:309`），因为屏幕 y-down 而 G-buffer 纹理顶行在 v=1。这条不影响宿主提交 UV，
  是引擎内部采样约定，§6 UV 条对宿主仍然成立。

未在本节关闭、仍保持 UNKNOWN 的：负 scale 镜像、pixel snapping（§3/§6 原两条不动）。
其余条款已在 §1/§2/§8/§11 写明，不再重复。

---

## 14. 渲染三层模型与 Sprite→G-buffer 适配接口（v1.3）

本节是给游戏开发者看的接入层。§1-§13 是引擎内部审计，本节把它们按"游戏能改什么 /
游戏不能改什么 / 游戏自己要做什么"重新分三层。原条款不删，只分层。

### 14.1 三层定义

- **CORE（定死，游戏不可改）**：跨模块共同规则。游戏换个值可以，换公式不行。
- **MODULE-CFG（游戏可调旋钮）**：现有公开 API 已经暴露的参数。改值不改规则。
- **ADAPTER（宿主自己负责）**：把游戏对象换算成引擎认识的数据。引擎不替你做这步。

### 14.2 CORE 冻结清单（5 条，游戏不可改）

1. 世界系 + 唯一投影公式：X 东/Y 南/Z 上、`zcsr_camera` 活指针、PPU、height_shear、
   viewport 取 GL viewport。证据：§2。
2. G-buffer 四附件布局：G0 RGBA16F 线性 albedo / G1 RGBA16F 世界XYZ /
   G2 RGBA8 编码法线 / G3 RGBA16F emissive + 清屏值。证据：§7。
3. 帧序与 GL 状态配对：begin_geometry→geo→begin_lighting→add_light→end_lighting→HUD；
   geo begin/end 精确保存恢复。证据：§9。
4. 路径 A/B 边界：Path A 屏幕像素 straight-alpha 不受光，Path B 世界几何进 G-buffer 受光；
   两者都写同一个 RGBA16F 线性帧，但提交模型不互通。证据：§0、§8。
5. 全帧颜色语义：Path A/B 都只写线性值；纹理采样器解码，tint/常量按域解码；
   `present/read_rgba` 通过 `zcsr_color_pipeline` 的 LUT 一次 encode。
   Light2D composite FS 不再单独 encode。证据：§1。

### 14.3 MODULE-CFG 现有旋钮（游戏可调，值来自现有 API）

- 相机：center_x/center_y、px_per_unit（clamp [8,1024]）、height_shear。证据：`camera.h:12-17`。
- 光：x/y/height/radius/decay/r/g/b/a、type(POINT/PARALLEL)、dir_xyz。证据：`light2d.h:44-56`。
- 阴影/质量：shadows/normals/gain/light_scale_pct/bloom/ray_shadows。证据：`light2d.h:139-146`。
- 纹理：`zcsr_gl_texture_filter` 切 NEAREST/LINEAR。证据：`gl_render.h:38`。
- tint：`geo_tex_face` 的 tint_rgba（显示域乘）。证据：`gl_render_l2d_geo.h:75-78`。
- 遮挡体：add/clear_occluder，容量 64×16 顶点；ray 模式下实际生效上限 8。证据：§13。
- 注意：上述旋钮里 PARALLEL、ray 阴影、2D 掩码、resize 都有未验证标记（§11），
  游戏可调但不构成完整运行保证。

### 14.4 ADAPTER：Sprite→G-buffer 适配接口（本期定稿，只写契约不写实现）

输入（全部用现有类型，不新造结构）：
- `zcsr_transform t`：x/y 世界位置、rotation 弧度、scale_x/scale_y。证据：`transform.h:6-11`。
- `zcsr_sprite_asset a`：tex、u0/v0/u1/v1、w/h（**世界单位 @scale=1**）。证据：`sprite.h:41-45`。
- `tint_rgba[4]`：显示域乘子。
- `ground_z`：精灵平面离地高度（世界单位）。**B 方案：这个值由游戏传，不写死 0**。

尺寸：
```
half_w = a.w * t.scale_x * 0.5
half_h = a.h * t.scale_y * 0.5
```
单位=世界单位。不读纹理像素宽高做尺寸——尺寸只能来自 asset.w/h × scale。

Pivot：中心。t.x/t.y 就是精灵中心，不引入 pivot 枚举。

局部→世界四角（唯一实现口径）：
```
局部四点（绕原点）：
  L0 = (-half_w, -half_h)
  L1 = ( half_w, -half_h)
  L2 = ( half_w,  half_h)
  L3 = (-half_w,  half_h)
旋转 c=cos(t.rotation), s=sin(t.rotation)：
  R(x,y) = (x*c - y*s, x*s + y*c)
世界角点（corners_xyz[12]，顺序 L0,L1,L2,L3）：
  P0 = (R(L0).x + t.x, R(L0).y + t.y, ground_z)
  P1 = (R(L1).x + t.x, R(L1).y + t.y, ground_z)
  P2 = (R(L2).x + t.x, R(L2).y + t.y, ground_z)
  P3 = (R(L3).x + t.x, R(L3).y + t.y, ground_z)
```
拆三角 (0,1,2)+(0,2,3)，正好对上 `geo_tex_face` 边界序。证据：`glrender_l2d_geo.h:20-22`。

UV 透传（按角点顺序，不翻转）：
```
uv_st[8] = (a.u0,a.v0), (a.u1,a.v0), (a.u1,a.v1), (a.u0,a.v1)
```

法线：无 normal map 时地面/水平精灵统一 `(0,0,1)`；立面/斜面可通过
`zcsr_gl_l2d_geo_nmap_face` 提交切线空间法线，逐材质额外 normal 覆盖层仍在 §14.5。

提交：
- 受光精灵（常量法线）→ `zcsr_gl_l2d_geo_tex_face(r, a.tex, corners_xyz, uv_st, tint, (0,0,1), emissive)`。
- 受光精灵（法线图）→ `zcsr_gl_l2d_geo_nmap_face(...)`，输入切线空间法线图。
- HUD、屏幕 UI、不透明背景 → 继续走路径 A（`zcsr_sprite_draw`/`zcsr_gl_submit`），不进 G-buffer。
- 透明特效（半透明叠加）→ 路径 A；当前路径 B 不支持半透明。证据：§8。
- 排序：受光精灵之间仍由宿主按 q 键远到近提交（§8）。

### 14.5 第二层扩展槽位（未来可 add，不改 CORE）

这些位置现在留接口、不实现。未来第一款游戏真需要时，在 ADAPTER 层加，不动 §14.2 的 CORE：

- normal 覆盖：基础法线图已由 `zcsr_gl_l2d_geo_nmap_face` 支持（见 §1）；本槽位
  仅指逐材质额外 normal 覆盖层，仍未定（PROPOSED，触发=墙面精灵需求）。
- emissive 材质参数：除 uniform emissive 外的逐精灵发光（PROPOSED，当前 G3 是逐 draw uniform）。
- tint 模式：MIX/ADD 闪烁（路径 A 已有 `zcsr_color_mode`，路径 B 未接；PROPOSED）。
- 纹理样式扩展：normal map 已存在；遮罩图/粗糙度图等额外数据纹理仍走独立上传入口，
  不混 G0 gamma 路径（§7）。
- ground_z 非 0 悬浮精灵：接口已留，当前几何上和贴地等价，不需要特殊处理。

**不开放的扩展**：tilemap、动画帧、九宫格、图集旋转、premultiplied alpha、
Sprite 实例化直进 G-buffer。这些保持 UNSUPPORTED 记录（§1-§9），不在本接口预留槽位。

---

## 15. 自定义扩展边界（v1.4 实现落地）

本节记录游戏层可以通过哪些接口定制引擎行为，以及四个候选扩展点的
当前状态。原则：核心稳定，扩展按需；现有接口优先；回调必须定义调用时机、
资源所有权、失败处理和 GL 状态恢复责任，否则不进公共 API。

### 15.1 游戏层已经能做的自定义（SUPPORTED）

- 纹理：host 自己 `zcsr_gl_texture_create` 创建/上传/销毁，引擎只持有句柄。
- 几何：`geo_face` / `geo_tex_face` / `geo_nmap_face` 提交任意世界空间凸四边形。
- 光照：每盏光的位置/半径/decay/颜色/高度独立填，类型 POINT/PARALLEL/SPOT。
- 质量：`zcsr_light2d_set_options` 独立开关 shadows/normals/ray_shadows/bloom/gain/scale_pct。
- 相机：`set_center` / `set_zoom` / `set_viewport`。
- 后处理：`zcsr_gl_l2d_set_post_process_hook` 注册全屏回调（见 15.2-1）。
- Sprite 进 G-buffer：`zcsr_sprite_draw_lit`（§14.4）。
- HUD/透明：路径 A `zcsr_sprite_draw` / `zcsr_gl_submit`。
- 资源组织：文件/图集/包加载完全在引擎外，引擎不关心数据从哪来。

### 15.2 四个候选扩展点的状态

**1. 自定义后处理 hook（CRT/色偏/扫描线）—— SUPPORTED**
- 接口：`zcsr_gl_l2d_set_post_process_hook(ctx, callback, userdata)`。
- 合成现在先写进引擎拥有的合成 FBO（`l2d_comp_tex`，GL_RGBA16F 线性），再写
  统一线性帧；不再直写默认 framebuffer 0。回调触发时该纹理绑在 unit 9，
  回调从它采样做全屏效果，输出写回线性帧。
- 责任边界：
  - 调用时机：合成完成、写线性帧之前，每帧至多一次。
  - 资源所有权：合成纹理由引擎创建/持有/销毁，回调不得删除或重分配。
  - 输出目标：回调负责把线性结果写回当前绑定的线性帧；不得做 display encode。
  - GL 状态：回调可改 shader/纹理单元/framebuffer，返回后引擎不依赖其状态；
    回调必须自己保证输出已写入线性帧。
  - 失败处理：回调返回 false 或未注册时，引擎用 blit program 直复制原始线性合成。
  - 线程：仅在渲染线程、GL current 时调用，不可重入。
- 限制：合成纹理是线性 RGBA16F，保留 HDR；最终显示编码由 present/read_rgba
  统一执行。视口尺寸随引擎 viewport，window resize 由引擎重建。

**2. 自定义光类型（spot/area light）—— SPOT 已 SUPPORTED，其余 PROPOSED**
- 已落地：`ZCSR_LIGHT_SPOT = 2`，`zcsr_light` 加 `inner_angle/outer_angle`（弧度），
  锥心方向复用 `dir_x/y/z`（提交时归一化）。
- 光照 shader 用 `acos(dot(frag_dir, aim_dir))` 求锥角，`smoothstep(inner, outer)`
  在内外锥间衰减，锥外归零；点光衰减公式保持不变，cone 是乘在它上面的门控。
- 未落地（area/圆柱光等）：PROPOSED，不预留高位枚举，不加 custom_light_cb。
  新光类型若需要不同 shader/阴影算法，先定义数据结构与 pass，再按最小扩展新增。
  能由现有光组合表达的，游戏层自己组合。

**3. 自定义几何（法线贴图 quad）—— SUPPORTED**
- 接口：`zcsr_gl_l2d_geo_nmap_face`（albedo + normal 两张纹理 + 面法线 + 切线）。
- 顶点格式 11 float：pos3 + normal3 + tangent3 + uv2，独立 program/VAO/VBO，
  现有 `geo_face` / `geo_tex_face` 行为完全不变。
- FS 对 tangent 做 Gram-Schmidt 重新正交化，构建 TBN，把切线空间法线
  （`tex*2-1`）变换到世界空间写 G2。
- 切线约定：调用方给的 `tangent_xyz` 是单位世界向量，与法线贴图 +U 方向对齐。
- 法线贴图是数据纹理不是颜色：按普通 GL_RGBA 创建、shader 原样采样，
  不走 sRGB 解码。alpha cutout 规则与 `geo_tex_face` 相同（<0.5 discard）。

**4. 资源加载回调（zip/网络流纹理）—— UNSUPPORTED（不进 Light2D API）**
- 原因：文件/zip/网络读取是资源层职责，不是渲染器职责。
  游戏层读文件→解码成像素→调现有 `zcsr_gl_texture_create`，这条路已经通了。
- 重新评估条件：要做统一资源流时，在资源层定义 Reader/Stream 契约，
  不为 Light2D 单独做混合入口。

### 15.3 明确不做的事（当前阶段）

- 脚本语言运行时（Lua/Woolang）
- 材质节点编辑器
- 通用插件注册/动态加载
- Vulkan/Metal 后端
- 自定义 shader 图
- 空函数指针占位

这些不是永久禁止，是没有足够理由在当前阶段做。
未来只有真实游戏需求触及边界时，才按最小扩展原则新增接口。

## 16. UI / 文字 / HUD 的渲染归属（v1.5）

- UI、文字、HUD 永远走路径 A immediate forward batch（zcsr_gl_fill_rect /
  zcsr_gl_draw_text / zcsr_font_atlas_draw_text），由宿主在 light2d 合成到
  统一线性帧之后提交，最后统一 flush + present/read_rgba（一次 encode）。
- 禁止把 UI / 文字 / HUD 提交进 light2d 的 G-buffer（G0/G1/G2/G3）或光照 pass。
  它们不参与延迟光照，也不该被光线调制。
- 重开销项（ray_shadows、bloom、light_scale_pct=100、高 gain）只挂在 HIGH / ULTRA
  档；LOW 档默认全关、走高性能路径。切档时这些项随之开关，不是永久开。
- 字体默认 TTF（Inter），通过 font_atlas 注入 UI 后端；未注入时回退内置 8x8
  bitmap font，老 test 不受影响。

## 17. GPU-only 收口后仍未合并的结构边界

以下不是“已实现但没写文档”，而是当前明确不做假统一的边界：

- **三套相机不互相冒充**：`zcsr_camera`（X 东 / Y 南 / Z 上，height-shear）、
  `zcsr_scene_camera`（x/z 地面轴、y 高度、look-at + ortho）、
  `zcsr_camera3d`（透视数学工具）各自成立。没有隐式转换；混用必须由调用方
  显式换算并写清轴映射。路径 B 的 `geo_*` 入口只接受 `zcsr_camera`。
- **Pivot 仍按各自路径定义**：屏幕 sprite 以矩形中心为锚点；scene entity 用
  `pivot_x/pivot_y`；实例路径用中心 + 宽高；L2D face 用显式四角。跨路径迁移
  必须重算锚点，不能假设同一个 pivot 语义。
- **路径 B 没有 lit instancing**：`geo_tex_face*` 是一面一次提交，没有跨面
  batch。需要大量受光实体时，先做实例化 lit batch，不能直接拿路径 A 的实例
  数量去压路径 B。
- **存在统一入口但不自动选路径**：`zcsr_scene_submit` 是 scene 的唯一统一提交入口，
  调用方必须显式选择 `ZCSR_SCENE_SUBMIT_DISPLAY` 或 `ZCSR_SCENE_SUBMIT_LIT`。
  DISPLAY 复用 `scene_render` 的矩形/四角桥；LIT 复用 `zcsr_gl_l2d_geo_*`，并按
  §8 的 q 键远到近提交。没有“自动根据 scene 内容选路径”的隐式行为，也不能用
  `scene_render` 的桥冒充 LIT 提交。
- **路径 A 不做自动 alpha 排序**：`zcsr_gl_submit` 是 painter order；需要
  透明排序时用 `zcsr_gl_instance` + `has_alpha` + `zcsr_sort_state`。
- **Light2D 只有 1D per-channel LUT + per-face 3x3 矩阵**：没有全局 3D LUT；
  per-object 色彩分级用 `zcsr_gl_l2d_geo_tex_face_ex` 的矩阵入口。