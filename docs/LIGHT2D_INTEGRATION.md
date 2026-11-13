# LIGHT2D_INTEGRATION.md — Light2D 可继承基础层 v1 接入文档

封版版本：Q5（2026-10-26）；加固增量 v1.1（2026-10-26，见 §7 R1 与 §8）。
范围：平色 + 纹理几何进入 G-buffer、世界坐标契约、
共享相机高度剪切投影、逐像素射线阴影、延迟合成。
**这不是完整的游戏 Sprite/Tilemap 管线**—— sprite 管线集成、半透明混合、
emissive 纹理等明确不支持（见"能力与限制"）。
维护者文档：`LIGHT2D_PIPELINE_MAP.md`（结构/风险）；数学基准：
`LIGHT2D_MATH_MODEL.md`（公式与证据分级）。

## 1. 系统目标与渲染阶段

Light2D 是零耦合的 2.5D 延迟光照管线，跑在 glrender 之上：

```
宿主游戏
  ├── zcsr_camera（共享相机，camera.h）
  ├── 场景几何（世界坐标角点 + 法线 + 材质）
  ▼
zcsr_light2d_begin_geometry()        ← G-buffer MRT 绑定（引擎内部管理）
  ├── zcsr_gl_l2d_geo_begin/face/tex_face/end   ← 宿主提交几何（公开 API）
  ▼
zcsr_light2d_begin_lighting()        ← 绑回默认目标 + G-buffer 纹理上单元
  ├── zcsr_light2d_add_light() × N   ← 世界坐标 3D 点光
  ▼
zcsr_light2d_end_lighting()          ← 逐像素射线阴影 + N·L + 合成回窗口
  ▼
宿主照常画 HUD（默认目标已在绑）
```

数学核心（Geometry Proof v1，数值用例固化在 testmain 启动时打印）：
接收点 P 被遮挡 ⟺ 线段 L→P 与遮挡棱柱（footprint × z∈[0,H]）相交。
遮挡体高度与光高真实参与：光越低/箱越高影子越长。地面（z=0）接收点
的阴影尖端由相似三角形给出：`x_tip = x_max · L_z / (L_z − H)`
（L_z > H；H=2、x_max=3 时 L_z=3→9、L_z=5→5）。

## 2. 最小调用示例

### 2.1 平色几何（Q3）

```c
/* 一次性 */ zcsr_light2d_set_camera(ctx, &cam);   /* 存活指针，改字段即生效 */
/*           zcsr_light2d_add_occluder(ctx, &occ); footprint+height，世界单位 */

/* 每帧 */ zcsr_light2d_begin_geometry(ctx);
zcsr_gl_l2d_geo_begin(r, &cam);
zcsr_gl_l2d_geo_face(r, corners_xyz, albedo_rgba, normal_xyz, emissive_rgba);
/* ... 按远到近提交 ... */
zcsr_gl_l2d_geo_end(r);
zcsr_light2d_begin_lighting(ctx);
zcsr_light2d_add_light(ctx, &light);   /* x,y,世界单位；height=离地高 */
zcsr_light2d_end_lighting(ctx);        /* 合成自动回窗口 */
```

### 2.2 纹理几何（Q4）

```c
zcsr_gl_texture tex = zcsr_gl_texture_create(r, rgba, w, h); /* 宿主所有 */
/* geo_begin/geo_end 之间： */
zcsr_gl_l2d_geo_tex_face(r, tex, corners_xyz, uv_st, tint_rgba,
                         normal_xyz, emissive_rgba);
/* 宿主在 shutdown 里 zcsr_gl_texture_destroy(r, tex); */
```

## 3. 调用顺序与生命周期（源码审计结论）

- `l2d_geo_*` 的 program/VAO/VBO 属于 **renderer 内部资源**
  （`glrender_internal.h` l2d_geo_* 字段），首次 `geo_begin` 惰性创建
  （glrender_l2d_geo.c:126 门卫），`zcsr_gl_core_destroy` 统一释放
  （glrender_core.c:281 → cleanup :239）。宿主不创建、不销毁、不持有。
