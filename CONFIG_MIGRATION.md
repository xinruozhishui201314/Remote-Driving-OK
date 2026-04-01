# 配置文件迁移说明

## 新旧位置对比

整理后，配置文件从根目录 `config/` 移动到各模块的 `config/` 目录下：

| 配置文件 | 旧位置 | 新位置 |
|---------|--------|--------|
| Backend配置 | `config/backend_config.yaml` | `backend/config/backend_config.yaml` |
| Client配置 | `config/client_config.yaml` | `client/config/client_config.yaml` |
| Vehicle-side配置 | `config/vehicle_config.yaml` | `Vehicle-side/config/vehicle_config.yaml` |

## 向后兼容

为了保持向后兼容，根目录的 `config/` 目录保留了符号链接：

```bash
config/
├── backend_config.yaml -> ../backend/config/backend_config.yaml
├── client_config.yaml -> ../client/config/client_config.yaml
└── vehicle_config.yaml -> ../Vehicle-side/config/vehicle_config.yaml
```

这意味着：
- ✅ 现有的挂载路径 `./config/backend_config.yaml` 仍然有效
- ✅ 现有的脚本和文档引用仍然有效
- ✅ 可以无缝迁移到新的配置文件位置

## 使用建议

### 独立模块部署（推荐）

使用各模块自己的config目录：

```yaml
# docker-compose.yml
services:
  backend:
    volumes:
      - ./backend/config/backend_config.yaml:/app/config/backend_config.yaml:ro

  client:
    volumes:
      - ./client/config/client_config.yaml:/app/config/client_config.yaml:ro

  vehicle:
    volumes:
      - ./Vehicle-side/config/vehicle_config.yaml:/app/config/vehicle_config.yaml:ro
```

### 完整链路部署

可以使用符号链接或直接从各模块config目录挂载：

```yaml
# 方式一：使用符号链接（兼容旧方式）
volumes:
  - ./config/backend_config.yaml:/app/config/backend_config.yaml:ro

# 方式二：直接从模块目录挂载（推荐）
volumes:
  - ./backend/config/backend_config.yaml:/app/config/backend_config.yaml:ro
```

## 迁移步骤

如果之前使用的是根目录的 `config/` 目录中的配置文件，建议：

1. **检查现有配置**
   ```bash
   ls -la config/
   ```

2. **查看符号链接**
   ```bash
   readlink config/backend_config.yaml
   ```

3. **使用新位置（推荐）**
   - 更新 docker-compose.yml，使用各模块的 config/ 目录
   - 更新脚本和文档引用

4. **或保持使用符号链接**
   - 现有配置继续有效，无需修改

## 配置文件加载顺序

各模块的配置加载优先级（从高到低）：

1. 环境变量（最高优先级）
2. 命令行参数
3. 配置文件（`/app/config/<module>_config.yaml`）
4. 默认值

因此，无论配置文件放在哪里，都可以通过环境变量覆盖配置。

## 相关文档

- [工程整理指南](docs/REFACTORING_GUIDE.md)
- [配置指南](docs/CONFIGURATION_GUIDE.md)
- [配置总结](docs/CONFIGURATION_SUMMARY.md)

---
**更新日期**: 2026-02-28
**版本**: v1.0
