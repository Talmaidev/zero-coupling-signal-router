# zcsr 总规划

生成：2026-10-16
基线：tag android-phase1（HEAD ce41de0）
用途：豆包保存，供后续 AI 接力使用

## 一、项目定位

跨平台 2D 游戏引擎。零耦合模块设计，契约头驱动。

支持平台下限：Windows 10 以上、macOS 12 以上、Linux 桌面、Android API 24 以上、iOS 15 以上（未做）。

不做：3D 渲染、手柄作为移动端一等输入、旧设备兼容、厂商 SDK。

## 二、已完成

- Windows：主线封存，ctest 17/17
- Linux：主线封存，ctest 17/17
- Android：Phase 0 + Phase 1 完成，真机验证，tag android-phase1
  - 1A 窗口 / 1B EGL-GLES / 1C 纹理精灵 / 1D AAudio / 1E 触摸

## 三、未完成任务全清单

### A 组 平台边缘情况

- A1 显示器枚举 + 窗口尺寸 + 窗口模式 + 移屏
- A2 DPI 变化处理
- A4 窗口尺寸约束
- A5 焦点丢失 / 恢复
- A6 统一日志接口
- A3 音频设备切换

### B 组 macOS 接入

- B1 AudioUnit 后端
- B2 .app bundle 打包
- B3 GitHub Actions macOS CI
- B4 极简 README + CLEANUP
- B5 借群友 Mac 真机验证

### C 组 扩展层

- C1 外部库接入规则（已写入 v4.0）
- C2 zcsr_physics.h + Box2D 后端
- C3 zcsr_light2d.h

### D 组 文档 / 交接

- D1 v4.0 启动指令修正
- D2 新对话必读文件包清单

### E 组 低优先

- E1 iOS 支持
- E2 多点触控
- E3 Android 键盘

## 四、推荐完成顺序

### 阶段 1：A 组（平台边缘情况）

A1 → A2 → A4 → A5 → A6 → A3

理由：A1/A2 基础，其余依赖它；A6 在 A3 前，因为 A3 需要日志；A3 最复杂放最后。

### 阶段 2：B 组（macOS 接入）

理由：A 组打稳后，macOS 一次测完，不返工。

### 阶段 3：C 组（物理 / 光照）

理由：等真做游戏时再引，避免过早设计。

### 阶段 4：D 组（文档）

理由：每阶段完成顺手更新，不单独做。

### 阶段 5：E 组（按需）

## 五、测试环境

- Windows 主机：手动跑，输出贴回
- Linux WSL2：跑大部分验证
- Linux VMware：只做 WSL2 覆盖不到的（X11 真窗口、ALSA 真设备）
- 不让 agent 控制虚拟机，你手动执行，贴输出回来

## 六、当前下一步

开 A 组，从 A1 开始。详细设计见 A PLAN 终版。

## 七、关键约束（贯穿所有阶段）

1. 契约头改动 additive 优先，breaking 先提议
2. 每阶段独立 commit，Conventional Commits
3. 每阶段跑 Windows 回归 17/17 + 耦合守卫 PASS
4. 不 git push
5. 失败保留原始日志，不猜
6. 外部库只在后端模块 include，契约头不暴露第三方类型
7. 引擎不依赖第三方数学库，自带最简 vec/mat

## 八、A 组完成后的状态（修正版）

- Windows / Linux 平台边缘情况达到本阶段验收标准。
- Android 对不支持操作返回明确 false。
- macOS 由 B 组负责。
- 不宣称整个跨平台层生产可用。

## 九、阶段判断的依据

**为什么先 A 组再做 macOS：**
A 组是基础设施（分辨率、DPI、焦点、日志、音频设备切换）。这些不做，macOS 也要重测一遍。先做 A，macOS 一次测完。

**为什么 C 组（物理/光照）放后面：**
现在没有游戏需求驱动。过早设计的物理接口 90% 是错的。等真做游戏时，按实际需求设计 zcsr_physics.h。

**为什么 E 组（iOS/多点触控/Android 键盘）最后：**
无当前需求。保留在清单里，不主动做。

**为什么 D 组（文档）不单独做：**
每阶段完成顺手更新 v4.0。不专门开文档阶段，避免文档与代码脱节。

## 十、每阶段完成的标志

1. 契约头 additive 改动落盘
2. 各平台后端实现
   - Windows / Linux：必须实现
   - Android：明确 no-op（返回 false，不伪装成功）
   - macOS：占位，留 B 组
3. ctest 17/17 不退化
4. 耦合守卫 PASS
5. 真机验证：Win + Linux
6. 独立 commit
7. 保留原始日志

## 十一、关键 commit（已封存）

- b541845 1E 触摸+手柄
- 66a72c2 1D AAudio
- 44cd59d 1C 收尾
- 1367960 1B EGL/GLES
- dca490c 1A 窗口生命周期
- 5140d72 Windows 后端 + 项目整理
- 0bdd0dc 2D 优化四件套
- tag android-phase1 Phase 1 完整封存

## 十二、已知缺口（登记，未实现）

- 分辨率 / 显示器适配 API
- DPI 变化处理
- 音频设备切换监听
- 窗口最小尺寸 / 尺寸约束
- 焦点丢失 / 恢复
- 错误日志 / 崩溃捕获（崩溃部分拆出，A 组不做）
- 重力 / 物理（计划集成 Box2D）
- 光照（2D 简易光照待设计）