- `geo_begin` 失败（惰性初始化失败/嵌套 begin/相机参数非法）返回 false，
  不会置 active；`geo_face/tex_face/geo_end` 在非 active 时一律 false。
  顺序错误不会破坏 GL 状态。
- **"只在几何阶段内调用"是文档契约，无机制强制**：geo API 不感知
  light2d 相位，错相位调用会画进当时绑定的任意 FBO。接入者必须自律。
- 纹理句柄 `zcsr_gl_texture` 由宿主 `zcsr_gl_texture_create` 创建并销毁；
  `tex_face` 内部经 `zcsr_gl_find_tex` 解析，**句柄无效或已销毁 → 返回
  false，无 GL 调用、无崩溃**。geo 不拥有纹理。

## 4. 坐标 / 投影 / 数据契约

- 世界：X 东、Y 南、Z 上；地面 z=0；一切几何、光源、遮挡体用世界单位。
- **G-buffer Position 必须是世界 XYZ**（VS varying 插值直传）。禁止用
  `gl_FragCoord` 反推——那会丢 z（本项目曾因此整层方向错误）。
- 投影（唯一权威 = `zcsr_camera.height_shear`，任何 shader 禁止写死）：
  ```
  screen_x = view_w/2 + (x − cam.cx) · ppu
  screen_y = view_h/2 + ((y − cam.cy) − k·z) · ppu      k = height_shear
  ```
  0 = 纯俯视；0.5 = 当前 2.5D 调试观感。这是**游戏剪切投影**，
  不是真实斜俯视相机（地面 y 无 cosθ 压缩）。视口尺寸取调用时
  GL viewport（渲染目标为权威）。
- 面角点：`corners_xyz[12]` 边界顺序 4 点，拆 (0,1,2)+(0,2,3) 两个三角形；
  v1 只承诺简单凸平面四边形。`normal_xyz` 为世界轴法线。
- **Normal 编码与实际支持范围**：FS 写 `n*0.5+0.5`；光照 shader 解码只读
  xy——xy=(0,0) 解码为朝上 (0,0,1)，否则为平面内 (nx,ny,0)。
  **法线 z 分量被写入但当前不被读取（D2）**：仅支持朝上面与轴对齐立面，
  斜面会在光照时丢 nz。
- **Emissive**：RGBA16F HDR 自发光（attachment 3，调用方提供），
  v1 不支持纹理发光。注意 v1 合成语义：Emissive 在最终式中先与光照/
  环境光相加，**再被 Albedo 逐通道调制**（`C = A_lin·(E_lin+ΣL+amb)`），
  不是独立加性自发光（`A_lin·ΣL + E_lin`）；独立加性 = PROPOSED，未实现。
- **深度**：G-buffer 无深度附件 → painter's order，宿主按远到近提交。
  排序参考键 `q = k·(y − cy) + z` 升序。`world_z` 单独不是通用深度。

## 5. 纹理 / 颜色 / Alpha 契约

- **UV**：`(u0,v0)` = 子矩形**图像左上**，v 向图像底增长（与 sprite 管线
  一致；运行时证据：v=0.25 采样到夹具顶行象限=红色，翻转假设会被证伪）。
  角点 0 对应 (u0,v0)。
- **颜色空间**：纹理以普通 GL_RGBA 上传（无 GL sRGB 内部格式）→ GPU 不做
  sRGB 解码；**合成 pass 的 pow(2.2) 是全链路唯一线性化环节**。texel×tint
  直写 attachment 0（RGBA8），与平色路径同一编码——**禁止在纹理路径再
  解一次 sRGB**。
- **tint / albedo 乘法空间（v1.1 契约裁决）**：tint 与平色 albedo_rgba
  实际在**显示域（gamma）**与纹理想素逐通道相乘，乘积直写 RGBA8 G0，
  合成时整体 pow(2.2) 线性化。**这不是线性空间乘法**。旧版头文件曾将
  tint 标注为 "linear"，该描述与实际实现不符，自 v1.1 起视为已纠正的
  契约说明；需要真正线性 tint 的调用者不能把当前路径当作已支持。
  RGBA8 会把 >1 的 tint/albedo 截断到 1.0——G0 路径不能承载 HDR；
  HDR >1 仅在 RGBA16F 附件（Emissive、光缓冲）上保留。
