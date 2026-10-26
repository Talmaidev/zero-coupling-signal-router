# RENDER2D_CONTRACT.md — 2D 渲染基础契约 v1

基线：`light2d-baseline-v1.1` = commit `1911e3b`。
本文件不是新设计，而是把"现在已经定了什么 / 当前只有一半能力 / 建议以后这么定 / 明确不做 / 没查清"
逐条分开写清楚，给第一款 2D 游戏当不返工的边界。已有文档：
`LIGHT2D_INTEGRATION.md`（接入）、`LIGHT2D_MATH_MODEL.md`（公式）、`LIGHT2D_PIPELINE_MAP.md`（结构）。
本文件不复制它们的公式，只在每条后给源码位置做交叉锚点。

状态标签（每条必须挂一个）：

- FROZEN：已有实现与公开承诺一致，改它要动基线，不许顺手改。
- CURRENT-LIMITED：行为明确但能力有限，限制写在同一条里。
- PROPOSED：为后续 Sprite/Tilemap/材质接入建议的目标规范，**现在没有实现**。
- UNSUPPORTED：当前明确不支持，别当有。
- UNKNOWN：源码/调用方/文档不足以判定，先挂着，不猜。

行号对应 `1911e3b` 工作树；源码改动会漂移，符号/函数名优先于行号。

---

## 0. 两条渲染路径并存（先读这个，再看下面九节）

仓库里同时存在两条互不相同的 2D 渲染路径，后面所有契约都在这两条之上：

- 路径 A — 立即精灵/UI 路径：`modules/glrender/glrender_core.c` + `glrender_sprite.c` + `glrender_instanced.c`。
  屏幕像素空间，`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` 直混合
  （`glrender_core.c:264-265`），左上原点 dst rect（`include/zcsr/gl_render.h:41`）。
  **不进 Light2D G-buffer，不受光照。**
- 路径 B — Light2D 延迟光照路径：`modules/glrender/glrender_l2d_geo.c` + `modules/light2d/light2d.c`
  + `glrender_light2d_ext.c`。世界空间凸四边形进 MRT G-buffer，blend/depth 全关，
  painter's order（`glrender_l2d_geo.c:291-293`）。

`modules/sprite/sprite.c` 的 `zcsr_sprite_draw` 是个桥：把世界 (x,y) 在 wz=0 投成屏幕像素后
喂路径 A（`sprite.c:23-48`）。所以**现有地面精灵画在合成结果之上，不受点光照射**——这是现状，
不是 bug。首款游戏如果要"被光照的精灵"，必须走路径 B 的 `geo_tex_face` 显式提交世界角点，
不能指望路径 A 自动进 G-buffer。

---

## 1. 颜色空间与材质语义

- [FROZEN] 纹理以普通 `GL_RGBA` 内部格式上传，GPU 不做 sRGB 解码。
  证据：`glrender_core.c:313`、`:330`（`glTexImage2D(..., GL_RGBA, ..., GL_UNSIGNED_BYTE, rgba)`）。
- [FROZEN] Light2D 路径 B 的 Albedo 线性化点 = 合成 FS 内 `pow(albedo, vec3(2.2))`，deferred 与 bloom
  两个变体同一处。**本规则只描述 Light2D 的 G-buffer 合成路径，不适用于路径 A 的立即 Sprite/UI**——
  路径 A 直写默认目标，不做这个 pow。禁止在 Light2D geo 纹理路径再解一次 sRGB。
  证据：`glrender_light2d_ext.c:126`（deferred）、`:176`（bloom）；契约原文 `gl_render_l2d_geo.h:82-85`。
- [FROZEN] Light2D 路径 B 的输出编码 = 同一合成 FS 内 `pow(mapped, 1/2.2)`，tonemap `x/(x+1)` 也在同一 FS，
  无独立后处理 pass。**该输出编码规则不代表路径 A 有相同的颜色转换流程**。
  证据：`glrender_light2d_ext.c:128-129`、`:179-180`。
