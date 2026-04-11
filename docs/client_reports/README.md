# 客户端报告产物说明（可写副本）

部分环境下 `client/reports/` 可能由 **Docker 以 root 创建**，宿主机用户无法写入。说明与样本可放在本目录。

| 产物 | 路径 |
|------|------|
| **clang-tidy 全量基线** | `client/reports/clang-tidy-baseline.txt`（由 Docker/脚本生成；大文件可不提交） |
| **生成命令** | [`scripts/run-client-clang-tidy-baseline.sh`](../../scripts/run-client-clang-tidy-baseline.sh)（容器内缺 `clang-tidy-18` 时会自动 `apt-get install`） |
| **Qt UI 门禁** | [`scripts/verify-client-qt-ui-google-style.sh`](../../scripts/verify-client-qt-ui-google-style.sh)（契约 + `qmllint` + UI C++ format） |
| **工程对齐说明** | [`docs/CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md`](../CLIENT_GOOGLE_ENGINEERING_ALIGNMENT.md) |

若需修复目录属主：`sudo chown -R "$USER:$USER" client/reports`