- **Alpha-cutout v1（精确规则，源码 glrender_l2d_geo.c:92）**：
  `if (rgba.a < 0.5) discard;` 其中 `rgba = texel × tint`。
  阈值 0.5、比较符 `<`、作用于乘后 alpha。被 discard 的片元**不写任何
  attachment**，先提交的下层表面完整保留；通过阈值的片元按表面数据写入
  （albedo alpha = texel.a × tint.a）。无混合、无排序。
- **纹理 alpha ≠ 阴影几何**：遮挡轮廓只由 `zcsr_light2d_add_occluder`
  契约决定，永不从纹理 alpha 自动生成。

## 6. OpenGL 状态契约（逐项源码核对，行号为 glrender_l2d_geo.c）

| 状态 | begin 保存 | 使用期间 | end 恢复 |
|---|---|---|---|
| GL_CURRENT_PROGRAM | :271 | 改为 geo program（face 可切 tex :406） | :428 ✓ |
| GL_VERTEX_ARRAY_BINDING | :269 | geo VAO（:359/:418） | :429 ✓ |
| GL_ARRAY_BUFFER_BINDING | :270 | geo VBO（:360/:419） | :430 ✓ |
| GL_BLEND | :272 | **禁用**（:280） | :434 ✓ |
| GL_DEPTH_TEST | :273 | **禁用**（:281） | :435 ✓ |
| GL_CULL_FACE | :274 | **禁用**（:282） | :436 ✓ |
| GL_ACTIVE_TEXTURE | :276 | 切到 TEXTURE0（:277/:416） | :433 ✓ |
| TEXTURE0 的 2D 绑定 | :278 | 绑宿主纹理（:417） | :431-432 ✓ |
| viewport / FBO / draw buffers / scissor | 不触及（viewport 只读 :284） | — | — |

注意两点：(1) 恢复 `GL_ACTIVE_TEXTURE` 与恢复 unit0 绑定是**两个独立动作**，
:431-432 先在 TEXTURE0 下还原绑定、:433 再还原活动单元——两者都已处理。
(2) begin 与 end 之间 blend/depth/cull 处于引擎禁用态：**不要在 geo_begin/
geo_end 之间穿插其他 GL 绘制**；需要画别的，先 geo_end。

## 7. 能力与限制（接入者必读）

**能用的**：平色/纹理凸四边形进 G-buffer；世界 XYZ Position；朝上面 +
轴对齐立面法线；alpha-cutout 贴片；3D 点光（高度/半径/逐光强度）；
逐像素高度感知射线阴影；遮挡棱柱（height 在 ray 路径生效；
factor/distance/reverse/self_shadow 在 2D 挤出路径生效——ray_shadows
开启时点光走射线、**平行光仍走 2D 掩码路径并保留全部属性**）；bloom 合成；
LOW 档回退（调试级色块）。

**不能用的（明确限制）**：任意半透明混合与排序；纹理 emissive；斜面法线
（D2：Normal.z 写而不读）；ray 阴影路径忽略遮挡体 factor/distance/reverse/
self（G1）；sprite/instance 管线直接进 G-buffer（单输出 shader，需另建
入口）；自动 alpha→遮挡体；图集/动画/Tilemap。缺陷登记：D1 已修复
（height_shear 单源化）、D2/D3、G1 仍开放。

## 8. 如何验证接入成功（工程教训）

**退出码 0 + shader 编译成功 ≠ 像素正确。** Q4 曾出现：shader 编译通过、
无任何 GL 报错、autoquit 退出 0，但 tex program 的相机 uniform 未设置
（u_ppu=0）→ 四边形塌缩成视口中心的零面积点，屏幕上什么都没有。
**唯一可靠的验收是读像素**：

