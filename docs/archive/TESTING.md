# Testing

Two complementary layers, kept in **separate directories on purpose**:

| Directory | Purpose | Runs where |
|---|---|---|
| `tests/` | **Per-module unit / contract tests** — each module exercised *in isolation* against its contract header, plus one cross-module wiring test. Correctness gates. | Headless, via `ctest`. |
| `integration/` | **Runnable demo + performance benchmarks** — modules composed into a running app, and frame-time / throughput measured. | Manual; some need a real display / sound card / gamepad. |

## Why two directories

The split mirrors the project's zero-coupling rule:

- **`tests/` proves each module works alone.** A module compiles independently and is tested only through its public contract — never by reaching into another module. So each `tests/test_<module>.c` links exactly the one module (plus the contract layer it depends on) and asserts that module's behavior. These are fast, deterministic, **headless**, and are the suite a CI job runs. The single exception, `tests/test_integration.c`, checks that several modules wire together *correctly* (logic), not performance.
- **`integration/` proves the modules work together as a real app, and how fast.** It holds the `zcsr_demo` (wires the whole stack and prints status) and the benchmarks (`bench*.c` — sprite frame-time, worst-case adversary, raylib comparison, culling, atlas, etc.). These are about **composition and performance**, and several depend on a real GPU / window / device, so they are not part of the headless correctness suite. Benchmarks **skip** themselves when run headless rather than fail.

Keeping them apart means the correctness suite stays fast, deterministic and CI-friendly, while the display-/hardware-dependent demo and perf artifacts don't pollute it.

## tests/ — unit & contract suite (`ctest`)

One test executable per module, registered with CTest:

`test_core`, `test_signal`, `test_hsm`, `test_overlay`, `test_render`, `test_input`, `test_audio`,
`test_gamelogic`, `test_glrender`, `test_audiomix`, `test_inputext`, `test_image`, `test_atlas`,
`test_culling`, `test_state`, `test_state_reuse`, and `test_integration` (cross-module wiring).

That is **17 test targets** total, all registered with `add_test()`.

GPU/audio/window-touching tests (`test_glrender`, `test_overlay`, …) keep their headless-safe checks always on and guard the parts that need a real display behind a runtime check, so the suite passes on a headless box.

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build            # run the whole unit/contract suite
ctest --test-dir build -V         # verbose (see each assertion)
ctest --test-dir build -R glrender  # one module by name
ctest --test-dir build -R state     # state sort