# Performance reference

## Measurement protocol

- 同机、同 build、同条件 A/B。跨机、跨 engine、跨分辨率、跨 draw shape 不混成单一比值；shape matrix 单元格必须标注 API、locked/current 与 upload 口径。
- GPU frame 为 finish-equivalent：GL timer query 在 swap 前结束；Unity 使用 `FrameTimingManager.gpuFrameTime`。present 不计入。
- CPU 列分 `data_gen` / `submit` / `frame`。`submit` 为 draw entry 调用区间，`frame` 为 clear + draw。
- 每格为多轮 median。取不到的值记 `n/a`，不估算。
- Arc B580 无法锁定 clock。第一轮 cold clock 数据不计入；warmup ≥ 30 frames。
- Android retail device 无法锁定 clock。warmup ≥ 10 s，≥ 5 轮取 median，记录机身温度。

## Draw shape matrix (ZCSR GL vs Unity D3D11)

Windows / Arc B580 / ZCSR GL 3.3 / Unity D3D11，65,535 instances，1920×1080，nearest，alpha 0.75，direct，单 texture，无 cull/sort。ZCSR 与 Unity 各自按可用 API 形状提交；单元格不是同形状 A/B。

| draw shape | ZCSR GL 3.3 | Unity D3D11 |
|---|---|---|
| 1 draw | 1.468（plain，upload included） | 2.282（`DrawMeshInstancedIndirect`） |
| 65 draw | 2.390（plain，capacity 1,023） | 1.0219（`DrawMeshInstanced`） |

ZCSR 1→65 draw：1.468→2.390（+0.922 ms）；Unity 65→1 draw：1.0219→2.282（+1.260 ms）。四个 reference cell 上形状方向相反；但 ZCSR 1-draw 是 pre-fix locked、2.390 是 current，不能把这个差值当作同 build 形状效应。矩阵也不能归因于 draw 形状、GL/D3D11 或 engine 净差中的任何一项。

| reference cell | GPU frame median (ms) | samples | GPU timing includes instance upload |
|---|---|---|---|
| ZCSR plain 1 draw，upload included，pre-fix locked | 1.468 | 5 × 150 | yes（2.62 MB entry upload） |
| ZCSR plain 1 draw，GPU-only `--noupload` | 0.833 | 3 × 300 | no |
| ZCSR plain 65 draw，capacity 1,023 | 2.390 | 5 × 330 | yes（same 2.62 MB，split 65 calls） |
| Unity `DrawMeshInstancedIndirect` 1 draw | 2.282 | 587 / 600 | no（`gpuFrameTime` excludes 5.24 MB record upload） |
| Unity `DrawMeshInstanced` 65 draw | 1.0219 | 595 / 600 | no（`gpuFrameTime` excludes 4.19 MB matrix upload） |

`0.833` 是 3 轮 `--noupload` 的 GPU-only 中位（3 × 300 frames），原始数值低于 Unity `1.0219`；但它对应 1 draw 且不含 2.62 MB 上传，不能作为“1:65 同形状、同上传”的控制变量胜负。`1.468` 是 upload-included 的 pre-fix locked 1-draw 参考，`2.390` 是 current 65-draw；两者不是同一 build。

CPU submit：`0.104` 出自 ZCSR `plainrun_65draw_final`（capacity 1,023，65 draw），`1.0829` 出自 Unity `DrawMeshInstanced` 65 draw；两者同形状，提交段相差约 10.4×。ZCSR 1 draw 的提交为 0.101–0.118（`plainrun_1draw` 组）。该列不与 GPU 矩阵单元混用。

Upload 口径：ZCSR `1.468` / `2.390` 的 GPU 窗口包含 entry 内 2.62 MB `glBufferSubData`；Unity `gpuFrameTime` 不包含 4.19 MB matrix 或 5.24 MB indirect record upload。两列口径不对称，不能直接相减当作纯渲染差；`0.833` 是排除 upload 的 `--noupload` 口径。

Unity API：`DrawMeshInstanced` 单次 1,023 instance 上限，65,535 需 65 draw。`DrawMeshInstancedIndirect` 可 1 draw 提交 65,535，实测 2.282，比 65 draw instanced 的 1.0219 慢。准确说法是该 API 的提交形状上限，不是 Unity 不能单次 draw。indirect shader 未按实例切 UV，与 instanced 的 UV 路径不同，见 `gpu-shape.md` §4。

