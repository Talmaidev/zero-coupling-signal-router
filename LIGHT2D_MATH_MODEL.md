# LIGHT2D_MATH_MODEL.md — 数学与设计基准（证据分级）

回答"坐标、投影、材质、光照、阴影的统一数学约定是什么、证据是什么"。
接入文档=LIGHT2D_INTEGRATION.md，结构地图=LIGHT2D_PIPELINE_MAP.md。

证据等级：**CURRENT**=当前源码核实的实际行为；**CONTRACT**=公开接口/文档
承诺；**PROPOSED**=建议目标模型（不代表实现）；**UNKNOWN**=未查清；
**UNSUPPORTED**=明确不支持。同一约定可同时标多个等级并说明差异。

---

## 1. 坐标系统与投影

### 1.1 世界坐标 【CURRENT】
X 东、Y 南、Z 上；地面 z=0。几何角点、G-buffer Position、光源位置、
遮挡体全部同一世界系（gl_render_l2d_geo.h、light2d.h 注释；测试：
test_light2d_math 地面往返）。

### 1.2 Height-shear 投影 【CURRENT】
```
s_x = W/2 + (x − c_x)·p
s_y = H/2 + ((y − c_y) − k·z)·p
```
- 实现：camera.c `world_to_px`；geo VS（同式，k 经 u_k uniform）；
  testmain `project()` 镜像（LOW 路径）。三处一致由 test_light2d_math
  断言（投影往返、k-shift 定律：`ay − by = Δk·z·ppu`，k 增大→屏幕上移）。
- k 唯一权威 = `zcsr_camera.height_shear`（camera.h）；shader 内无剪切
  常量（grep 核对：geo shader 的 0.5 仅为视口居中与法线编码）。
- 视口 W/H 取调用时 GL_VIEWPORT（geo_begin :284）——渲染目标为权威，
  相机 view 字段仅咨询。
- p（PPU）来自相机，任何模块不硬编码（基线调试值 96）。

### 1.3 逆变换 【CURRENT】
地面（z=0，鼠标语义）：`x = c_x + (s_x−W/2)/p`，`y = c_y + (s_y−H/2)/p`
（camera.c px_to_world——**恒为地面反投影**，灯高独立于鼠标）。
已知高度 z 的通用逆：`y = c_y + (s_y−H/2)/p + k·z`
（test_light2d_math "elevated round-trip" 断言）。
单凭屏幕坐标不能同时定 x/y/z——逆变换必须指定 z。

### 1.4 投影不变量 【CURRENT，自动化】
test_light2d_math（ctest `light2d_math`，24 断言）：z=0 往返、已知 z 往返、
k-shift 定律、view_rect、zoom 钳位 [8,1024]、排序键序、slab 用例。
G-buffer 对 k 不变（世界数据不随投影改写）= 场景级属性，Q3 双 k 截图
探针运行验证（Position/Normal 窗格逐字节不变）。

## 2. 几何输入与 G-buffer

### 2.1 面数据 【CURRENT】
`G = (P0..P3 世界角点边界序, [UV×4], tint RGBA, N 世界轴法线, E RGBA)`；
四边形拆 (0,1,2)+(0,2,3)。v1 限简单凸平面四边形（契约，gl_render_l2d_geo.h）。

### 2.2 G-buffer 附件 【CURRENT】

| 附件 | 格式 | 数学职责 | 写入 | 读取 |
|---|---|---|---|---|
| G0 Albedo | RGBA8 | 材质色（显示域，见 §5） | geo FS | 合成 FS |
| G1 Position | RGBA16F | 世界 XYZ（清屏远哨兵=空气） | geo FS（varying 直传） | light FS |
| G2 Normal | RGBA8 | 表面朝向（编码见 §2.3） | geo FS | light FS（仅 xy） |
| G3 Emissive | RGBA16F | 线性自发光 | geo FS（uniform） | 合成 FS |

禁止 `gl_FragCoord` 反推 Position（丢 z；历史教训）。

