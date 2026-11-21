# ZCSR 日志系统重构规格（2026）

> 状态：P0–P3 已实施（2026-11-16），2026-11-17 复查补丁已落地；工作区未提交，等待整体审核。
> 范围：`include/zcsr/log.h`、`include/zcsr/diagnostics.h`、`modules/core/log.c`，以及全部模块的日志调用点。
> 非范围：渲染、音频、UI 的功能改动；第三方日志库引入。

---

## 0. 结论

1. **不需要再封装一层 SDL Log 形状的 API。** `zcsr_log` 已经是那层封装；再造一层只会多一套词汇，不增加能力。
2. **`zcsr_log` 方向正确，P0–P3 后结构上避开了 SDL2 的依赖、堆、编译期剥离、归属、首次竞态、重入六类问题；printf 的类型/宽度/locale 问题靠 GCC/Clang 的 `format(printf)` 属性、`PRI*` 宏和类型化字段 API 收窄，但 MSVC 默认不检查用户函数（SAL `_Printf_format_string_` 还需 `/analyze`），调用点仍必须遵守契约。**
3. **重构目标不是“换 sink”，是把 printf 模型换成可检查的日志模型。** 格式化字符串 `(fmt, ...)` 本身就是类型不安全的根源；只换输出端，坑还在。
4. **C11 不需要宽度别名。** `<stdint.h>` 已经提供 `uint32_t`；需要补的是 `_Static_assert`、`PRI*` 打印宏和强类型句柄，不是 `ZCSR_U32`。
5. **优先级：先修两个 bug，再补 per-tag 级别与限流，最后做结构化与崩溃路径。** 顺序不能倒。

### 0.1 两个必须先修的硬伤

> 状态：已在 2026-11-16 修复，验证记录见 §6。

| ID | 位置 | 问题 | 修法 |
|---|---|---|---|
| BUG-1 | `modules/core/log.c:69` | Android 参数错位：`__android_log_print(prio, "%s", tag, "%s", msg)`，tag 变成字面量 `"%s"`、fmt 变成模块名、msg 从未被消费。logcat 里只有 `%s: resource`，没有消息正文。 | `__android_log_print(prio, tag, "%s", msg);` |
| BUG-2 | `modules/core/log.c:74-81` | 二级截断把 `snprintf` 写入的 NUL 覆盖成 `'\n'`：`line[2047] = '\n'`，数组失去终止符，`fputs` 越界读栈。 | `line[sizeof(line) - 2] = '\n'; line[sizeof(line) - 1] = '\0';` |

BUG-2 只在默认 sink 触发；自定义 handler 拿到的是 `buf`，那条路径的终止是正确的。

### 0.2 类型别名的结论（与日志的交集）

SDL 2.32.8 的 `Sint8..Uint64` 实际是：

```c
typedef int8_t Sint8;  typedef uint8_t Uint8;
typedef int16_t Sint16; typedef uint16_t Uint16;
typedef int32_t Sint32; typedef uint32_t Uint32;
typedef int64_t Sint64; typedef uint64_t Uint64;
SDL_COMPILE_TIME_ASSERT(uint32, sizeof(Uint32) == 4);  /* 共 8 条 */
```

它的价值是三层：**稳定拼写、编译期宽度断言、`SDL_PRIu64` 打印宏**；不是类型安全。C11 已经保证 `<stdint.h>` 存在，所以：

- 不需要 `ZCSR_U32` 之类的宽度别名；`uint32_t` 就是最稳的拼写。
- 需要补的是：每个公开 ID 的 `_Static_assert(sizeof(...) == N)`；`PRId64/PRIu32` 的封装；需要强类型时用 `struct { uint32_t v; }` 或 opaque pointer，而不是 typedef。
- 对日志的直接含义：整数参数不要写 `%u/%d/%ld`，统一走 `PRI*` 宏或类型化字段 API（见 §4.1）。

---

## 1. 业界对照（2026）

| 系统 | 编译期 | 运行期 | 格式化 | 落点/内存 | 值得抄 | 不要抄 |
|---|---|---|---|---|---|---|
| UE5 | 每个 category 有 `CompileTimeVerbosity`；Shipping 可整体编译掉 | `-LogCmds`、控制台 `log Cat verbosity`、`[Core.Log]` ini | `UE_LOG` printf 风格；`UE_LOGFMT`（5.2+）结构化、位置/命名字段 | `FOutputDevice` 多 sink；`Saved/Logs/*.txt` | per-category 编译期上限；Fatal 在日志关闭时仍打印并崩溃；Log 级别只进文件不进控制台 | 把 category 做成固定 enum，扩展要改引擎头 |
| Unity | 无（Debug.Log 调用不被剥离） | `ILogger.logLevel`、`Debug.unityLogger.logEnabled` | `Debug.Log(object)`，字符串拼接，无类型检查 | Editor/Player log 文件 | `context` 对象把日志关联到场景对象 | 无 tag/category；拼接分配；发布构建仍执行 |
| Godot | `print_verbose` 受 verbose 开关；release 不逐条 flush stdout | `--verbose`、`--quiet`、项目设置、`OS.add_logger` | `print`/`printerr`/`push_error`/`push_warning` | `user://logs/godot.log`，默认保留 5 份；崩溃日志写同一文件 | 轮转与保留策略；stdout/stderr 不同的 flush 策略；自定义 logger 的递归保护 | 脚本层日志无编译期剥离 |
| SDL2 | 无 | 每个 category 的 priority；`SDL_LogSetOutputFunction` | printf 风格 + `SDL_vsnprintf` | Windows ODS/stderr、Android logcat、Apple NSLog | 跨平台默认 sink；输出函数可替换 | 256 B 栈后 `SDL_malloc`；桌面不打印 category；自定义 category 在 Android 全变 `CUSTOM` |
| raylib | `SUPPORT_TRACELOG` 可整体关闭 | `SetTraceLogLevel` | `TraceLog(level, fmt, ...)` | 回调拿到 `va_list` | 回调延迟格式化；8 级枚举含 `ALL/NONE` | 全局级别，无 tag |
| glog | `DLOG*` 在 NDEBUG 下编译掉 | `--minloglevel`、`--v`、`--vmodule` 按模块 | `LOG`/`VLOG`/`CHECK`/`PLOG` | 按 severity 分文件；`RAW_LOG` 无分配无锁 | `LOG_EVERY_N/FIRST_N/EVERY_T` 限流；`CHECK` 永远执行；`DFATAL` 发布降级；`PLOG` 带 errno；前缀含时间/线程/file:line | `VLOG` 与 severity 方向相反，易混 |
| spdlog | `SPDLOG_ACTIVE_LEVEL` | `set_level`、per-logger level、argv/env | fmt 库，编译期检查格式串 | 同步/异步 sink、轮转、daily、syslog、Windows event log、ODS | 编译期+运行期双过滤；异步队列；backtrace sink；pattern formatter | 默认异步需要自己管 flush 与背压 |
| Apple os_log | 格式串必须字面量 | subsystem/category | 插值 + 隐私修饰符 | 统一日志系统 | 默认遮蔽动态字符串/对象；`public/private/sensitive`；hash 掩码 | 平台绑定，不能当通用层 |
| Android logcat | 无 | priority + tag | printf 风格 | logcat 环形缓冲 | tag + priority 双维度 | tag 历史长度限制；stdout 不进 logcat |
| Windows ETW | 无 | provider/level/keyword | 结构化事件 | 系统级消费者、实时会话（WPA/xperf） | 结构化、低开销、可离线分析 | 接入成本高，不适合当唯一 sink |