```powershell
# 1) 构建（零警告）
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build build-msvc -j"
# 2) autoquit 冒烟（proof 4/4 PASS 打印在 stdout）
build-msvc\zcsr_test.exe autoquit
# 3) 截图探针（启动 exe → 定窗 → 截帧 → 读指定像素 RGB）
powershell -ExecutionPolicy Bypass -File tools\verify_light2d_shot.ps1
# 4) 数学契约 ctest（纯 CPU：投影往返/k-shift 定律/射线 slab 用例）
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat\" && cd build-msvc && ctest -R light2d_math --output-on-failure"
```

`tools/verify_light2d_shot.ps1`（自 build-msvc/shot.ps1 固化）在 1280×720
autoquit 会话（光钉在 (0,0)）截帧后输出指定像素 RGB。预期（tonemap 后的
实测值，判定方式 = 色相比对而非逐字节相等，标注◎处须为"地板中性色"）：

| 探针（客户坐标） | 场景 | 实测 RGB | 判定 |
|---|---|---|---|
| (256,216) | 火苗标记 (0,0,3) | 106,80,31 | 暖色 R>G>B |
| (126,422) | rug A 左上象限 | 77,25,19 | **红主** |
| (174,470) | rug A 右下象限 | 43,39,14 | **黄主**（R≈G≫B） |
| (106,402) | rug A 透明框 | 50,49,49 | ◎地板中性色 |
| (338,432) | rug B（子矩形 UV 全面 BR） | 43,39,14 | **黄主** |
| (386,480) | rug B 角点（落在夹具透明框） | 23,24,27 | ◎地板值 |
| (100,64) | Position 窗格中心 | 183,129,0 | 解码=世界 (4,0,0) |
| (316,64) | Normal 窗格中心 | 128,128,255 | 朝上法线 |

数字会随光照/gain 调整漂移；**判定标准是色相结构**（红主/黄主/中性地板/
朝上蓝），不是逐字节相等。autoquit 的阴影数值证明（4 用例 + 尖端公式）
每次运行打印在 stdout，属自动化断言；上表为人工+脚本探针流程，**不是
CTests**——CTest 全绿不覆盖纹理视觉路径。

## 9. 真实 Sprite/Tilemap 宿主需要做什么

1. 用 `zcsr_camera` 作为唯一相机（center/ppu/height_shear），几何、光照、
   鼠标拾取全部从它取（活指针契约）。
2. 场景对象改为按面提交：`geo_face`（平色）或 `geo_tex_face`（图块），
   传世界角点 + 轴对齐法线，按 q 键远到近排序。
3. 遮挡体单独注册（footprint+height），与纹理 alpha 无关。
4. 光源实体存世界 (x,y,height)，鼠标 = `px_to_world`（地面反投影），
   高度独立控制。
5. 帧序固定：begin_geometry → geo 提交 → begin_lighting → add_light →
   end_lighting → 宿主 HUD。
6. 合成后如需读 G-buffer（调试窗格等）：`zcsr_light2d_gbuffer_handle` +
   重绑 `bind_target_textures`；光缓冲在 unit 8 需 `bind_light_texture`。
7. 已知会被阻塞的需求 → 先查第 7 节限制清单；新能力由真实宿主需求触发，
   最小适配，不预先扩建。

## 10. 相关文件

契约：`include/zcsr/light2d.h`、`include/zcsr/gl_render_light2d_ext.h`、
`include/zcsr/gl_render_l2d_geo.h`、`include/zcsr/camera.h`。
实现：`modules/light2d/light2d.c`、`modules/glrender/glrender_light2d_ext.c`、
`modules/glrender/glrender_l2d_geo.c`。参考场景：`integration/testmain.c`
（程序化夹具 + rug A/B）。验收：`tools/verify_light2d_shot.ps1`（视觉探针）、
`tests/test_light2d_math.c`（数学 ctest）。
维护者：`LIGHT2D_PIPELINE_MAP.md`；数学基准：`LIGHT2D_MATH_MODEL.md`。

### v1.1 加固增量（2026-10-26，分支 light2d-hardening-v1.1）

- R1 修复：geo 惰性初始化要求平色+纹理两 program 齐备，纹理侧创建失败
  整组回滚重试；`tex_face` 增加零程序守卫——杜绝"部分初始化后静默无渲染"
  （u_ppu=0 同类教训的第二个实例）。
