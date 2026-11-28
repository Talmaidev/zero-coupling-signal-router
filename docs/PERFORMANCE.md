# Performance reference

## Measurement protocol

- 同机、同 build、同条件 A/B。跨机、跨 engine、跨分辨率、跨 draw shape 不混成单一比值；跨 draw shape 只作为 control，必须标注是否为生产路径与 upload 口径。
- GPU frame 为 finish-equivalent：GL timer query 在 swap 前结束；Unity 使用 `FrameTimingManager.gpuFrameTime`。present 不计入。
- CPU 列分 `data_gen` / `submit` / `frame`。`submit` 为 draw entry 调用区间，`frame` 为 clear + draw。
- 每格为多轮 median。取不到的值记 `n/a`，不估算。
- Arc B580 无法锁定 clock。第一轮 cold clock 数据不计入；warmup ≥ 30 frames。
- Android retail device 无法锁定 clock。warmup ≥ 10 s，≥ 5 轮取 median，记录机身温度。

## Best available path (GPU cost, not draw count)

Windows / Arc B580 / ZCSR GL 3.3 / Unity D3D11，65,535 instances，1920×1080，nearest，alpha 0.75，direct，单 texture，无 cull/sort。指标是每帧 GPU 成本；draw count 只是 API 上限造成的提交形状，不列入主表、不作比较项。

| engine | path | GPU frame median (ms) | samples | GPU timing includes instance upload |
|---|---|---|---|---|
| ZCSR | 1 draw，upload included，current | 0.847 | 5 × 300 | yes（2.62 MB entry upload） |
| ZCSR | 1 draw，GPU-only `--noupload` | 0.833 | 3 × 300 | no |
| Unity | `DrawMeshInstanced`（1,023 cap） | 1.0219 | 595 / 600 | no（`gpuFrameTime` excludes 4.19 MB matrix upload） |

`0.847 < 1.0219`：即使 2.62 MB 上传只计入 ZCSR 一侧，每帧 GPU 成本仍低约 17%。`0.833 < 1.0219` 是两边都排除上传的 GPU-only 面，低约 18.5%。

ZCSR 单次提交 65,535，capacity 即计数上限；Unity `DrawMeshInstanced` 的 1,023 上限才把同一负载拆成 65 次。65 次是接口限制的结果，不是性能算法，也不作为胜负指标。ZCSR 自身跑 65 draw 对照为 2.390，反而慢于 1 draw，因此 65 draw 不进入生产路径。

Unity 的 1-draw `DrawMeshInstancedIndirect` 实测 2.282，慢于 `DrawMeshInstanced` 的 1.0219；因此对 Unity 而言，带 1,023 上限的 `DrawMeshInstanced` 是 best available path。

## Control references

| control | GPU frame median (ms) | samples | note |
|---|---|---|---|
| ZCSR 1,023-cap control | 2.390 | 5 × 330 | 慢于自身 1 draw；不用于生产路径 |
| Unity indirect 1-draw control | 2.282 | 587 / 600 | 慢于 Unity 1,023-cap control；不作为 Unity best path |
| Unity 1,023-cap control | 1.0219 | 595 / 600 | Unity best available path |

CPU submit：ZCSR `plainrun` 1,023-cap control 为 `0.104`，Unity `DrawMeshInstanced` 同形状 control 为 `1.0829`，提交段相差约 10.4×；ZCSR 1 draw 的提交为 0.101–0.118（`plainrun_1draw` 组）。该列不与 GPU best-path 单元混用。

Upload 口径：`0.847` 含 2.62 MB entry upload，`0.833` 是 `--noupload`；Unity `gpuFrameTime` 不含 4.19 MB matrix upload。`0.847` 对 `1.0219` 是“上传只计入 ZCSR 一侧”的保守比较；`0.833` 对 `1.0219` 是两边都排除上传的 GPU-only 比较。两行不可互换。

Unity API：`DrawMeshInstanced` 单次 1,023 instance 上限，65,535 必须拆成多次提交；`DrawMeshInstancedIndirect` 可 1 draw 提交 65,535，实测 2.282，慢于 `DrawMeshInstanced` 的 1.0219。准确说法是该 API 的提交形状上限，不是 Unity 不能单次 draw。indirect shader 未按实例切 UV，与 instanced 的 UV 路径不同，见 `gpu-shape.md` §4。

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

上表 1.468 / 3.107 为每 batch make-current 修正前的同一 build。修正后 1 draw 的 `--noupload` 诊断为 0.833 ms；general 未重测。1.468 只作为 record 对照的 upload-included locked 值，不是当前 1-draw 值，也不替代 best-path 的 0.833。

## 36 B vs 168 B

隔离组：linear HDR + sort none + 无 cull，同机同 build A/B，168 B general 与 36 B compact 对照。

| platform | 168 B submit / frame (ms) | 36 B submit / frame (ms) | Δ submit / Δ frame (ms) |
|---|---|---|---|
| Windows / B580 | 1.0418 / 2.1023 | 0.5072 / 1.5286 | −0.5346 / −0.5737 |
| Linux / GTX 1650 | 3.1922 / 7.3014 | 0.9434 / 6.2405 | −2.2488 / −1.0609 |

36 B 在 PC 隔离组降低 submit 与 frame，因此 `compact` layout 不是移动端专属；是否使用由 product 场景决定。
