# 客户端 UI 质量链路：官方工具链 + 架构契约 + 自动化门禁 + 测试/观测

| 字段 | 值 |
|------|-----|
| 文档版本 | 1.0 |
| 一键脚本（全量） | [`scripts/verify-client-ui-quality-chain.sh`](../scripts/verify-client-ui-quality-chain.sh) |
| CI 轻量模式 | 同上脚本设 `CLIENT_UI_CHAIN_CI_LITE=1`（跳过需 Docker/Qt 的 `qmllint` + format） |

## 1. 四根支柱是否具备？

| 支柱 | 本仓库状态 | 入口 / 证据 |
|------|------------|-------------|
| **官方工具链** | **具备**（本地/容器） | Qt 6 **`qmllint`** + [`client/qml/.qmllint.ini`](../client/qml/.qmllint.ini)；LLVM **`clang-format-18`**（[`client/.clang-format`](../client/.clang-format)）；可选 **`clang-tidy`**（[`scripts/run-client-clang-tidy-baseline.sh`](../scripts/run-client-clang-tidy-baseline.sh)） |
| **架构契约** | **具备** | [`docs/CLIENT_UI_MODULE_CONTRACT.md`](CLIENT_UI_MODULE_CONTRACT.md)（DrivingFacade v3）；[`scripts/verify-client-ui-module-contract.sh`](../scripts/verify-client-ui-module-contract.sh) |
| **自动化门禁** | **具备**（分层） | **全量**：[`scripts/verify-client-ui-quality-chain.sh`](../scripts/verify-client-ui-quality-chain.sh)；**串联**：[`scripts/verify-client-ui-and-video-coverage.sh`](../scripts/verify-client-ui-and-video-coverage.sh)；**CI**：`.github/workflows/client-ci.yml` 中 `ui-quality-chain-lite`（契约 + 静态 grep + 布局 + 视频结构） |
| **测试 / 观测** | **部分具备** | **单测**：`ctest` / [`scripts/run-all-client-unit-tests.sh`](../scripts/run-all-client-unit-tests.sh)；**功能串联**：[`scripts/verify-all-client-modules.sh`](../scripts/verify-all-client-modules.sh)、[`scripts/run-client-oneclick-all-tests.sh`](../scripts/run-client-oneclick-all-tests.sh)；**观测**：[client/docs/OBSERVABILITY_METRICS.md](../client/docs/OBSERVABILITY_METRICS.md)、日志前缀规范见 [`docs/FEATURE_ADD_CHECKLIST.md`](FEATURE_ADD_CHECKLIST.md) |

**结论**：链路在 **本地/容器** 侧已闭环；**GitHub Actions** 上对 **无 Docker 的 runner** 采用 **轻量门禁**（不跑 `qmllint`/format），避免误红；**完整官方工具链门禁**须在 **client-dev 镜像**或安装 Qt6 的环境执行。

---

## 2. 推荐执行顺序（研发自测）

1. `./scripts/verify-client-ui-quality-chain.sh` — **全量**（需 Docker 镜像 `remote-driving-client-dev:full` 或本机 `qmllint` + `clang-format-18`）。
2. `./scripts/build-and-verify.sh` — 全栈策略见 [`docs/BUILD_AND_RUN_POLICY.md`](BUILD_AND_RUN_POLICY.md)。
3. 容器内 **`ctest --output-on-failure`** — 见 `verify-all-client-modules.sh`。

**仅改 QML/契约相关、无 Qt 环境时**：`CLIENT_UI_CHAIN_CI_LITE=1 ./scripts/verify-client-ui-quality-chain.sh`

---

## 3. 缺口与演进（诚实清单）

| 缺口 | 说明 | 建议 |
|------|------|------|
| GHA 全量 `qmllint` | 托管机拉 `client-dev` 镜像成本高 | 保持 **lite CI** + 发布前/夜间在容器跑全量链 |
| QML `TestCase` 单测 | 仓库内几乎无独立 `.qml` 单测文件 | 对登录/会话切换等加 **最小 QTest/QML** 或保留现有 **脚本化静态验证** |
| `shell/*` 契约 | 文档曾提可另立 `SessionShellContract` | 可仿 `verify-client-ui-module-contract.sh` 增加轻量 grep 门禁 |

---

## 4. 与相邻文档关系

- Qt UI 细则：[CLIENT_QT_UI_GOOGLE_STYLE.md](CLIENT_QT_UI_GOOGLE_STYLE.md)
- C++/工程对齐：[CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md](CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md)
- 模块化评估：[CLIENT_MODULARIZATION_ASSESSMENT.md](CLIENT_MODULARIZATION_ASSESSMENT.md)