---

## 2. 坑清单（87 项）

### A. 编译期（7）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P01 | 没有编译期级别剥离 | Release 二进制含全部日志代码，热路径照常求值 | UE5 `CompileTimeVerbosity`；glog `DLOG`；spdlog `SPDLOG_ACTIVE_LEVEL` → 已有 `ZCSR_LOG_COMPILE_LEVEL`，保留 |
| P02 | 只有全局剥离，没有 per-module | 一个模块开 VERBOSE，全包都开 | UE5 每个 category 独立第三参 → 用 per-target `target_compile_definitions(zcsr_audio PRIVATE ZCSR_LOG_COMPILE_LEVEL=4)`，`diagnostics.h` 已是 `#ifndef`，可直接覆盖 |
| P03 | 剥离宏写成 `if (0) log()` 而不是 `((void)0)` | 参数仍被求值，副作用仍发生 | glog/spdlog 都保证不求值 → 现有 6 个宏已是 `((void)0)`，保持并加测试 |
| P04 | `##__VA_ARGS__` 非标准 | 换编译器/开 `/Zc:preprocessor` 报错 | C23 `__VA_OPT__` 已标准化 → 双分支：C23 用 `__VA_OPT__(,)`，C11 保留 DEV-001 |
| P05 | Release 保留 INFO，逐帧 INFO 拖慢 | 帧时间抖动 | UE5 Shipping 关日志；Godot release 不逐条 flush → 发布配置把编译级别设到 3/4 |
| P06 | `fmt` 非字面量，无法拼接 file/line | 无法编译期检查，也无法前缀 | 现有 `"[%s:%d] " fmt` 强制字面量 → 保留；公开函数文档写明“fmt 必须是可信字面量” |
| P07 | tag 是自由字符串，拼写无检查 | 25 个模块 tag 漂移，过滤失效 | UE5/SDL 都用 enum category → 增加 `ZCSR_LOG_TAG_*` 常量表；长期换 enum |

### B. 运行期（7）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P08 | 只有全局级别，没有 per-tag | 无法只开音频或只开 UI | UE5 `-LogCmds`；SDL per-category → 增加 `zcsr_log_set_tag_level(tag, level)` |
| P09 | 级别配置读写竞态 | 读到撕裂的 handler/user 组合 | 现有 mutex 快照正确 → 读路径改 `_Atomic int`，写路径仍用锁 |
| P10 | 默认级别导致静默丢失 | 新模块日志“看不见” | SDL 非 APPLICATION 默认 ERROR（反例）→ 保持全局默认 INFO，并在文档写明 |
| P11 | 没有 `is_enabled` 快速判断 | 昂贵参数白算 | glog `LOG_IF` 不求值；spdlog `should_log` → 增加 `zcsr_log_enabled(level, tag)` |
| P12 | 配置无回读、无法恢复默认 | 调完级别回不去 | glog 有 getter；spdlog 有 `level()` → 增加 `zcsr_log_get_level` / `zcsr_log_reset_tag_levels` |
| P13 | handler 替换与在途回调竞态 | 旧 `user` 已释放，回调仍用 | 现有头文件已声明调用方责任 → 增加 sink 版本号或要求 `user` 静态生命周期 |
| P14 | 级别数值被重排 | ABI/存档破坏 | 现有 `_Static_assert` 冻结 0..5 → 保留 |

### C. 格式化（12）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P15 | varargs 类型不匹配 | UB、错值；32 位下后续参数错位 | CERT FIO47；glog/spdlog 有编译期检查 → GCC/Clang attribute 已有；MSVC 靠 clang-cl CI 门禁 |
| P16 | `long` 宽度模型（LLP64/LP64） | `%ld` 打 `int64_t` 在 Windows 截断 | `<inttypes.h>` 的 `PRId64`；SDL 的 `SDL_PRIu64` → 封装 `ZCSR_PRId64/ZCSR_PRIu32` |
| P17 | `size_t` 用 `%u/%d` | 64 位截断 | `%zu`（C99/UCRT 支持）→ 契约规定 size_t 只用 `%zu` |
| P18 | 32 位 ABI 参数错位 | `%d` 消费 int64 的低 4 B，后续 `%s` 读高 4 B → 野指针 | CERT FIO47 → 同 P15；32 位 Android 构建必须跑格式门禁 |
| P19 | 截断返回值/NUL | 缓冲区没有终止符，越界读 | glog/spdlog 固定缓冲 → 修 BUG-2；保留一级 `buf` 的处理 |
| P20 | locale 小数点 | 宿主 `setlocale` 后 `%f` 变成逗号，日志/测试不稳定 | 日志不应依赖 locale → 契约禁止 `%f/%e/%g`，或统一走定点/整数 |
| P21 | 格式串注入 | 读栈、写内存、执行代码 | CERT FIO30 / CWE-134 → 宏强制字面量；公开 `zcsr_log_write` 文档写明不可传运行时串 |
| P22 | `%n` | 任意地址写 | CERT/CWE-134 明确点名 → CI grep 禁止 `%n`，代码评审规则 |
| P23 | 浮点 NaN/Inf/精度 | 输出 `nan`/`inf`，跨平台拼写差异 | spdlog/fmt 有规范 → 禁止浮点，或定义归一化函数 |
| P24 | 指针/句柄打印 | `%p` 格式平台差异、句柄被当整数 | glog 无统一做法 → 统一转 `uintptr_t` + `PRIxPTR` |
| P25 | 宽字符/编码 | UTF-8 字节进 GBK 控制台或 `OutputDebugStringA`，中文乱码 | SDL 在 Windows 转 UTF-16 → sink 层统一 UTF-8 输入，Windows 转 UTF-16 输出 |
| P26 | 换行/控制字符注入 | 一条日志伪造多行，污染审计 | Apple 隐私/结构化日志默认转义 → sink 前清洗 `\r\n` 与 C0 控制字符 |

### D. 内存（6）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P27 | 日志路径堆分配 | 音频回调、低内存、崩溃时不可用 | SDL 超 256 B `SDL_malloc`（反例）；glog `RAW_LOG` 无分配 → 保持栈缓冲，禁止堆 |
| P28 | 双缓冲 4 KB/次 + 嵌套 | 音频线程栈紧张；嵌套日志叠 4 KB | glog 单次格式化 → 合并 `buf` 与 `line`，只保留一个 2 KB 缓冲 |
| P29 | 没有丢弃策略 | 磁盘写满、队列无限增长 | spdlog 异步队列有界 → 定义 drop-oldest/drop-newest |
| P30 | 背压阻塞调用方 | 音频/渲染线程被日志拖住 | 异步 sink 非阻塞计数 → 默认同步，异步必须非阻塞 |
| P31 | 环形缓冲覆盖关键行 | 崩溃时最早/最关键的行已丢 | spdlog backtrace sink；glog 分级文件 → 增加崩溃 ring，保留最后 N 条 |
| P32 | handler 内分配 | 违反零堆约束 | 现有 README 只约束引擎自身 → 文档写明：默认 sink 不分配，handler 自行负责 |

