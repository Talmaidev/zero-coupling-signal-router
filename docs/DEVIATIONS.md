# ZCSR 语法与 ABI 偏差记录

本文件只记录**明确不采纳**或**接受为已知偏差**的条目；已执行的整改不在这里重复。

## DEV-001 — `##__VA_ARGS__` 零实参日志宏

- 位置：`include/zcsr/diagnostics.h`
- 状态：**不采纳“ISO C11 严格零实参变参宏”要求；接受 GNU/MSVC/Clang 扩展**
- 原因：ISO C11 没有 `__VA_OPT__`。同一宏既要支持 `ZCSR_LOG_INFO("tag", "message")`，又要支持 `ZCSR_LOG_INFO("tag", "%d", value)`，在标准 C11 内无法同时消除尾逗号。
- 约束：扩展只允许存在于 `diagnostics.h`；其它编译器在预处理阶段直接 `#error`，不静默退化。
- 已验证编译器：MSVC 19.51（本机 `/W4 /WX` 构建通过）。
- 未验证：GCC/Clang `-Wpedantic` 实跑；本轮没有 Linux 编译器环境。
- 结论：这不是“已修成 ISO C11”，而是**明确拒绝该验收口径**，并保留可编译、可诊断的扩展实现。

## DEV-002 — MSVC C4996 对 `getenv` 的局部抑制

- 位置：`UIR_Editor/src/runtime.c` 的 `uir_getenv()`
- 状态：**接受为局部偏差**
- 原因：`getenv` 是 ISO C11 只读查询；MSVC 的 C4996 是 CRT 弃用提示，不是缓冲区或类型安全缺陷。
- 约束：抑制只在 `uir_getenv()` 函数体周围生效；UIR/UIM 中真实的 `strcpy/strncpy/_wfopen` 调用已替换（引擎既有 `strncpy` 均有显式终止，不属本轮误报），没有用同一宏全仓关闭 C4996。
