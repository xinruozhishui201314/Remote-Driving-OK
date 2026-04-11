# 客户端「Google 级工程规范」对齐说明（含 Qt / `m_`）

| 字段 | 值 |
|------|-----|
| 文档版本 | 1.0 |
| 关联 | [CLIENT_MODULARIZATION_ASSESSMENT.md](CLIENT_MODULARIZATION_ASSESSMENT.md)、[CLIENT_UI_MODULE_CONTRACT.md](CLIENT_UI_MODULE_CONTRACT.md)（HTTP/MQTT 契约总表见 `project_spec.md` §12 + `contract-ci`）、[CLIENT_QT_UI_GOOGLE_STYLE.md](CLIENT_QT_UI_GOOGLE_STYLE.md)、[CLIENT_UI_QUALITY_CHAIN.md](CLIENT_UI_QUALITY_CHAIN.md)（四链路：工具链+契约+门禁+测试/观测） |
| C++ 风格真源 | [`client/.clang-format`](../client/.clang-format)、[`client/.clang-tidy`](../client/.clang-tidy) |

## 1. Executive Summary

本仓库客户端追求 **与 Google 公开工程文化同方向的稳定性与规范性**（可测试、可观测、模块边界清晰、关键路径可推理），**不追求**对 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 的 **字面逐条复刻**。

**刻意保留的 Qt 惯例（与「纯 Google C++ 字面」的差异）**：

- **成员命名**：`m_` 前缀（由 `clang-tidy` `readability-identifier-naming.MemberPrefix` 强制），而非 Google 文档中常见的私有成员 `trailing_underscore_`（全仓重命名风险高，未在本阶段改动）。
- **格式化基线**：`clang-format` 使用 **`BasedOnStyle: Google`**（缩进 2 等随 Google 预设）、**`ColumnLimit: 100`**（较常见 80 列略宽，减少无谓换行），并保留 **Qt 头文件 `IncludeCategories`**。建议在容器内使用 **`clang-format-18`** 与 LLVM 18 工具链一致。
- **MOC / 信号槽 / QML 绑定**：必须容忍 Qt 宏与代码生成；不适用 Google 内部 Bazel `strict deps` 的一键等价物，由 **目录约定 + 契约脚本** 补偿（见模块化评估文档 §2）。

**QML / Qt UI**：无 Google 官方 QML 指南；**规范真源**为 **契约门禁 + 全量 `qmllint`（`client/qml/.qmllint.ini`）+ UI 相关 C++ 的 `clang-format` 校验** — 见 [CLIENT_QT_UI_GOOGLE_STYLE.md](CLIENT_QT_UI_GOOGLE_STYLE.md) 与 `./scripts/verify-client-qt-ui-google-style.sh`。

---

## 2. 「Google 规范」在本项目中的可操作定义

| 层级 | 含义 | 如何验证 |
|------|------|----------|
| **工程原则子集** | IWYU 精神、头文件顺序、线程模型可解释、资源 RAII、关键路径日志与指标 | 代码评审 + [`docs/CLIENT_MODULARIZATION_ASSESSMENT.md`](CLIENT_MODULARIZATION_ASSESSMENT.md) 检查表 |
| **C++ 静态分析** | `clang-tidy`（`cppcoreguidelines`/`modernize`/`performance`/`readability`/`bugprone`/`misc`） | [`scripts/run-client-clang-tidy-baseline.sh`](../scripts/run-client-clang-tidy-baseline.sh) |
| **QML 契约** | Facade 形状、破坏性变更语义 | `./scripts/verify-client-ui-module-contract.sh` |
| **HTTP/MQTT 契约** | OpenAPI + JSON Schema + golden（§12 MVP） | `./scripts/verify-contract-artifacts.sh`；CI：`contract-ci.yml` |
| **跨服务 API** | OpenAPI 路径 vs Backend 路由（§12 V1） | `./scripts/verify-contract-v1-cross-service.sh` |
| **Breaking 检测** | PR 上 OpenAPI 不兼容变更（§12 V2） | `./scripts/verify-openapi-breaking-change.sh`（需 Docker 拉取 `tufin/oasdiff`） |

---

## 3. 5 Whys：为何是「Google 级 + Qt」，而非「纯 Google C++」？