### E. 线程（8）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P33 | 多线程交错 | 一行前缀来自 A、正文来自 B | 单条 `fputs` 在 FILE 锁内原子 → 改为“前缀+正文一次写入”，不拆两次 |
| P34 | 重入/递归 | handler 内再 log，无限嵌套 | Godot 明确递归保护；SDL 可重入 mutex 但不防嵌套 → 现有 `thread_local` guard 保留，并补默认 sink guard |
| P35 | 锁竞争在热路径 | 每条被过滤的日志仍加锁 | spdlog `should_log` 先判 → `_Atomic` 级别 + 无锁 fast path |
| P36 | 音频回调里同步日志 | 实时性破坏、爆音 | 专业引擎禁止音频线程格式化 → 提供 `ZCSR_LOG_RT` 无锁路径或直接禁用 |
| P37 | 首次调用初始化竞态 | 两个线程同时首次 log | SDL 源码自认有竞态；现有 `call_once` → 保留 `call_once` |
| P38 | TLS 析构顺序 | 退出阶段 `thread_local` 已销毁 | 避免在静态析构里 log → 文档 + 关闭顺序 |
| P39 | fork 后锁状态（Linux） | 子进程继承已持有的 mutex，死锁 | `pthread_atfork` 或 fork 后禁用日志 → 文档；需要 fork 的宿主自行处理 |
| P40 | 信号处理上下文 | 崩溃 handler 里用 mutex → 死锁 | glog `RAW_LOG` → 独立 crash sink，不分配、不加锁 |

### F. 落点（8）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P41 | Windows GUI 无 console | `fputs(stderr)` 失败，日志消失 | SDL `AttachConsole` + ODS → ODS 必发；可选文件 sink |
| P42 | Windows 编码 | 中文在 ODS/控制台乱码 | SDL `WIN_UTF8ToString` + `OutputDebugString` → `OutputDebugStringW` |
| P43 | Android 参数错位 | 消息丢失（BUG-1） | `__android_log_print(prio, tag, fmt, ...)` → 修 |
| P44 | Apple 隐私 | 动态字符串默认被遮蔽，排障看不到 | `os_log` 的 `public/private/sensitive` → 若支持 Apple，字段级标注 |
| P45 | 文件轮转/保留 | 磁盘占满 | Godot 默认 5 份 + `max_log_files` → 文件 sink 必须可配上限 |
| P46 | flush 策略 | 每条 flush 慢；不 flush 崩溃丢 | Godot release 不逐条 flush stdout，stderr 每条 → 默认 stderr 每条；文件 sink 可配 |
| P47 | 多 sink 顺序/失败 | 一个 sink 抛错拖垮全部 | UE5 `FOutputDevice` 分离 → sink 独立错误隔离 |
| P48 | 磁盘满/权限 | 写失败静默 | glog 有 `--stop_logging_if_full_disk` → 检测并降级到 stderr |

### G. 结构化（6）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P49 | 只有字符串 | 无法按字段检索/聚合 | UE5 `UE_LOGFMT`；Go `slog`；spdlog pattern → v2 增加字段 API |
| P50 | 无时间/线程/帧号 | 跨线程问题无法关联 | glog 前缀含时间/线程/file:line → 默认前缀补 time/thread；帧号由宿主注入 |
| P51 | 无 event id | 同一事件不同文案无法聚合 | UE5 结构化字段；ETW event id → 增加 `event_id` |
| P52 | 无字段类型 | 解析器只能猜 | `slog`/`UE_LOGFMT` 有类型 → 字段带 type tag |
| P53 | 无 trace/span | 跨线程/跨帧链路断 | Rust `tracing`/ETW activity id → 可选 `trace_id/span_id` |
| P54 | 无脱敏层 | PII/密钥进日志 | Apple 隐私修饰符 → 字段级 `public/private/sensitive` |

### H. 性能（6）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P55 | 每条都格式化 | 热路径成本 | spdlog `should_log`；glog 条件宏 → 先判 `zcsr_log_enabled` |
| P56 | 热路径字符串拼接 | 分配 + 拷贝 | UE5 避免 FString 拼接 → 禁止在每帧路径 `snprintf` |
| P57 | 无频率限制 | 刷屏、磁盘爆 | glog `LOG_EVERY_N/FIRST_N/EVERY_T` → 增加 `ZCSR_LOG_EVERY_N/ONCE/EVERY_MS` |
| P58 | 无采样 | 高频事件不可控 | 专业引擎按帧/按秒采样 → 增加采样宏 |
| P59 | 默认异步 | 崩溃丢日志、背压复杂 | spdlog 异步可选 → 默认同步，异步 opt-in |
| P60 | 无帧预算 | 日志导致卡顿 | UE5 有 stat 预算 → 统计每条耗时/每帧总量 |

### I. 崩溃（5）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P61 | 非 async-signal-safe | 崩溃 handler 死锁 | glog `RAW_LOG` → 独立 crash 路径 |
| P62 | 崩溃丢缓冲 | 最后几条最关键 | glog 分级文件 + flush → crash ring + 预打开 fd |
| P63 | 无 backtrace | 无法定位 | Godot crash backtrace；minidump → 宿主集成，引擎只留钩子 |
| P64 | 无 minidump 关联 | dump 与日志对不上 | UE5 crash reporter → 日志带 session id |
| P65 | handler 崩溃递归 | 栈溢出 | Godot 递归保护 → guard + 失败即静默 |

### J. 安全/合规（5）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P66 | PII/密钥 | 合规事故 | Apple 默认遮蔽 → 字段策略 + 评审清单 |
| P67 | 日志注入 | 伪造审计行 | 清洗换行/控制字符 → sink 前统一处理 |
| P68 | 路径/用户名 | 隐私泄露 | 相对化路径 → 文档 + 可选脱敏 |
| P69 | 保留期限 | GDPR/合规 | Godot 轮转上限 → 文件 sink 必须可配 |
| P70 | 主机认证 | 发布拒审 | TRC/TCR/XR 非公开文档，需逐平台核对 → 预留“发布模式”：只保留 WARN+，关闭 ODS |

### K. 工具链（5）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| P71 | MSVC 无用户函数格式检查 | 类型错误静默通过 | clang-cl 作为 CI 门禁；可选 `/analyze` + SAL |
| P72 | GCC/Clang attribute 未覆盖全部入口 | 漏检 | 统一宏 `ZCSR_PRINTF_FORMAT`；新增入口必须带 |
| P73 | 无 clang-tidy | 格式串/注入漏检 | `bugprone-format-string`、`cert-printf` 规则 |
| P74 | 无 ASan/UBSan 长消息测试 | BUG-2 这类越界读漏检 | 长消息单测 + ASan |
| P75 | 无格式串 fuzz | 边界格式崩溃 | 对 `zcsr_log_write` 做 libFuzzer 目标 |