- D3 部分交付：`tests/test_light2d_math.c`（ctest `light2d_math`，24 断言：
  投影往返/已知高度往返/k-shift 定律/view_rect/zoom 钳位/排序键/射线 slab
  用例含边界与退化段）。视觉路径仍需 §8 探针流程，不被 ctest 覆盖。
- 走查澄清：parallel+ray_shadows 组合正确（平行光走 2D 掩码并保留全部
  遮挡属性，light2d.c 掩码分支内 use_shadow=1）——此前风险假设不成立。

---

## v1.1 最终冻结（2026-10-26，HEAD=1911e3b）

### 能力分级（接入者必读）

**已运行验证（有 GPU 像素或 CTest 证据）**
- 世界坐标 / PPU / height-shear 投影数学
- 平色与纹理四边形进入 G-buffer
- UV 方向与 alpha-cutout 规则
- Albedo RGBA8 sRGB → 合成 pow(2.2) 线性化
- 点光源距离衰减公式
- 逐像素射线阴影（两点间接证据，非同像素因果验证）
- 29 项 CPU 数学断言 CTest 通过
- 纹理截图探针（火苗/纹理/透明区域/G-buffer 窗格）

**源码契约已确认（有代码审计，缺独立 GPU 验证）**
- Emissive RGBA16F HDR，受 Albedo 调制（非独立加性）
- tint 显示域乘法（非线性乘子）
- 遮挡棱柱（footprint × height）契约
- painter's order 排序规则
- GL 状态保存/恢复（geo begin/end 期间）

**未验证（有代码路径，缺真实场景运行证据）**
- GPU 射线阴影同像素因果验证（ENV-BLOCKED）
- 2D 挤出阴影掩码路径独立 GPU 验收
- 平行光完整场景运行表现
- resize / FBO 重建生命周期

**明确不支持（v1.1 不承诺）**
- 半透明混合与排序
- 纹理 Emissive
- 斜面法线（Normal.z 写而不读）
- Sprite / Tilemap / Instance 管线
- 图集打包与动画帧

### 最小核心光照检查结果（2026-10-26）

场景：平地 + 单点光 L=(0,0,3)，关闭 shadow，左右等距采样。
- G-buffer Position：左右一致（pos.z≈0 地面）
- G-buffer Normal：左右一致（读回 ≈(0.5,0.5) 中性地面法线）
- G-buffer Albedo：左右一致（同一地面 quad）
- atten：左右对称（RGB 完全一致）
- nl：左右不对称（P_left≈0.23, P_right≈0.03）

**结论**：atten 计算正确，nl 存在未解释差异。登记为"待确认"，
不猜测修复，不扩展诊断范围。后续如其他路径复现同一现象，再单独立项。

### 首款游戏可依赖的最小配置

第一款 2D 游戏**只依赖**以下能力：
1. 共享相机（center / ppu / height_shear）与固定世界坐标约定
2. 平色和纹理四边形（凸平面，轴对齐法线）
3. 已验证的 G-buffer 输入路径（Position / Normal / Albedo / Emissive）
4. 点光源的既有基础路径（高度 / 半径 / 衰减）
5. 明确记录的颜色空间语义（tint 显示域、Albedo sRGB、Light Buffer 线性）
6. 射线阴影模式（有间接 GPU 证据）
7. 固定渲染目标尺寸（1280×720 已验证）

**不要依赖**以下能力（v1.1 未充分验证）：
- 平行光完整运行表现
- 2D 掩码阴影独立验收
- 动态 resize / FBO 失败回滚
- 任意透明排序

### 待确认问题登记

1. GPU 射线阴影同像素因果验证：ENV-BLOCKED
   （当前两点亮度对照无法排除距离衰减混淆）
2. 光照覆盖不对称 / nl 左右差异：根因未确认
   （atten 对称，nl 不对称，Normal 读回与计算存在未解释差异）

以上两项不阻塞 v1.1 冻结。后续如真实宿主需求触发，再单独立项调查。

