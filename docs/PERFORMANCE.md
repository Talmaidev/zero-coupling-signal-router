# Performance reference

## Measurement protocol

- 同机、同 build、同条件 A/B。跨机、跨 engine、跨分辨率、跨 draw shape 不混表。
- GPU frame 为 finish-equivalent：GL timer query 在 swap 前结束；Unity 使用 `FrameTimingManager.gpuFrameTime`。present 不计入。
- CPU 列分 `data_gen` / `submit` / `frame`。`submit` 为 draw entry 调用区间，`frame` 为 clear + draw。
- 每格为多轮 median。取不到的值记 `n/a`，不估算。
- Arc B580 无法锁定 clock。第一轮 cold clock 数据不计入；warmup ≥ 30 frames。
- Android retail device 无法锁定 clock。warmup ≥ 10 s，≥ 5 轮取 median，记录机身温度。

## ZCSR 1 draw vs Unity 65 draw

Windows / Arc B580 / GL 3.3 / D3D11，65,535 instances，1920×1080，nearest，alpha 0.75，direct，单 texture，一次 draw，无 cull/sort。

| path | draw shape | GPU frame median (ms) | warm range (ms) | samples / draw_calls |
|---|---|---|---|---|
| ZCSR `zcsr_gl_draw_instances_plain`，capacity 65,535 | 1 draw | 0.833 | 0.821–0.842 | 5 × 330；draw_calls=1；upload once |
| Unity `DrawMeshInstanced`，D3D11 | 65 draw | 1.0219 | n/a | gpu_valid 595/600 |

| metric | ZCSR | Unity |
|---|---|---|
| GPU frame median | 0.833 ms | 1.0219 ms |
| CPU submit | 0.104 ms | 1.0829 ms |

ZCSR 1 draw 对 Unity 65 draw：GPU frame 快 18%，CPU submit 快约 10×。

ZCSR 不提供 chunk entry；每个 range 一次调用。`count` 超过 `ZCSR_GL_MAX_INSTANCES` 时由 engine 内部按 capacity 分 chunk，caller 不传 chunk 参数。拆分为多个 1,023-record 调用不进入性能路径。

Unity 的 65 draw 来自 `Graphics.DrawMeshInstanced` 的单次 1,023 instance 上限：65,535 / 1,023 向上取整为 65。每轮另执行 1,023 个 64 B `Matrix4x4` 的 `Array.Copy` 与 1,023 个 16 B UV 的 `MaterialPropertyBlock.SetVectorArray`。该形状是 API 上限的 workaround，不是性能算法。

Upload 口径：ZCSR 的 GPU frame 包含 entry 内 `glBufferSubData` 2.62 MB（40 B × 65,535）；Unity `gpuFrameTime` 不包含每帧 4.19 MB（64 B × 65,535）matrix upload。ZCSR 在更宽的计时面内仍领先 18%。

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

上表两组数字为每 batch make-current 修正前的同一 build。修正后 plain 1 draw frame median 为 0.833 ms；general 未重测，1.468 ms 不是当前值。

## 36 B vs 168 B

隔离组：linear HDR + sort none + 无 cull，同机同 build A/B，168 B general 与 36 B compact 对照。

| platform | 168 B submit / frame (ms) | 36 B submit / frame (ms) | Δ submit / Δ frame (ms) |
|---|---|---|---|
| Windows / B580 | 1.0418 / 2.1023 | 0.5072 / 1.5286 | −0.5346 / −0.5737 |
| Linux / GTX 1650 | 3.1922 / 7.3014 | 0.9434 / 6.2405 | −2.2488 / −1.0609 |

36 B 在 PC 隔离组降低 submit 与 frame，因此 `compact` layout 不是移动端专属；是否使用由 product 场景决定。