### L. 测试（6）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| L01 | 无日志捕获 | 只能靠肉眼 | 测试装 handler，断言 level/tag/msg |
| L02 | 无 golden 字节 | 输出格式漂移 | 固定前缀断言 |
| L03 | 无并发测试 | 丢行/死锁 | 8 线程 × N 条 |
| L04 | 无长消息测试 | 截断/越界 | 2047/2048/4096/8192 B |
| L05 | 无崩溃路径测试 | crash sink 不可测 | 注入式测试钩子 |
| L06 | 无无堆断言 | 违反零堆 | 替换 malloc 计数器 |

### M. 契约（6）

| ID | 坑 | 后果/触发 | 业界做法 → zcsr 对策 |
|---|---|---|---|
| M01 | handler 生命周期 | 悬垂 `user` | 现有文档已写明 → 保留并加示例 |
| M02 | handler 重入 | 递归 | guard → 补默认 sink |
| M03 | tag 所有权 | 传栈上字符串 | 要求静态/字面量 → 文档 + 测试 |
| M04 | handler 并发 | 数据竞争 | 现有文档已声明 → 保留 |
| M05 | 全局状态/多实例 | 两个宿主抢配置 | 单例文档；需要多实例则加 context 参数 |
| M06 | 卸载顺序 | 退出时崩溃 | 文档 + `zcsr_log_shutdown` |

---

## 3. 现状 gap 矩阵

> 下表是重构前的基线；P0/P1 之后已改善的项见 §6，未变的项仍按此表执行。

| 类别 | 现状 | 判定 |
|---|---|---|
| A 编译期 | 有全局剥离；无 per-module；`##__VA_ARGS__` 非标准 | 部分 |
| B 运行期 | 全局级别；无 per-tag；无 `enabled` 快判 | 部分 |
| C 格式化 | 一级截断正确；二级截断坏；MSVC 无检查；locale 未隔离 | 部分/坏 |
| D 内存 | 栈缓冲、无堆；双缓冲 4 KB；无丢弃策略 | 部分 |
| E 线程 | `call_once` + `thread_local` guard + 快照正确；过滤路径仍加锁 | 好/部分 |
| F 落点 | Android 坏；Windows 编码缺；无文件 sink | 部分/坏 |
| G 结构化 | 无字段、无 event id、无 trace | 缺 |
| H 性能 | 无 per-tag、无限流、默认同步 | 缺 |
| I 崩溃 | 无 crash 路径、无 ring、无 backtrace | 缺 |
| J 安全 | 宏挡注入；无脱敏、无换行清洗 | 部分 |
| K 工具链 | 无 MSVC 检查、无 clang-tidy、无 fuzz | 缺 |
| L 测试 | `tests/test_log.c` 仅 3 处 | 缺 |
| M 契约 | 级别 ABI、handler 生命周期/并发已写明 | 好 |

---

## 4. 重构方案

### 4.1 API 草案（v1 兼容，v2 增量）

```c
/* 级别数值冻结，禁止重排 */
typedef enum {
    ZCSR_LOG_TRACE = 0, ZCSR_LOG_DEBUG = 1, ZCSR_LOG_INFO = 2,
    ZCSR_LOG_WARN = 3,  ZCSR_LOG_ERROR = 4, ZCSR_LOG_FATAL = 5,
} zcsr_log_level;

/* 热路径先判，避免参数求值 */
bool zcsr_log_enabled(zcsr_log_level level, const char* tag);

/* 主入口：fmt 必须是字面量 */
void zcsr_log_write(zcsr_log_level level, const char* tag, const char* fmt, ...)
    ZCSR_PRINTF_FORMAT(3, 4);

/* 限流：每个调用点独立计数，无堆 */
#define ZCSR_LOG_ONCE(level, tag, fmt, ...)
#define ZCSR_LOG_FIRST_N(level, tag, n, fmt, ...)
#define ZCSR_LOG_EVERY_N(level, tag, n, fmt, ...)
#define ZCSR_LOG_EVERY_MS(level, tag, ms, fmt, ...)

/* per-tag 级别：32 项静态表 + FNV-1a，无堆 */
bool zcsr_log_set_tag_level(const char* tag, zcsr_log_level level);
bool zcsr_log_get_tag_level(const char* tag, zcsr_log_level* out);
void zcsr_log_reset_tag_levels(void);
void zcsr_log_set_level(zcsr_log_level level);
zcsr_log_level zcsr_log_get_level(void);

/* sink：默认 sink + 可选自定义 sink */
typedef struct zcsr_log_meta {
    uint64_t timestamp_ns;
    uint64_t thread_id;
    uint32_t frame_index;   /* 宿主注入，可为 0 */
    const char* file;
    int line;
} zcsr_log_meta;

typedef struct zcsr_log_sink {
    void (*write)(void* user, zcsr_log_level level, const char* tag,
                  const char* msg, const zcsr_log_meta* meta);
    void* user;
    uint32_t flags;   /* UTF8 / CRASH_SAFE / ASYNC */
} zcsr_log_sink;

bool zcsr_log_set_sink(const zcsr_log_sink* sink);

/* v2 结构化：字段而非格式串 */
typedef enum {
    ZCSR_LOG_FIELD_I64, ZCSR_LOG_FIELD_U64, ZCSR_LOG_FIELD_F64,
    ZCSR_LOG_FIELD_STR, ZCSR_LOG_FIELD_BOOL, ZCSR_LOG_FIELD_PTR
} zcsr_log_field_type;

typedef struct {
    const char* key;
    zcsr_log_field_type type;
    union { int64_t i64; uint64_t u64; double f64; const char* str; bool b; void* ptr; } v;
} zcsr_log_field;

void zcsr_log_event(zcsr_log_level level, const char* tag, uint32_t event_id,
                    const zcsr_log_field* fields, size_t count);

/* 崩溃路径：不分配、不加锁、不开新 fd、不用 vsnprintf；
 * 只接受已格式化字符串，或极小的无锁整数格式化 */
void zcsr_log_crash_write(const char* tag, const char* msg);
```

### 4.2 阶段划分

| 阶段 | 内容 | 退出条件 |
|---|---|---|
| P0 | 修 BUG-1、BUG-2；补长消息/Android 单测 | ASan 下 8192 B 消息无越界；mock Android 断言 tag/正文 |
| P1 | per-tag 级别 + `_Atomic` 快路径 + 限流宏 | 8 线程并发无丢行；过滤路径无锁；限流计数正确 |
| P2 | sink 重构：Windows UTF-16、Android 修正、可选文件 sink + 轮转 | Windows 中文 ODS 正确；文件 5 份轮转；磁盘满降级 |
| P3 | 结构化字段 + crash ring + clang-cl 门禁 + clang-tidy + fuzz | 结构化事件可 JSON 化；crash 路径无分配；`%d`+int64 在 clang 下编译失败 |

### 4.3 明确不做

- 不引入 SDL2 只为日志。
- 不引入 C++ fmt/spdlog 作为核心依赖（可以作为测试对照）。
- 默认不同步转异步；异步是 opt-in sink。
- 不在引擎层做网络上报；由宿主 handler 决定。
- 不把 tag 做成固定 enum 后再暴露给外部宿主；先保留字符串，内部提供常量表。