- [FROZEN] tint 与平色 albedo_rgba 实际在**显示域(gamma)**与纹理想素逐通道相乘，乘积直写 RGBA8 G0，
  合成时才整体 pow2.2——不是线性空间乘子。v1.1 已裁决旧头文件"linear"标注为错误描述。
  证据：FS `vec4 rgba = texture(u_tex, v_uv) * u_tint;`（`glrender_l2d_geo.c:91`）；
  公开契约 `gl_render_l2d_geo.h:23-26`（平色）、`:75-78`（tint）。
- [FROZEN] tint/albedo 乘积 >1 被 RGBA8 截断到 1.0，G0 路径不承载 HDR。
  证据：G0 = RGBA8（`glrender_light2d_ext.c:724`）；头文件明说 `>1 is clamped by RGBA8`（`gl_render_l2d_geo.h:26`）。
- [FROZEN] HDR>1 只在 RGBA16F 附件上保留：G3 Emissive、光累积缓冲。
  证据：`glrender_light2d_ext.c:726`（G1/G3 用 RGBA16F）、`:1141`（光缓冲 RGBA16F）。
- [CURRENT-LIMITED] `zcsr_light.rgb` 注释声明为 linear light 颜色，`a` 为逐光强度，允许 >1。
  证据：`include/zcsr/light2d.h:39-42`。限制：进入光缓冲后仍走同一合成公式，
  不提供逐光 gamma 校正。
- [FROZEN] Emissive 当前语义：`lit = albedo_lin * (emissive + light + ambient)`——自发光先与光照相加，
  **再被 albedo 逐通道调制**，不是教科书式独立加性 `albedo*light + emissive`。
  证据：`glrender_light2d_ext.c:127`（deferred）、`:177-178`（bloom 对 E 也做 9-tap 后放进同一括号）。
- [PROPOSED] 独立加性自发光 `C = albedo_lin*(light+ambient) + emissive_lin`——未实现，触发条件=真实自发光需求，
  届时必须重跑合成回归。依据：`LIGHT2D_MATH_MODEL.md` §4.4。
- [UNSUPPORTED] 法线贴图、粗糙度图、金属度图、遮罩图——材质通道里没有这些采样器，FS 只有 u_tex/u_tint/u_emissive。
  证据：`glrender_l2d_geo.c:83-85`。
- [UNSUPPORTED] sRGB 上传格式（`GL_SRGB8`/`GL_SRGB8_ALPHA8`）——全仓 `glTexImage2D` 内部格式只有
  `GL_RGBA`/`GL_RGBA16F`/`GL_R8`（`glrender_core.c:313,330`；`glrender_light2d_ext.c:421,1109,1141`）。
  未来要线性材质流必须改这里，不要假定 GPU 已经在解码。
- [FROZEN] 环境光固定 `vec3(0.03)`。证据：`modules/light2d/light2d.c:783`。

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
- [CURRENT-LIMITED] 现有 GL 精灵不带高度：`zcsr_sprite_draw` 写死 `wz=0.0f` 投影。
  证据：`sprite.c:23`。要带高度精灵是新需求。
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
- [CURRENT-LIMITED] 旋转：整纹理路径绕中心旋转（`sprite.c:34`）；**图集子矩形路径无旋转通道**。
  证据：`sprite.c:27` 的 if/else 注释 "the atlas queue has no rotation channel"。
- [UNKNOWN] 镜像/翻转（负 scale_x 或负 scale_y）：`zcsr_transform` 有 scale 分量，`sprite.c:24-25` 直接乘进
  pw/ph，负宽高在 `zcsr_gl_queue_atlas_sprite` / VS 里的行为无文档、无测试。要做镜像必须先补这条契约。
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
- [FROZEN] 实例化顶点 `zcsr_gl_instance` = 15 float + uint32 tex + float z = 68 字节，
  tex/z 是 CPU-only 排序键。证据：`include/zcsr/gl_render.h:97-108`。
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
- [UNSUPPORTED] 图集子矩形 + 旋转同时：见 §3，`sprite.c:43` 注释明说 atlas queue 无旋转通道。
- [FROZEN] 过滤：创建默认 NEAREST，`zcsr_gl_texture_filter` 可切 LINEAR。
  证据：`glrender_core.c:309-310`（默认创建）、`:322-323`（filter 接口）。
