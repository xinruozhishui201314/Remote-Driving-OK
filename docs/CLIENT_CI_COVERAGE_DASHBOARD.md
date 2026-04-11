# 客户端覆盖率与变异结果上屏（V2 看板）

## 目标

将 **`lcov` 行覆盖率**与可选 **Mull 变异存活率**从「本机 HTML」升级为团队可见的趋势数据（PR 评论、主分支徽章或 BI）。

## 推荐接入方式

| 方案 | 适用 | 做法概要 |
|------|------|----------|
| **Codecov** | GitHub/GitLab 托管 | CI 中 `lcov --capture` 后上传 `coverage.info`（`codecov/codecov-action`）；按 `client/src/core/*` 等路径配置 [components](https://docs.codecov.com/docs/components)。与本仓库 `docs/CLIENT_UNIT_TEST_SOURCE_MAP.md` 前缀对齐。 |
| **GitHub Actions Artifacts** | 不引入第三方 | `run-client-coverage-with-thresholds.sh` 生成的 `coverage-html` 目录打包 `actions/upload-artifact`，保留 14–30 天。 |
| **SonarQube / SonarCloud** | 企业统一质量门 | 导入 `coverage.info`（Generic Test Data）+ CFamily 规则；与 `CLIENT_COVERAGE_ENFORCE` 双轨：Sonar 管趋势，脚本管本地/容器即时失败。 |
| **Mull** | 核心模块检错测试力度 | `mull-runner` 输出 JSON/HTML；可解析存活变异数上传自定义指标或粘贴到 PR 模板。 |

## CI 与本仓库脚本的关系

- **分路径门禁**：`scripts/run-client-coverage-with-thresholds.sh` 中 `UTILS_LINES_MIN`、`INFRA_LINES_MIN`、`APP_LINES_MIN` 等与 Codecov `component` 使用同一路径语义（`client/src/...`）。**分支**：`OVERALL_BRANCHES_MIN`、`CORE_BRANCHES_MIN` 等同脚本注释。
- **映射表契约**：`.github/workflows/client-ci.yml` 的 `source-map-sync` job 保证 CMake 与 `CLIENT_UNIT_TEST_SOURCE_MAP.md` 一致，避免「有覆盖率无文档归属」。

## 最小 GitHub Actions 片段（上传 lcov）

```yaml
- run: ./scripts/run-client-coverage-with-thresholds.sh
  env:
    CLIENT_COVERAGE_ENFORCE: '0'
- uses: codecov/codecov-action@v4
  with:
    files: client/build-coverage/coverage.info
    flags: client
```

（需在 **client-dev** 或完整 Qt 链下先产生 `coverage.info`；托管 runner 若缺依赖，仅在自托管或容器 job 中执行。）

## 指标建议

- **必看**：`core/`、`services/`、`media/` 行覆盖率趋势；PR diff 覆盖。
- **增强**：Mull「存活变异数」按模块周环比；L4 脚本失败率（`verify-client-*.sh`）。
