# Media 流媒体服务

本目录包含 ZLMediaKit 流媒体服务器的构建与运行脚本。工程化约定：**media 模块** 与 backend / client / carla-bridge / Vehicle-side 并列，可独立部署；所有端点通过配置指定，见 [docs/DISTRIBUTED_DEPLOYMENT.md](../docs/DISTRIBUTED_DEPLOYMENT.md)。

## 获取 ZLMediaKit

ZLMediaKit 作为子依赖，需单独克隆（本仓库未包含其源码）：

```bash
cd media
git clone --depth 1 https://github.com/ZLMediaKit/ZLMediaKit.git
```

然后执行 `./build.sh` 进行编译。

## 健康与就绪

- **探测方式**：HTTP `GET /index/api/getServerConfig`，返回 200 表示服务可用（与 `scripts/wait-for-health.sh` 中 ZLM 探测一致）。
- **部署前检查**：车端推流需能访问 ZLM 的 RTMP 端口；Client 拉流需能访问 WHEP/WHIP 基地址（Backend 配置 `ZLM_PUBLIC_BASE`）。