- [FROZEN] Wrap：全部 `GL_CLAMP_TO_EDGE`，无 REPEAT/MIRROR。
  证据：`glrender_core.c:311-312`、`glrender_light2d_ext.c:419-420,1107-1108,1139-1140`。
- [UNSUPPORTED] mipmap：全仓无 `glGenerateMipmap`，min_filter 只在 NEAREST/LINEAR 间切（`glrender_core.c:309,322`）。
  缩小纹理会直接线性取单像素，无 LOD 链。
- [UNKNOWN] 像素对齐（pixel snapping / 整数 PPU 对齐）：无任何 snap 代码或注释，要做必须新立契约。
- [UNSUPPORTED] 九宫格多区域 UV：见 §3。
- [CURRENT-LIMITED] atlas padding 只有 1px gutter（打包期固定），不支持运行时可调 inset。
  证据：`texture_atlas.h:36`。

---

## 7. 纹理资源与数据图语义

- [FROZEN] 普通纹理上传 = 8-bit RGBA，内部 `GL_RGBA`，`GL_UNSIGNED_BYTE`，上传时 GPU 不做 sRGB 解码。
  **这只是上传行为，不代表路径 A 与路径 B 共享同一套颜色处理流程**。
  证据：`glrender_core.c:313`（create）、`:330`（带 linear 标志的 create）。
- [FROZEN] Light2D 路径 B 的颜色图处理：G0 Albedo 在合成阶段由 `pow(albedo, vec3(2.2))` 手动线性化。
  路径 A 的立即 Sprite/UI 不经过 Light2D 合成，**不得把这条颜色空间契约推广到路径 A**。
  上传入口当前不区分颜色图与数据图（`gl_render.h:36` 签名只有 rgba/w/h，无 internalformat 参数）；
  法线/粗糙度/遮罩等数据图不属于现有 Light2D 材质能力。证据：`glrender_light2d_ext.c:126`。
- [FROZEN] G-buffer 附件格式表：
  - G0 Albedo = RGBA8（`glrender_light2d_ext.c:724`）
  - G1 Position = RGBA16F（`:726`）
  - G2 Normal = RGBA8（`:724`）
  - G3 Emissive = RGBA16F（`:726`）
  枚举同步在 `gl_render_light2d_ext.h:22-25`。
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
- [FROZEN] 子区域上传 `zcsr_gl_texture_upload_region`（公开头文件 `gl_render.h:191-195` 有声明——
  GPT 那份 API 摘要漏列，以头文件为准）。实际 GL 状态行为（`glrender_sprite.c:246-252`）：
  上传前 `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)`、`GL_UNPACK_ROW_LENGTH = src_stride/4`；
  上传后把 `GL_UNPACK_ROW_LENGTH` 设回 0——**不保存、也不恢复调用前原值**；
  `GL_UNPACK_ALIGNMENT=1` 也不还原。`src_stride % 4 != 0` 或平台无 `GL_UNPACK_ROW_LENGTH` 时退化为
  逐行 `glTexSubImage2D`（`:254-263`），不碰 ROW_LENGTH。调用方若此前设过非零 ROW_LENGTH 或
  非 1 ALIGNMENT，本函数不会替你恢复。
- [PROPOSED] 线性数据纹理（法线/粗糙度/遮罩）未来应走独立上传入口，带明确 internalformat 枚举，
  不进 G0 的 gamma 直写路径。触发条件=首款游戏真的要法线贴图。

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
  证据：`gl_render_l2d_geo.h:40-42`。
