# 客户端 Mull 变异测试（操作说明）

[Mull](https://github.com/mull-project/mull) 用于验证「现有单测能否杀死逻辑变异」，与行覆盖率互补。

## 前置条件

1. 使用 **Clang** 与 Mull 文档要求的编译标志生成 **可变异** 的测试二进制与 `compile_commands.json`（通常单独 `build-mull/` 目录，勿与生产 Release 混用）。  
2. 安装 `mull-runner` / `mull-runner-17` 等（与 LLVM 主版本对齐）。

## 与本仓库脚本

```bash
export MULL_BUILD_DIR=/path/to/mull-enabled-build
./scripts/run-client-mutation-sample.sh [mull-runner 额外参数...]
```

未安装 Mull 或未设置 `MULL_BUILD_DIR` 时脚本 **安全退出 0**（与 CI 可选步骤兼容）。

## 推荐首批变异范围（安全相关）

在 `MULL_BUILD_DIR` 下针对已链接的测试可执行文件运行 Mull，例如：

- `test_antireplayguard`  
- `test_commandsigner`  
- `test_rtcpcompoundparser`  
- `test_mqttcontrolenvelope`  

具体 flags 以本机 Mull 版本为准，常见形态包括指定可执行文件、工作目录、超时等，例如：

```text
mull-runner \
  -test-executable ./test_antireplayguard \
  -mutators=all \
  -timeout=10000
```

（参数名随 Mull 主版本变化，请以 `mull-runner --help` 为准。）

## 定期执行

仓库提供 **`.github/workflows/client-mutation-weekly.yml`**（周一 UTC 提醒 + `workflow_dispatch`）。在自托管 runner 上可改为真实 `mull-runner` 步骤并上传 HTML/JSON 报告。