---

## 5. 验收测试（12 条）

| 编号 | 测试 | 通过条件 |
|---|---|---|
| T01 | 2047/2048/4096/8192 B 消息，默认 sink，ASan | 无越界读；输出恰好一个换行；NUL 终止 |
| T02 | mock `__android_log_print`，中文 tag + 消息 | tag 等于模块名，正文等于消息 |
| T03 | `zcsr_log_write(..., "%d", int64_t)`，clang-cl `/W4 /WX` | 编译失败（`-Wformat`） |
| T04 | 8 线程 × 10k 条，混合级别 | 无丢行、无行内交错、无死锁 |
| T05 | handler 内递归 log | 不无限嵌套，嵌套消息走默认 sink |
| T06 | per-tag 级别：audio=WARN，ui=INFO | audio 的 INFO 不输出，ui 的 INFO 输出 |
| T07 | `ZCSR_LOG_EVERY_N(INFO, "x", 10)` 调 100 次 | 输出 10 次 |
| T08 | Windows 控制台/ODS 输出中文 | 无乱码 |
| T09 | crash sink：模拟 SIGSEGV 前写 3 条 | 3 条落盘；无 malloc、无 mutex |
| T10 | 默认路径 malloc 计数 | 0 次引擎侧分配 |
| T11 | 长消息 + 自定义 handler | handler 收到的 msg 是 2047 B 内且 NUL 终止 |
| T12 | `zcsr_log_set_tag_level` 并发写 + 并发 log | 无数据竞争（TSan） |

---

## 6. 实施记录（2026-11-16，P0–P3 一次改动）

### 6.1 已落地

| 文件 | 改动 |
|---|---|
| `modules/core/log.c` | 修 BUG-1（Android 参数顺序）；修 BUG-2（二级截断改为 `line[2046]='\n'; line[2047]='\0'`）；Windows 仅在 `IsDebuggerPresent()` 时用 `MultiByteToWideChar(CP_UTF8)` + `OutputDebugStringW`；新增 32 项 per-tag 级别表（FNV 不需要，线性查表，无堆）；全局级别改 `_Atomic int`；新增 `zcsr_log_enabled` 无锁快判；P0/P1 当时 Android 下用 `#if !defined(__ANDROID__)` 包住仅桌面使用的级别名映射，P2 因文件 sink/crash ring/JSON 需要该映射已去掉 guard |
| `include/zcsr/log.h` | 新增 `zcsr_log_enabled`、`zcsr_log_set_tag_level`、`zcsr_log_get_tag_level`、`zcsr_log_reset_tag_levels`；补 `stdbool.h` 与契约说明 |
| `include/zcsr/diagnostics.h` | 新增 `ZCSR_LOG_ONCE`、`ZCSR_LOG_FIRST_N`、`ZCSR_LOG_EVERY_N`；每个展开点一个 static 计数器；低于 `ZCSR_LOG_COMPILE_LEVEL` 时整体裁掉；C 用 `_Atomic`/`stdatomic.h`，C++17 用 `std::atomic`/`<atomic>`（见 6.8 ⑭） |
| `tests/test_log.c` | 新增 per-tag 覆盖/回读/重置、非法 tag、`enabled` 快判、三个限流宏的断言；原 44 项检查编号顺延 |
| `tests/test_log_default.c` | 新增默认 sink 长消息回归：重定向 stderr，发 4095 B 消息，断言输出 ≤2048 B、单个换行、无嵌入 NUL、前缀正确 |
| `tests/engine/CMakeLists.txt` | 注册 `log_default` CTest |

### 6.2 验证结果（P0/P1 历史快照，当前状态见 6.8）

| 环境 | 命令 | 结果 |
|---|---|---|
| Windows 11 + VS 2026 Professional 18.11.12324.101 / MSVC 14.51.36231 | `cmake --build build --config Debug`，编译选项 `/W4 /WX /utf-8 /experimental:c11atomics` | 全量构建通过 |
| 同上 | `ctest --test-dir build -C Debug -R log --output-on-failure` | 2/2 通过（`log`、`log_default`） |
| Ubuntu 22.04 + GCC 11.4.0 | `gcc -std=gnu11 -Wall -Wextra -Wpedantic -pthread` 编译 `log.c` + 两个测试 | 零警告；`test_log` 全过；`test_log_default` 2047 B 通过 |
| Linux + 假 `android/log.h` + stub logcat | `gcc -D__ANDROID__` 编译 Android 分支，断言 tag/msg/prio | 通过：`tag=resource/ui`、`msg=loaded 42`、`prio=4` |
| Windows 全量 CTest | `ctest --test-dir build -C Debug` | 20/21；`input` 失败来自工作区已有的 `input.c`/`platform_win32.c` 未提交改动（测试假设 fake surface 不被解引用），与日志改动无关 |

### 6.3 P2：sink 重构与崩溃路径

| 文件 | 改动 |
|---|---|
| `include/zcsr/log.h` | 新增 `zcsr_log_get_level`、`zcsr_log_now_ms`（Windows `GetTickCount64`（Win7+，无 49.7 天回绕）；POSIX `CLOCK_MONOTONIC`）、`zcsr_log_set_file`、`zcsr_log_clear_file`、`zcsr_log_set_crash_file`、`zcsr_log_clear_crash_file`、`zcsr_log_crash_write`、`zcsr_log_crash_flush`、`zcsr_log_field_type` / `zcsr_log_field` / `zcsr_log_event`；文件 sink 明确是附加 sink，crash sink 由宿主信号处理器调用，引擎不安装信号处理器 |
| `modules/core/log.c` | 文件 sink：先开新 handle 再锁内换旧；`max_files` 0→5、1..16，`max_bytes` 0→1 MiB、最小 4096；写满轮转 `<path>.1`..`.N`；Windows 用 `CreateFileW`/`WriteFile`/`MoveFileExW`，POSIX 用 `open`/`write`/`fstat`/`fsync`/`rename`；失败关闭文件 sink 并打印一次 WARN，默认 sink 继续 |
| 同上 | crash sink：预开 append handle；`zcsr_log_crash_write` 手工拼 `[CRASH][tag] msg\n`，无 `snprintf`/mutex/stdio；无 handle 时写 stderr；64 槽 × 256 B ring，每槽 `_Atomic unsigned seq` 奇偶 seqlock + 全局 `msg_seq`；`zcsr_log_crash_flush` 按 `msg_seq` 升序 raw write + `fsync`/`FlushFileBuffers` |
| 同上 | 结构化事件：`{"ts","tid","level","tag","event","fields"}`；字符串 JSON 转义（合法 UTF-8 原样保留，非法字节转成 `\u00XX`，保证 JSON 可解析）；I64/U64 用 `PRId64`/`PRIu64`；F64 用 locale 无关定点，NaN/Inf/越界写 `null`；PTR 写 JSON 字符串（小写 `0x...`，例如 `"ptr":"0x1234"`）；空 fields 合法，`count>0 && fields==NULL` 拒绝 |
| 同上 | Windows 默认 sink：控制台用 `MultiByteToWideChar(CP_UTF8)` + `WriteConsoleW`，失败回退 `fputs`；重定向走 `fputs`；调试器附加时额外 `OutputDebugStringW`。Apple 默认 sink 用 `os_log_with_type(OS_LOG_DEFAULT, ...)`；Android 保持 `__android_log_print(prio, tag, "%s", msg)` |
| `include/zcsr/diagnostics.h` | 新增 `ZCSR_LOG_EVERY_MS`：每展开点一个 `static _Atomic uint64_t last_ms`；首调用立即输出，`ms<=0` 不输出；CAS 抢占避免并发重复 |
| `tests/test_log.c` | 修并发阶段 collector 的数据竞争；新增 `counting_handler`、8 线程 × 10k、配置线程、per-tag 配置线程；新增 `get_level`、`EVERY_MS` 断言 |
| `tests/test_log_default.c` | 重写为 2047/2048/4096/8192 四种长度，二进制模式重定向 stderr，断言 ≤2048 B、末尾 `\n`、无嵌入 NUL、前缀 `[INFO][big] ` |
| `tests/test_log_file.c` | 新增：非法目录/不存在目录失败；`max_files=5`、`max_bytes=4096` 的 500 条消息轮转；current + `.1`..`.5` 存在、`.6` 不存在、每份 ≤4096 B；额外写一条 3000 B 长消息，断言文件末尾 `\n` 且无嵌入 NUL；Linux `/dev/full` ENOSPC 降级后可恢复 |
| `tests/test_log_crash.c` | 新增：无效路径失败；100 条 ring 消息 + direct crash line + NULL 安全；断言保留 036..099、036 在 099 前、`--- crash ring ---` 存在、clear 幂等 |
| `tests/test_log_event.c` | 新增：8 种字段（I64/U64/F64/NaN/转义 STR/BOOL/PTR/非法 type）、空 fields、NULL fields 拒绝、tag 过滤、3000 B 字符串截断 |