- [FROZEN] alpha-cutout 唯一规则：`if (rgba.a < 0.5) discard;`（严格小于；作用于 texel*tint 乘积）。
  证据：`glrender_l2d_geo.c:92`。阈值 0.5、比较符 `<`、先 discard 再写四附件——被丢片元对所有附件零写入。
- [UNSUPPORTED] Light2D G-buffer 路径的任意半透明混合 / 排序：cutout 之外没有半透明通道。
  证据：`glrender_l2d_geo.c:291` blend 关；`LIGHT2D_INTEGRATION.md` §7。
- [FROZEN] 但路径 A（立即精灵）有 straight alpha 混合：`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`。
  证据：`glrender_core.c:264-265`。这是屏幕空间 sprite/UI 的混合，与 G-buffer 无关。
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
  begin_geometry → 宿主提交世界几何 → begin_lighting → add_light×N → end_lighting（合成回默认目标）→ 宿主 HUD/路径 A 精灵。
  证据：`light2d.h:114-122`；`LIGHT2D_INTEGRATION.md` §1。
- [CURRENT-LIMITED] UI 没有独立层概念：HUD 就是 end_lighting 之后用路径 A 在默认目标上画的屏幕像素。
  证据：`LIGHT2D_INTEGRATION.md` §1 末行。

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
  不改写 geo/light2d 核心契约；触发条件=首款游戏需求出现。依据：GPT 计划 §五。

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
3. 颜色语义：tint 显示域乘、Albedo sRGB 内容、合成唯一 pow2.2；alpha-cutout <0.5。
4. 点光（高度/半径/decay/逐光强度）+ ray 阴影（间接 GPU 证据，非同像素因果）。
5. Painter's order：宿主按 q 键远到近提交，不穿插。
6. 固定渲染目标 1280×720 已验证；动态 resize 不做保证。
7. HUD/屏幕 UI/透明精灵走路径 A（straight alpha，屏幕像素），不要求光照。

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

## 12. 交付报告（本文件 v1 自检）

各领域状态计数（按本文实际条目逐条数，FROZEN / CURRENT-LIMITED / PROPOSED / UNSUPPORTED / UNKNOWN）：

- §1 颜色空间与材质：8 / 1 / 1 / 2 / 0
- §2 单位相机：10 / 2 / 0 / 1 / 0
- §3 Pivot 变换：4 / 1 / 1 / 2 / 1
- §4 几何法线：7 / 2 / 1 / 1 / 0
- §5 顶点提交：8 / 1 / 0 / 1 / 0
- §6 UV 图集：6 / 1 / 0 / 4 / 1
- §7 纹理资源：9 / 0 / 1 / 1 / 0
- §8 深度混合：12 / 1 / 0 / 2 / 0
- §9 生命周期：11 / 2 / 1 / 0 / 0

合计：FROZEN 75 / CURRENT-LIMITED 11 / PROPOSED 5 / UNSUPPORTED 14 / UNKNOWN 2。
UNKNOWN 两项（负 scale 镜像、像素对齐）源码无证据，未猜。

未做的事（对应 GPT 计划 §6）：未改任何 shader/数学/基线；未新增测试；未动 light2d core；
未把未验证能力标成已支持；未重构文件布局；未 push/merge。本文件唯一新增产物。
两个已登记限制（GPU 射线阴影同像素因果、nl 不对称）维持原状，不重开诊断。

---

## 13. v1.2 证据补记（GPT-web/DS-web plan 回本地源码核对后关闭的 UNKNOWN）

以下条目全部来自 `modules/glrender/glrender_light2d_ext.c` 的 light FS 逐行核对，
状态一律 FROZEN（已读代码，非运行证据）。这一节只补精度，不改 §1-§11 的既有结论。

