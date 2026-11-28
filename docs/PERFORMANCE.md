# Performance reference

## Measurement protocol

- 同机、同 build、同条件 A/B。跨机、跨 engine、跨分辨率、跨 draw shape 不混成单一比值；跨 draw shape 只作为 control，必须标注是否为生产路径与 upload 口径。
- GPU frame 为 finish-equivalent：GL timer query 在 swap 前结束；Unity 使用 `FrameTimingManager.gpuFrameTime`。present 不计入。
- CPU 列分 `data_gen` / `submit` / `frame`。`submit` 为 draw entry 调用区间，`frame` 为 clear + draw。
- 每格为多轮 median。取不到的值记 `n/a`，不估算。
- Arc B580 无法锁定 clock。第一轮 cold clock 数据不计入；warmup ≥ 30 frames。
- Android retail device 无法锁定 clock。warmup ≥ 10 s，≥ 5 轮取 median，记录机身温度。

## Best available path (GPU-only)

Windows / Arc B580 / ZCSR GL 3.3 / Unity D3D11，65,535 instances，1920×1080，nearest，alpha 0.75，direct，单 texture，无 cull/sort。比较对象是各 engine 的实际实现选择，不是强迫 ZCSR 复制 Unity 的 65-draw 形状。

| engine | path | draw_calls | GPU frame median (ms) | GPU timing includes instance upload |
|---|---|---|---|---|
| ZCSR | plain 1 draw，`--noupload` | 1 | 0.833 | no |
| Unity | `DrawMeshInstanced` 65 draw | 65 | 1.0219 | no（`gpuFrameTime` excludes matrix upload） |

`0.833 < 1.0219`：ZCSR best available path 在 GPU-only 面低约 18%。这里的 1 draw 对 65 draw 不是把 ZCSR 改成 Unity 的形状，而是两边各自的实现选择：ZCSR 单次提交 65,535、无 1,023 上限；Unity `DrawMeshInstanced` 的 1,023 上限才产生 65 draw。ZCSR 的 65-draw 路径没有收益，反而更慢，不作为生产路径。

Unity 的 1-draw `DrawMeshInstancedIndirect` 实测 2.282，慢于 65-draw instanced 的 1.0219；因此 65 draw 是 Unity 在该 API 下的 best available path，不是 ZCSR 的。

## Control references

| control | GPU frame median (ms) | samples | note |
|---|---|---|---|
| ZCSR plain 65 draw，capacity 1,023 | 2.390 | 5 × 330 | ZCSR 对照，慢于自身 1 draw；不用于生产路径 |
| Unity `DrawMeshInstancedIndirect` 1 draw | 2.282 | 587 / 600 | 慢于 Unity 65 draw；不作为 Unity best path |
| Unity `DrawMeshInstanced` 65 draw | 1.0219 | 595 / 600 | Unity best available path |

CPU submit：ZCSR `plainrun_65draw_final`（capacity 1,023，65 draw）为 `0.104`，Unity `DrawMeshInstanced` 65 draw 为 `1.0829`，同形状提交段相差约 10.4×；ZCSR 1 draw 的提交为 0.101–0.118（`plainrun_1draw` 组）。该列不与 GPU best-path 单元混用。

Upload 口径：`0.833` 是 `--noupload` 的 GPU-only 中位（3 × 300 frames）；Unity `gpuFrameTime` 也不含 matrix upload。主表两边都排除上传，不能把它与 upload-included 的 1.468 混成同一行。`1.468` 是 make-current 修正前的 upload-included locked 值，只保留在下面的 record 对照，不是当前 1-draw 值。

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

上表 1.468 / 3.107 为每 batch make-current 修正前的同一 build。修正后 1 draw 的 `--noupload` 诊断为 0.833 ms；general 未重测。1.468 只作为 record 对照的 upload-included locked 值，不是当前 1-draw 值，也不替代 best-path 的 0.833。

## 36 B vs 168 B

隔离组：linear HDR + sort none + 无 cull，同机同 build A/B，168 B general 与 36 B compact 对照。

| platform | 168 B submit / frame (ms) | 36 B submit / frame (ms) | Δ submit / Δ frame (ms) |
|---|---|---|---|
| Windows / B580 | 1.0418 / 2.1023 | 0.5072 / 1.5286 | −0.5346 / −0.5737 |
| Linux / GTX 1650 | 3.1922 / 7.3014 | 0.9434 / 6.2405 | −2.2488 / −1.0609 |

36 B 在 PC 隔离组降低 submit 与 frame，因此 `compact` layout 不是移动端专属；是否使用由 product 场景决定。