### 6.4 P3：工具链门禁与 fuzz

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | 新增 `ZCSR_ENABLE_FORMAT_GATE`（默认 ON）、`ZCSR_BUILD_FUZZERS`（默认 OFF）、`ZCSR_ENABLE_SANITIZERS`、`ZCSR_ENABLE_TSAN`；sanitizer 只允许 GCC/Clang，MSVC 显式报错 |
| `tests/CMakeLists.txt` | `format_gate`：MSVC 找 `clang-cl`，找不到则跳过；非 MSVC 用 `CMAKE_C_COMPILER` 跑 `-Wformat -Werror`。`clang_tidy_format`：仅当 `clang-tidy --list-checks --checks=bugprone-format-string` 真正包含该检查时注册。`log_no_malloc`：Windows 用 `dumpbin /symbols`，Unix 用 `nm -u`。`ZCSR_BUILD_FUZZERS=ON` 且 Clang 时构建 `fuzz_log` |
| `tests/format_gate/bad_format.c` | 负例：`zcsr_log_write(..., "%d", int64_t)` |
| `tests/format_gate/good_format.c` | 正例：`zcsr_log_write(..., "%" PRId64, int64_t)` |
| `tests/format_gate/run_format_gate.cmake` | 负例必须编译失败、正例必须编译通过；支持 `clang-cl` 与 `clang/GCC` 两种 flavor |
| `tests/format_gate/run_clang_tidy.cmake` | 对正反例跑 `bugprone-format-string` + `cert-printf`，负例必须报错、正例必须干净 |
| `tests/check_log_no_malloc.cmake` | 扫描 `zcsr_core` 符号表，发现 `malloc/calloc/realloc/free` 即失败 |
| `tests/fuzz/fuzz_log.c` | libFuzzer harness：随机字节经 `%s` 进 `zcsr_log_write`，再进 `zcsr_log_event` |
| `tests/test_log_fuzz.c` | 确定性 fuzz smoke：20k 随机字节串 + 4 个长度边界，全平台 CTest 可跑 |

### 6.5 P2/P3 验证结果（2026-11-16 快照，当前状态见 6.8）

| 环境 | 命令/范围 | 结果 |
|---|---|---|
| Windows 11 + VS 2026 18.11.12324.101 / MSVC 14.51.36231 | `cmake --build build --config Debug` | 全量构建通过，无新增 error |
| 同上 | `ctest --test-dir build -C Debug -R log` | **7/7 通过**：`log_no_malloc`、`log`、`log_default`、`log_file`、`log_crash`、`log_event`、`log_fuzz` |
| 同上 | `ctest --test-dir build -C Debug` | **26/27**；唯一失败是 `input`（用户工作区已有的 `input.c`/`platform_win32.c` 未提交改动），与日志无关 |
| Windows `dumpbin /symbols zcsr_core.lib` | `log_no_malloc` | 通过：`zcsr_core` 无 `malloc/calloc/realloc/free` 引用 |
| Ubuntu 22.04 + GCC 11.4.0 | `gcc -std=gnu11 -Wall -Wextra -Wpedantic -Werror -Iinclude -pthread` 编译 `log.c` + 6 个测试 | 零警告；6/6 运行通过 |
| 同上 | `gcc -std=c11 -Wall -Wextra -Werror -Wformat` 编译 format-gate 正反例 | 负例失败（`%d` + `int64_t` 报 `-Werror=format=`），正例通过 |
| 同上 | ASan+UBSan 编译并运行 6 个测试（`detect_leaks=1`、`halt_on_error=1`） | 6/6 通过，无 ASan/UBSan 报告；更新后的 `test_log_file`（含 3000 B 长行）单独重跑 ASan 仍 PASS |
| Linux + 假 `android/log.h` | `-D__ANDROID__` 编译 Android 分支并断言 tag/msg/prio | 通过：`prio=4`、`tag=resource/ui`、`msg=loaded 42` |
| Linux + 假 `os/log.h` | `-D__APPLE__` 编译 Apple 分支并断言 os_log 类型/正文 | 通过：`type=OS_LOG_TYPE_INFO`、`msg=[INFO][ui] hello 7` |

### 6.6 未验证与环境限制