### 2.3 法线 【CURRENT + UNSUPPORTED 边界】
写入：`enc = n*0.5+0.5`（z 分量有值）。光照解码（ext.c light FS）：
`xy = enc.xy*2−1`；xy=(0,0)→**朝上 (0,0,1)**；否则 `normalize(nx,ny,0)`。
**z 在光照时被忽略**（D2）→ 支持：朝上面、平面内轴对齐立面；
不支持：斜面/曲面（其 z 贡献丢失，漫反射按平面内投影错误计算）。
完整 3D 编码解码（`n = normalize(2·enc−1)` 全 RGB）= PROPOSED，
触发条件=真实斜面需求；届时改 light FS 解码+geo 契约，G2 格式不变
（z 通道已在写）。

## 3. 纹理、Alpha 与颜色空间

### 3.1 采样与 cutout 【CURRENT】
`C = texture(UV) ⊙ tint`（逐通道）；**`C.a < 0.5 → discard`**（严格小于；
glrender_l2d_geo.c:92）。丢弃片元不写任何 attachment；通过者按表面写入。
UV：(u0,v0)=子矩形图像左上，v 向图像底（与 sprite 管线一致；运行证据=
红象限探针证伪翻转）。UNSUPPORTED：任意半透明混合/排序；alpha→遮挡体。

### 3.2 颜色空间 【CURRENT】
纹理以普通 GL_RGBA 上传（无 sRGB 内部格式）→ GPU 无解码。
**全链路唯一线性化 = 合成 FS 的 `A_lin = pow(max(A_stored,0), 2.2)`**
（ext.c deferred/bloom FS）。因此 texel 直写 G0（同平色路径编码），
纹理路径禁止二次解码。输出：`C_out = (C_lin/(C_lin+1))^(1/2.2)`，
tonemap 与 gamma 在同一合成 FS。

**tint / albedo 的实际乘法空间（合并前审计修正）**：geo FS 在
显示域直接做 `rgba = texture(UV) ⊙ tint` 后写入 G0（RGBA8），
合成时才整体 `pow(2.2)`。因此 tint 与平色 albedo_rgba 实际是
**显示域（gamma）乘子**，与纹理想素同空间相乘——**不是线性空间
乘法**。公开头文件早期注释中的"linear tint / linear albedo"措辞
不准确，v1.1 起改为"显示域值，直写 G0，合成时统一线性化"。
数学推论：`(texel·tint)^2.2 = texel^2.2 · tint^2.2`，tint 本身
也被 gamma 化；要得到真正的线性 tint 需在 geo FS 内先对 texel
`pow(1/2.2)`、乘线性 tint、再由合成 FS 不重复线性化——这是
PROPOSED 的 sRGB 工作流迁移，**本次不改**。

**HDR >1 的适用范围**：G0 是 RGBA8，tint/albedo 乘积 >1 会被
归一化格式截断到 1.0，**G0 路径不能承载 HDR Albedo**。
HDR >1 仅在 RGBA16F 附件上保留：G3 Emissive 与光缓冲（light
accumulation RGBA16F）。完整 sRGB 工作流迁移 = PROPOSED（需单独
立项规划三阶段：纹理解码/光照域/输出编码），本次不改。

## 4. 光照模型

### 4.1 点光源几何 【CURRENT】
光位 L_p=(lx,ly,lz)（世界，lz=height）；受光面 P 来自 G1（世界 XYZ）。
`D = L_p − P`；`dist = |D|`（**3D 含高度**）；`L̂ = D/max(dist,1e-6)`
（shader 内 dist<1e-6 时取 (0,0,1) 保护）。

### 4.2 漫反射 【CURRENT】
`d = max(0, N·L̂)`，N 按本文件 §2.3 解码（纯项，无底薪——历史缺陷
0.45 底薪已在第八轮移除）。朝上面受光完全来自光高的仰角贡献：
`N=(0,0,1)` 时 `d = lz/dist`。

