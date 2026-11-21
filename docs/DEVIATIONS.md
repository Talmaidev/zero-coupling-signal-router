# ZCSR 语法与 ABI 偏差记录

本文只记录明确不采纳或接受为已知偏差的条目；已执行的整改不在这里重复。

## DEV-001 — `##__VA_ARGS__` 零实参日志宏

- 位置：`include/zcsr/diagnostics.h`
- 状态：**不采纳“ISO C11 严格零实参变参宏”要求；接受 GNU/MSVC/Clang 扩展**
- 原因：ISO C11 没有 `__VA_OPT__`。同一宏既要支持 `ZCSR_LOG_INFO("tag", "message")`，又要支持 `ZCSR_LOG_INFO("tag", "%d", value)`，在标准 C11 内无法同时消除尾逗号。
- 约束：扩展只允许存在于 `diagnostics.h`；其它编译器在预处理阶段直接 `#error`，不静默退化。
- 已验证编译器：MSVC 19.51（本机 `/W4 /WX` 构建通过）。
- 未验证：GCC/Clang `-Wpedantic` 实跑；当前没有 Linux 编译器环境。
- 结论：不按 ISO C11 严格零实参口径验收；保留可编译、可诊断的扩展实现。