| 项 | 现状 | 影响 |
|---|---|---|
| Windows `clang-cl` | 本机 VS LLVM 目录只有 `clang-format.exe`/`clang-tidy.exe`，没有 `clang-cl.exe`/`clang.exe` | Windows `format_gate` CTest 被显式跳过；Linux GCC `-Wformat -Werror` 门禁已实跑通过，脚本换到有 clang-cl 的机器可直接启用 |
| `clang-tidy` | VS 22.1.3 构建的 `--list-checks --checks='*'` 不含 `bugprone-format-string`/`cert-printf`，`clang-diagnostic-*` 报 `no checks enabled` | `clang_tidy_format` CTest 在当前机器跳过；检测逻辑只在真正包含该检查时注册，不会假装通过 |
| MSVC 格式检查 | MSVC 有 SAL `_Printf_format_string_`（`/analyze`/IDE 用），但当前构建没有开 `/analyze`，也没有把它当作硬门禁 | `ZCSR_PRINTF_FORMAT` 在 MSVC 下为空；Windows 调用点仍可能把 `int64_t` 配给 `%d`；结构化事件 API 是 MSVC 下的类型安全替代 |
| TSan | 该 VM 的 GCC 11 libtsan 在子线程执行任意 syscall（最小 `getpid()`/`thrd_sleep()` 复现）即 SEGV；`mtx_t` 版本的 `test_log` 同样在 TSan 运行时崩溃，未产生 race 报告 | T12 的“TSan 实跑无 race”**未取得证据**；只能用 8×10k + 配置线程 + ASan/UBSan 压测覆盖。换 Clang/较新 libtsan 或裸金属环境需重跑 |
| libFuzzer | 本机无 Clang，`ZCSR_BUILD_FUZZERS=ON` 会主动报错；当前只跑了确定性 `test_log_fuzz`（20k 随机串） | fuzz harness 已交付，未做真实 libFuzzer 分钟级运行 |
| Windows 控制台/ODS 中文 | UTF-16 代码路径已编译并被 `log_test` 执行，但 CTest 未做像素/文本断言 | 需要人工在 DebugView/控制台确认中文无乱码 |
| 真实 macOS 15 / Android 13 | 只有 fake header 的分支级编译与 stub 断言，没有真机/NDK 构建 | Apple `os_log`、Android logcat 的运行时行为仍需目标平台验证 |
| Linux 全量 CMake | 未在 Linux 配完整仓库（只直接编译 `log.c` + 测试） | 日志模块本身通过；跨模块链接由 Windows 全量构建覆盖 |

### 6.7 下一步

- 整体审核这组未提交改动；审核通过后再决定分支/提交策略，不在半成品状态建分支。
- 在具备 clang-cl 的 Windows 环境或 CI 上重跑 `format_gate`；在完整 clang-tidy 发行版上重跑 `clang_tidy_format`。
- 在 Clang/新 libtsan 或裸金属 Linux 上重跑 `test_log` 的 TSan 版本，补 T12 证据。
- 在真实 macOS 15 / Android 13 上做一次 `os_log` / logcat 冒烟，并人工确认 Windows 控制台/ODS 中文。


### 6.8 2026-11-17 复查补丁（①–⑭）

| 项 | 处理 | 位置 | 验证 |
|---|---|---|---|
| ① `zcsr_log_event` 超长非法 JSON | 字段写入前记录回滚点；字段渲染溢出或尾部预留不足时丢弃该字段，按“首个字段/已有字段”分别补 `"truncated":true`，始终闭合 `}}`。tag 先渲染到 256 B scratch，超长退化为 `"..."`。 | `modules/core/log.c` 事件段；`include/zcsr/log.h` 契约；`tests/test_log_event.c` | `log_event` 通过；3000 B + 1 个正常字段的探针输出 `{"ts":...,"fields":{"ok":1,"truncated":true}}`，Python `json.loads` 成功 |
| ② X11/Wayland `take_close` 解引用任意指针 | 与 Win32 一致：先遍历 `zcsr_surfaces[ZCSR_SURFACE_POOL]` 校验池身份和 `in_use`，再读/清 `close_requested`。 | `modules/platform/platform_x11.c:97`、`platform_wayland.c:224` | X11 在 Ubuntu 22.04/GCC 11.4 以 `-std=c11 -Wall -Wextra -Werror -DZCSR_LINUX_BACKEND_X11` 通过 `-fsyntax-only`；Wayland 因该机无 `wayland-client.h` 未实编译，代码与 X11/Win32 同一模式，未写成“已验证运行” |
| ③ `diagnostics.h` 的 `log.h` 在 `extern "C"` 外 | 把 `#include "zcsr/log.h"` 移入 `extern "C"` 块内；C++ 直接 include `diagnostics.h` 时声明获得 C 链接。另把 C11 `<stdatomic.h>` 改为仅 C 包含：GCC 的 C++17 会拒绝其中的 `_Atomic` typedef，MSVC 只是警告；C++17 可用普通日志/事件 API，`ONCE/FIRST_N/EVERY_N/EVERY_MS` 的 C++ 实现见⑭。 | `include/zcsr/diagnostics.h:14-31` | Windows `cxx_diag.cpp`（C++17，include diagnostics 并调用宏）编译、链接 `zcsr_core.lib`、运行成功；Linux `g++ -std=c++17` 同样编译/链接/运行成功（仅剩零实参可变参数宏的 pedantic 警告） |
| ④ `msg_seq` 非原子 | 槽位 `msg_seq` 改为 `_Atomic unsigned`；读、写、clear 全部走 atomic。 | `modules/core/log.c` 槽结构/reader/clear/push | `log_crash`、`log` 通过；28/28 全量绿 |
| ⑤ >64 并发写者撕裂 crash ring | 写者先读偶 seq；奇数（正被写）直接丢弃；用 CAS 从偶 seq 抢占到奇 seq，失败者也丢弃。一个槽同一时刻只有一个写者；写者崩溃留下的 odd 槽会被 flush 跳过，属 best-effort。 | `modules/core/log.c:645-662` | `log_crash` 通过；未做 >64 并发写者的 TSan/压力证明 |
| ⑥ POSIX `thrd_t` 强转 | Linux/Android/Apple 改用 `pthread_self()` + `uintptr_t`；其他 POSIX 保留 `thrd_current()` 兜底并注释契约。 | `modules/core/log.c:920-926` | Windows 构建通过；Linux 仅由代码路径/头文件约束，未在真实 macOS/Android 编译 |
| ⑦ 字段枚举未冻结 | `ZCSR_LOG_FIELD_I64..PTR` 增加 6 条 `_Static_assert`（C++ 下跳过，保持现有头文件策略）。 | `include/zcsr/log.h:173-180` | `log_event` 通过；C11 编译通过 |
| ⑧（复查额外发现）`gl_render_light2d_ext.h` 尾部声明在 include guard/extern "C" 之外 | `zcsr_gl_l2d_set_msaa` 的声明移到 `extern "C"` 块内、include guard 结束前；原先与 `gl_render.h` 的同名声明链接规格冲突（MSVC C2732）。 | `include/zcsr/gl_render_light2d_ext.h:216-236` | `h_ALL2.c` 以 `/TP /std:c++17` 作为单翻译单元包含全部公开头，编译通过；`cxx_diag` 仍链接/运行成功 |
| ⑨ PTR 字段输出未加引号的 `0x...`，整个事件不是合法 JSON | PTR 改为 JSON 字符串 `"0x..."`；`zcsr_json_ptr` 加引号；`test_log_event` 断言 `"ptr":"0x1234"`。 | `modules/core/log.c` 事件段；`include/zcsr/log.h`；`tests/test_log_event.c` | 全字段事件经 Python `json.loads` 解析成功；PTR 从非法 token 变为字符串 |
| ⑩ `\u00XX` 转义用 `'0'+nibble` 生成十六进制字符，C0 控制字符/非法字节会产生 `\u00<3` 等非法转义 | 使用 `"0123456789abcdef"` 查表生成两个十六进制位；新增 `\u0001` 断言。 | `modules/core/log.c:zcsr_json_escape_byte`、`tests/test_log_event.c` | `test_log_event` 通过；全字段事件可解析 |
| ⑪ 非法 UTF-8 字节原样进入 JSON 字符串，JSON 文件按 UTF-8 读取失败 | 增加 UTF-8 校验；合法 2/3/4 字节原样保留，非法字节转 `\u00XX`；超长字符串仍走字段回滚。 | `modules/core/log.c:zcsr_json_utf8_len/zcsr_json_string`、`tests/test_log_event.c` | 非法字节探针输出 `"\u00c3("`，Python `json.loads(bytes)` 成功 |
| ⑫ per-tag 级别低于全局时仍被全局提前过滤，tag override 不能放宽 | 有具体 tag 级别时直接作为该 tag 的 effective minimum；无 override 才回退全局。 | `modules/core/log.c:zcsr_log_enabled`、`tests/test_log.c` | `verbose=DEBUG`、全局 INFO 时 DEBUG 通过、TRACE 过滤；`log` 测试通过 |
| ⑬ `log.h` 自身没有 `extern "C"`，C++ 直接 include 时按 C++ 名字修饰声明，链接 C 库失败 | 在 `log.h` 的声明区加 `extern "C"` 包裹；`diagnostics.h` 内嵌包含仍合法。 | `include/zcsr/log.h` | 直接 `#include "zcsr/log.h"` 的 C++17 探针编译、链接、运行成功；`cxx_diag` 仍通过 |
| ⑭ `diagnostics.h` 的 `ZCSR_LOG_ONCE/FIRST_N/EVERY_N/EVERY_MS` 在 C++17 下直接展开 `_Atomic` 和 `atomic_*`，C++ 不认这些 C11 语法，调用即编译失败 | 增加 C++/C 双实现：C++ 分支包含 `<atomic>`，用 `std::atomic<T>` + `std::memory_order_relaxed`；C 分支仍用 `<stdatomic.h>` + `_Atomic`/`atomic_*`。新增 `ZCSR_LOG_ATOMIC_TYPE/EXCHANGE/FETCH_ADD/LOAD/CAS` 适配宏，四个限流宏不再直接出现 `_Atomic`。 | `include/zcsr/diagnostics.h` 顶部包含区、`ZCSR_LOG_ATOMIC_*` 适配宏、四个限流宏 | Windows MSVC `/TP /std:c++17 /W4 /WX` 编译 `cxx_rate.cpp` 通过，链接 `zcsr_core.lib` 后运行输出四行；Linux GCC 11.4 C11 侧 6/6，C++17 侧编译/链接/运行成功 |