### 4.3 衰减 【CURRENT】
```
r = dist / R                    （R = light.radius，世界单位）
a = pow(clamp(1−r,0,1), decay) · 1/(dist/1.0 + 1)
```
第一项=半径内幂衰减（decay 光源参数）；第二项=世界单位距离软化
（分母标定 1.0=一个参考瓦片）。r≥1 时第一项为 0（半径外无光）。
PROPOSED 候选（未来统一用，当前不用）：`a = max(0,1−r²)²`——平滑边界
变体；更换=视觉变更，需真实需求。

### 4.4 累积与合成 【CURRENT】
每光一次 ADD quad（平行光=全屏）：贡献 = `color.rgb × a × d × V × gain`
（gain=全局 options.gain；逐光强度=a 已乘进 color——light2d.c draw_high）。
合成（ext.c deferred/bloom FS 直查）：
```
C_lin = albedo_lin ⊙ (E_lin + Σ light_lin + vec3(0.03))    （环境光 0.03 固定）
C_out = ( C_lin/(C_lin+1) )^(1/2.2)
```
**Emissive 当前语义（合并前审计明确）**：E_lin 在合成中**先与光照/
环境光相加，再被 albedo_lin 逐通道调制**——即发光面的颜色也受其
albedo 染色。这**不是**教科书式的独立加性自发光
`C_lin = albedo_lin ⊙ (Σ light_lin + amb) + E_lin`；后者在当前
管线中不成立（Bloom 路径同样把 bloom9(E) 放入 albedo_lin 的括号内）。
该行为由源码确定，v1 不修改；若未来需要独立加性自发光，应作为
PROPOSED 单独立项并重新跑合成回归。半透明混合 = UNSUPPORTED
（无逐层混合项）。

### 4.5 平行光 【已声明未确认（代码在，无运行证据）】
方向 = 归一化 (dir_x,dir_y,dir_z)（默认语义对齐参考引擎 (0,−1,1).unit）；
无距离衰减（全屏）；`d = max(0,N·(−dir̂))`；阴影沿 dir 的平面投影挤出
（2D 掩码，含全部遮挡属性）。LOW 档跳过平行光。

## 5. 射线阴影

### 5.1 线段测试 【CURRENT】
`V(P,L_p)=0 ⟺ ∃遮挡体: 线段 L_p→P 与棱柱 footprint×[0,H] 相交`。
实现=light FS `zcsr_ray_blocked`（AABB 三轴 slab，t∈(1e-4,1−1e-4)；
某轴 |D_j|<1e-6 时退化为原点在 slab 内判定）。数值边界：端点 ε 使
光点/接收面不自遮挡；退化线段（P=L_p）判定不受遮挡。
宿主镜像与自动化：testmain `seg_blocked`（autoquit 打印 4 个密封用例）
+ tests/test_light2d_math.c（4 密封用例 + 平行射线 + 影子尖端公式
x_tip = xmax·Lz/(Lz−H) 含 Lz=3/5 双光高与 near-H 长影边界 + 退化段，
全部确定性断言）。

### 5.2 遮挡体参数语义 【CURRENT + G1 裁决】
- ray 路径消费：footprint AABB + height（棱柱高度真实参与：光越低影越长。
  对地面接收点（z=0），光线越过遮挡体远边顶棱 (x_max,0,H) 后落地的
  尖端由相似三角形给出：
  `x_tip = x_max · L_z / (L_z − H)`，适用条件 `L_z > H`。
  数值：H=2、x_max=3 时，L_z=3 → x_tip=9、L_z=5 → x_tip=5、
  L_z→H⁺ → x_tip→∞。ctest 双光高 + near-H 边界断言。）
- `factor/distance/reverse/self_shadow`：2D 挤出掩码路径消费
  （浓度/有限带/反向/自阴影填充）；ray 路径不消费（硬 1.0/无限楔）——
  **契约已声明**（light2d.h ray_shadows 注释、INTEGRATION §7），
  裁决=结论 3（非缺陷）。平行光无论 ray 开关都走掩码路径（属性齐全），
  走查证据 light2d.c:702-740（"parallel+ray 丢阴影"假设被证伪）。
- alpha 与遮挡分离：cutout 只决定 G-buffer 片元生死（§3.1），
  永不生成/裁剪遮挡几何。UNSUPPORTED：纹理 alpha 自动遮挡。