## Complex linear path (ZCSR only)

Windows / Arc B580，`full` layout，`linear` HDR，`sort none`，65,535 instances，1920×1080，general 168 B record，1 draw，finish，5 × 330 samples。

| path | GPU frame median (ms) | GPU range (ms) | CPU submit (ms) | frame median (ms) |
|---|---|---|---|---|
| general 168 B linear，no optional stage | 1.263 | 1.165–1.370 | 1.67 | 4.03 |
| general 168 B linear + hue / ramp / glow / chroma / emiss / rim | 6.266 | 6.238–7.242 | 1.67 | 9.43 |

Complex 组激活 `hue_amount`、`ramp_amount`、`glow_amount`、`sat_boost` / `sat_floor` / `value_gain` / `value_contrast`、`emiss_amount`、`rim_amount`；ramp atlas 绑定到 texture row 0。Pixel coverage 为 2,073,481 / 2,073,600 = 99.9943%，证明 frame 非空、不是空 draw；entry 返回值用于检查 submitted records。

Unity benchmark 只有 `DrawMeshInstanced` 与 indirect sprite shader，没有 linear HDR、ramp/recolor、Light2D 或 shadow 对应实现。该组为 ZCSR path validation，不作跨 engine 比值。

## Reproduction

Linear complex：

```
cd build\bench-visible\Release
zcsr_stress_iso168hdr.exe --count 65535 --frames 330 --warmup 30 \
  --path general --capacity 65535 --shared-cfg --finish \
  --gpu-timer --gpu-scope frame --complex
```

`build\bench-visible` 的 cache 必须为：

```
ZCSR_INSTANCE_LAYOUT=full
ZCSR_COLOR_TIER=linear
ZCSR_SORT_TIER=none
```

Direct plain baseline：

```
cd build\bench-plain-direct\Release
zcsr_stress_plain.exe --count 65535 --frames 330 --warmup 30 \
  --path plain --capacity 65535 --shared-cfg --finish \
  --gpu-timer --gpu-scope frame
```

`build\bench-plain-direct` 的 cache 必须为 `full / direct / none`。两组使用同一 texture、同 count、同 client size、同 finish 口径。

## Plain 40 B vs general 168 B

Windows / Arc B580，65,535 instances，1920×1080，nearest，alpha 0.75，direct，单 texture，一次 draw，无 cull/sort；5 × 150 coherent query samples，finish 口径。

| path | vertex / attribute / record | frame clear+draw (ms) | draw-only (ms) |
|---|---|---|---|
| `zcsr_gl_draw_instances_plain` | 4 vertex / 5 attribute / 40 B | 1.468 | 1.416 |
| `zcsr_gl_draw_instances` general | 6 vertex / 13 attribute / 168 B | 3.107 | 2.642 |

Pixel gate：general（tint=1，alpha 0.75，rot=0）与 plain 在同帧 65,535 instances 的 offscreen dump SHA-256 相同：

```
C12A1564F7742DBFB5A6E276793EEB7732BB4C69D74E70CF349A7821963FF17F
```

0 byte difference；两路 `draw_calls=1`。同 pixel、同 overdraw 下，general 多出的 GPU time（frame +1.639 ms、draw-only +1.226 ms）来自 vertex assembly（6→4）与 instance record / attribute bandwidth（13→5 attribute、168 B→40 B，含每帧 entry 内 VBO upload 11.01 MB→2.62 MB），不是 fill rate。

上表 1.468 / 3.107 为每 batch make-current 修正前的同一 build。修正后 1 draw 的 `--noupload` 诊断为 0.833 ms；general 未重测。1.468 只作为 shape matrix 的 locked 1-draw 参考，不是修正后同 build、upload-included 的当前值。

## 36 B vs 168 B

隔离组：linear HDR + sort none + 无 cull，同机同 build A/B，168 B general 与 36 B compact 对照。

| platform | 168 B submit / frame (ms) | 36 B submit / frame (ms) | Δ submit / Δ frame (ms) |
|---|---|---|---|
| Windows / B580 | 1.0418 / 2.1023 | 0.5072 / 1.5286 | −0.5346 / −0.5737 |
| Linux / GTX 1650 | 3.1922 / 7.3014 | 0.9434 / 6.2405 | −2.2488 / −1.0609 |

36 B 在 PC 隔离组降低 submit 与 frame，因此 `compact` layout 不是移动端专属；是否使用由 product 场景决定。