**本补丁验证（2026-11-17）**：
- `cmake --build build-acc --config Debug`：通过。
- `ctest --test-dir build-acc -C Debug --output-on-failure`：**28/28 通过**（含 `log` 7 项、`input`、`platform`）。
- Ubuntu 22.04 + GCC 11.4.0：`log.c` + 6 个日志测试以 `-std=gnu11 -Wall -Wextra -Wpedantic -Werror` 编译，6/6 运行通过；ASan+UBSan（`detect_leaks=1`/`halt_on_error=1`）6/6 通过；`nm -u` 无 `malloc/calloc/realloc/free`。
- ① 探针：Python `json.loads` 解析成功，`fields={"ok":1,"truncated":true}`，未出现半截字符串。
- ③ 探针：Windows MSVC C++17 与 Linux g++ C++17 均编译/链接/运行成功；Linux 输出 `[INFO][t] [main:2] hi`，Windows 输出 `[INFO][t] [main:2] hi`。
- ⑧ 探针：`build-acc\_lint\hdr\h_ALL2.c` 以 `cl /TP /std:c++17` 单翻译单元包含全部公开头，通过。
- ⑨–⑪ 探针：包含 I64/U64/F64/NaN/STR/C0/非法 UTF-8/BOOL/PTR 的全字段事件，Python `json.loads(bytes)` 解析成功；PTR 为 `"0x1234"`。
- ⑫ 探针：全局 INFO + `verbose=DEBUG` 时 DEBUG 通过、TRACE 被过滤；`log` 测试通过。
- ⑬ 探针：直接 include `zcsr/log.h` 的 C++17 程序编译、链接、运行输出 `[INFO][cxx] direct log.h works`。
- ⑭ 探针：`build-acc\_lint\cxx_rate.cpp` 在 MSVC `/TP /std:c++17 /W4 /WX` 下编译通过，链接 `zcsr_core.lib` 后运行输出 `[INFO][t] [main:5] once` 等四行；Linux GCC 11.4 下 C++17 探针与 C 版 `log.c` 编译、链接、运行成功。
- MSVC C11 直接编译：`cl /std:c11 /W4 /WX /I include /c modules\core\log.c` 不加 `/experimental:c11atomics` 会 `vcruntime_c11_stdatomic.h` 报 C1189；加该 flag 后 0 warning/0 error。项目 `CMakeLists.txt:16` 已全局加该 flag，所以 CMake 构建通过；绕过 CMake 的外部消费者仍需自行提供该 flag。
- ② X11 `gcc -fsyntax-only` 通过；Wayland 未实编译（环境缺头文件）。

**仍未闭环（不得写成通过）**：
- Wayland 实编译/运行；真实 macOS 15 / Android 13 的 `os_log` / logcat。
- Windows clang-cl、`clang-tidy` 的 `bugprone-format-string`/`cert-printf`、TSan、真实 libFuzzer。
- Windows 控制台/ODS 中文可视化。
- `main` 分支是否受保护、是否直接 push，未从本地验证；当前仍 `main` / HEAD `19c193f` / 不提交。

---

## 7. 参考来源

- UE5 Logging: https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine
- Godot Logging: https://docs.godotengine.org/en/stable/tutorials/scripting/logging.html
- Unity Debug.Log: https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Debug.Log.html
- SDL2 `SDL_log.h`: https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.8/include/SDL_log.h
- SDL2 `SDL_log.c`: https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.8/src/SDL_log.c
- SDL2 `SDL_stdinc.h`: https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.8/include/SDL_stdinc.h
- glog: http://google.github.io/glog/stable/logging/
- spdlog: https://spdlog.readthedocs.io/en/latest/
- raylib cheatsheet: https://www.raylib.com/cheatsheet/cheatsheet_zh.html
- libtcod console printf (UTF-8 2D console): https://libtcod.readthedocs.io/en/stable/core/console.html
- Microsoft GDK `HCTraceImplMessage` (`_Printf_format_string_` 示例): https://learn.microsoft.com/gl-es/gaming/gdk/docs/reference/live/httpclient/trace/functions/hctraceimplmessage?view=gdk-2610
- Apple os_log privacy: https://developer.apple.com/documentation/os/generating-log-messages-from-your-code
- CERT FIO30-C: https://wiki.sei.cmu.edu/confluence/display/c/FIO30-C.+Exclude+user+input+from+format+strings
- CERT FIO47-C: https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/input-output-fio/fio47-c/
- CWE-134: https://cwe.mitre.org/data/definitions/134
- C23 `__VA_OPT__` proposal: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3792.pdf

未确认：GoBox/GameBox 未检索到可核实的 C 2D 引擎/库（2026-11-16 检索只命中 Akihabara 的 JS `gbox` 模块与 libtcod 的 UTF-8 console printf），本清单未纳入对照；若指某个具体仓库，补链接后追加。