### 5.3 排序 【CURRENT 限制】
无深度附件 → painter's order（宿主远到近）。参考键
`q = k·(y−c_y) + z`（升序）仅适用于当前支持的简单非穿插几何；
`world_z` 单独不是通用深度；任意 2.5D 排序 = UNSUPPORTED，
等真实宿主需求。（投影公式定屏幕位置，不定遮挡次序——两者不可混用。）

## 6. 阶段契约（Input→Transform→Output）

| 阶段 | 输入 | 输出 | 责任文件 | 失败行为 |
|---|---|---|---|---|
| 投影 | 世界点+相机 | gl_Position / 屏幕像素 | camera.c、geo VS、testmain 镜像 | 参数非法=false |
| G-buffer 写入 | 面+材质 | 四附件 | glrender_l2d_geo.c | 校验失败=false，无写入 |
| 阴影 | G1+光+遮挡体 | V 因子 | ext.c light FS / light2d.c | 盒数>8 截断 |
| 光累积 | G-buffer+光 | 光缓冲 RGBA16F | ext.c light program | emit 失败=false |
| 合成 | 光缓冲+G0+G3 | 窗口默认目标 | ext.c deferred/bloom FS | 目标绑定失败=false |

同一约定唯一权威：投影/逆变换/往返→本文档 §1 + camera.h；alpha→§3.1
+ geo 头注释；遮挡语义→§5.2 + light2d.h。其他文档引用不复制。

## 7. 验证矩阵（项目→不变量→方式）

| 验证项 | 不变量 | 方式 | 状态 |
|---|---|---|---|
| 投影往返 | 已知世界点恢复原坐标 | test_light2d_math | ✅自动 |
| k-shift | Δk 只动屏幕（ay−by=Δk·z·p），世界不动 | ctest + Q3 双 k 探针 | ✅自动+运行 |
| G1 Position | 世界 XYZ 非 gl_FragCoord | 源码审计 + Position 窗格解码 | ✅ |
| Normal | 编码解码一致；支持域=朝上+轴对齐 | 源码审计（D2 边界） | ✅审计 |
| Alpha-cutout | <0.5 丢；=0.5 过（严格小于） | shader :92 + rug 框探针 | ✅审计+运行 |
| UV | 全区域/子矩形采样正确 | rug A/B 探针 | ✅运行 |
| 光照方向 | max(0,N·L̂) 逐样例 | 源码审计（公式直查） | 审计（无数值单测） |
| 衰减 | r≥1 无第一项 | 源码审计 | 审计 |
| 射线相交 | 4 密封用例+边界 | ctest + autoquit proof | ✅自动 |
| 颜色空间 | 线性化仅合成一处 | 源码 grep（无二次 pow2.2） | ✅审计 |
| 合成目标 | 结果落默认目标 | v15 修复 + 探针 | ✅运行 |
| 平行光 | — | 无运行证据 | ⚠️未验证 |

## 8. 完成标准自查（对应补充令 §十二）

1. 世界→屏幕：§1.2（三处实现一致，ctest 断言）。
2. 几何→四附件：§2.2 生产者/消费者表。
3. Normal 支持域：§2.3（朝上+轴对齐；斜面=PROPOSED 触发条件）。
4. 线性化点：§3.2（合成 FS 唯一）；输出编码：同 FS ^(1/2.2)。
5. 光方向/衰减/可见性/emissive：§4.2-4.4（合成表达式逐因子）。
6. 射线=线段（非无限射线）；遮挡体=footprint×[0,H] 棱柱（§5.1-5.2）。
7. 排序=painter's order（无深度测试），q 仅参考键（§5.3）。
8. 改任意管线后的回归矩阵：PIPELINE_MAP §14 + INTEGRATION §8 +
   本文档 §7。
9. 证据分级已逐项标注（CURRENT/PROPOSED/UNSUPPORTED/未验证）。
10. 宿主接入不可变约定：世界系、height_shear 单源、G-buffer 四通道、
    alpha-cutout 阈值、painter 排序责任、地面鼠标反投影。