| # | Why | 结论 |
|---|-----|------|
| 1 | 为何对标 Google？ | 大规模 C++ 长期可维护、审查可预期、缺陷可定位。 |
| 2 | 为何 `ColumnLimit` 用 100 而非严格的 80？ | **减少无谓换行**；与许多 Google 系开源项目「Google + 略宽列」做法一致，仍由 `clang-format` 统一。 |
| 3 | 为何保留 `m_`？ | **Qt / 本仓库既有代码一致性与 clang-tidy 强制**；改为 Google 典型的 `trailing_underscore_` 需全量重命名，易与 Q_PROPERTY/历史提交冲突。 |
| 4 | 为何 QML 不套用 cpplint？ | QML 不是 C++；应以 **组件边界 + 契约脚本** 保证接口稳定。 |
| 5 | 根因 | **机械风格**已由 **`BasedOnStyle: Google` 的 clang-format** 收敛；**命名真源**仍为 **Qt 惯用 `m_` + camelBack**；工程原则见契约与 clang-tidy。 |

（模块化动机相关的 5 Whys 仍以 [CLIENT_MODULARIZATION_ASSESSMENT.md §3](CLIENT_MODULARIZATION_ASSESSMENT.md) 为准，与本表互补。）

---

## 4. 与 Google C++ Style Guide 的映射（摘录）

| Google 指南要点 | 本项目状态 |
|-----------------|------------|
| Include What You Use | 鼓励直接包含；`misc-include-cleaner` 会报缺失头（见 clang-tidy 基线）。 |
| Names / Order of Includes | `clang-format` `IncludeCategories` 约束 Qt/系统/本地顺序。 |
| 线程与并发 | 视频路径见 [CLIENT_CPP_HOTPATH_VIDEO_AUDIT.md](CLIENT_CPP_HOTPATH_VIDEO_AUDIT.md)。 |
| 命名（私有成员） | **采用 `m_`，非 Google 典型 `foo_`**（有意差异）。 |

---

## 5. clang-tidy 与编译器版本注意

- **Ubuntu 默认 `clang-tidy`（LLVM 10）** 在解析 **Qt 6.8** 头文件时可能崩溃；请在 `remote-driving-client-dev:full` 等环境中使用 **`clang-tidy-18`**（或同主版本 LLVM）跑基线脚本。
- `readability-identifier-naming` 中 **LLVM 18** 使用 `camelBack`（等价于惯称的 lowerCamelCase），已在 [`client/.clang-tidy`](../client/.clang-tidy) 配置；勿再使用已废弃的拼写 `camelCase`（会触发 `clang-tidy-config` 告警）。

---

## 6. 编译 / 运行说明（格式化与静态分析基线）

1. 在 **client-dev 镜像**内配置并生成 `compile_commands.json`（或挂载已配置的 `client/build`）。
2. **仅格式化 C++**：`./scripts/format-client-cpp.sh`（需本机或容器内 `clang-format-18`）。
3. 执行：`./scripts/run-client-clang-tidy-baseline.sh`  
   - 镜像内若**已有** `clang-tidy-18`（或环境变量 `TIDY` 指向的可执行文件），直接使用。  
   - 若**没有**，脚本在检测到容器环境（如 `/.dockerenv`）时会 **`apt-get install clang-tidy-18`**（已装包则 apt 几乎无操作）。宿主机可设 `CLANG_TIDY_AUTO_INSTALL=1` 强制走 apt（需具备权限）。  
4. 产物：`client/reports/clang-tidy-baseline.txt`（可由 CI 或本地归档，大文件可不提交 Git）。若该目录为 Docker root 所建无法写入，见 [`docs/client_reports/README.md`](client_reports/README.md)。

---

## 7. 验证清单

| 项 | 命令 / 产物 |
|----|-------------|
| C++ 格式化（Google 基线） | `./scripts/format-client-cpp.sh`（`clang-format-18`） |
| Qt UI「Google 级」串联 | `./scripts/verify-client-qt-ui-google-style.sh`（契约 + qmllint + **QML 相关 C++** `clang-format`，路径见 `scripts/lib/collect-qml-related-cpp.sh`） |
| QML 契约（含于上或单独） | `./scripts/verify-client-ui-module-contract.sh` |
| clang-tidy 基线 | `./scripts/run-client-clang-tidy-baseline.sh` → `client/reports/clang-tidy-baseline.txt`（**须跑完全部 79 个 .cpp** 后尾部出现 `Done. Files: 79`；中断产物仅作参考，请重跑刷新） |
| 全量构建验证 | `./scripts/build-and-verify.sh`（按仓库策略在容器内执行客户端构建） |

---

## 8. 风险与回滚

- **收紧 clang-tidy**：可能产生大量告警；建议先基线再分模块消减。
- **格式化风格切换**：全仓 `clang-format` 重排影响 `git blame`；应独立变更、可回滚。

---

## 9. 后续演进（MVP → V1 → V2）

- **MVP**：契约脚本 + clang-tidy 基线脚本（本交付）。
- **V1**：在 CI 中对增量文件或热点目录跑 `clang-tidy`（可选失败阈值）。
- **V2**：IWYU 或 CMake 目标级 include 清理、按模块的 `HeaderFilterRegex` 降噪。