- [FROZEN] 衰减公式里的 "1.0" 不是硬编码常数，是 uniform `u_dist_scale`：
  `atten *= 1.0 / (dist / max(u_dist_scale, 1e-4) + 1.0)`（`:324`）；半径也带门卫
  `r = dist / max(u_radius, 1e-4)`（`:322`）。旧文档写的 "1.0=一个参考瓦片" 是它的默认语义，
  实际可配置。`u_radius` 为 0 时不会除零。
- [FROZEN] dist=0 保护：`ldir = (tl < 1e-6) ? vec3(0,0,1) : to_light/tl`（`:336`）；
  法线零长保护 `n = (dot(n_raw,n_raw) < 1e-6) ? up : normalize(n_raw,0)`（`:333-334`）。
  GPT 标 UNKNOWN 的两个除零点都已在 shader 内保护，不会出 NaN。
- [FROZEN] 法线解码口径：`n_raw = texture(u_Normal).xy * 2.0 - 1.0`（`:332`）。
  "xy=(0,0) 取朝上"指的是**解码后**的 n_raw=(0,0)，对应 G2 存储值 (0.5,0.5)（=清屏值）。
  不是存储值 (0,0)。DS 提的歧义已澄清。
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
GPT/DS 提的其他"建议措辞"（Emissive 被调制、tint 显示域、px_to_world 只地面、viewport 权威、
q 键非通用深度）在 §1/§2/§8/§11 原文已经写清楚，不重复改。

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
2. G-buffer 四附件布局：G0 RGBA8 显示域 / G1 RGBA16F 世界XYZ / G2 RGBA8 编码法线 /
   G3 RGBA16F emissive + 清屏值。证据：§7。
3. 帧序与 GL 状态配对：begin_geometry→geo→begin_lighting→add_light→end_lighting→HUD；
   geo begin/end 精确保存恢复。证据：§9。
4. 路径 A/B 边界：路径 A 屏幕像素 straight-alpha 不受光；路径 B 世界几何进 G-buffer 受光；
   不自动互通。证据：§0、§8。
5. 路径 B 颜色语义：Albedo 唯一线性化点在合成 FS pow(2.2)；Emissive 当前被 albedo 调制。
   **本条只约束路径 B，不推广到路径 A**。证据：§1。

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

法线：地面/水平精灵统一 `(0,0,1)`。立面、斜面、翻转法线不在本期，见 §14.5 槽位。

提交：
- 受光精灵 → `zcsr_gl_l2d_geo_tex_face(r, a.tex, corners_xyz, uv_st, tint, (0,0,1), emissive)`。
- HUD、屏幕 UI、不透明背景 → 继续走路径 A（`zcsr_sprite_draw`/`zcsr_gl_submit`），不进 G-buffer。
- 透明特效（半透明叠加）→ 路径 A；当前路径 B 不支持半透明。证据：§8。
- 排序：受光精灵之间仍由宿主按 q 键远到近提交（§8）。

### 14.5 第二层扩展槽位（未来可 add，不改 CORE）

这些位置现在留接口、不实现。未来第一款游戏真需要时，在 ADAPTER 层加，不动 §14.2 的 CORE：

- normal 覆盖：水平 (0,0,1) 之外的立面法线输入（PROPOSED，触发=墙面精灵需求）。
- emissive 材质参数：除 uniform emissive 外的逐精灵发光（PROPOSED，当前 G3 是逐 draw uniform）。
- tint 模式：MIX/ADD 闪烁（路径 A 已有 `zcsr_color_mode`，路径 B 未接；PROPOSED）。
- 纹理样式扩展：未来加 normal map / 遮罩图时走独立上传入口，不混 G0 gamma 路径（§7 PROPOSED）。
- ground_z 非 0 悬浮精灵：接口已留，当前几何上和贴地等价，不需要特殊处理。

**不开放的扩展**：tilemap、动画帧、九宫格、图集旋转、premultiplied alpha、
Sprite 实例化直进 G-buffer。这些保持 UNSUPPORTED 记录（§1-§9），不在本接口预留槽位。
