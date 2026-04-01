# Git LFS 配置说明

本仓库使用 [Git LFS](https://git-lfs.com/) 管理超过 GitHub 建议大小（50 MB）的大文件，当前主要针对 `deploy/carla/deps/cmake.tar.gz`（CMake 预编译包）。

---

## 1. 安装 Git LFS（仅需一次）

### Linux（Debian/Ubuntu）

```bash
sudo apt-get update
sudo apt-get install git-lfs
git lfs install
```

### 其他方式

- 从 [git-lfs.com](https://git-lfs.com/) 下载对应平台安装包并执行安装脚本。
- 安装后执行一次：`git lfs install`（为当前用户启用 LFS 钩子）。

验证：

```bash
git lfs version
```

---

## 2. 本仓库已配置的 LFS 规则

在项目根目录的 `.gitattributes` 中已配置：

- `deploy/carla/deps/*.tar.gz` → 使用 LFS 跟踪（如 `cmake.tar.gz`）。
- 其他大二进制可按需追加规则。

克隆或拉取后，LFS 文件会自动按指针下载；需要完整拉取 LFS 内容时：

```bash
git lfs pull
```

---

## 3. 日常使用（已配置 LFS 后）

- **提交大文件**：与普通文件一样 `git add`、`git commit`，由 `.gitattributes` 自动走 LFS。
- **克隆仓库**：`git clone <url>` 会拉取 LFS 指针；要同时拉取大文件内容：`git lfs clone <url>` 或克隆后执行 `git lfs pull`。
- **查看 LFS 文件**：`git lfs ls-files`。

---

## 4. 将“已提交”的大文件迁入 LFS（可选）

若大文件已在历史提交中且希望迁入 LFS 并重写历史（可减小仓库体积、避免 GitHub 大文件警告），可按以下步骤操作。

> **注意**：会重写历史、变更 commit SHA，所有基于该分支的协作需重新基于新历史。执行前请备份或确认无他人依赖当前历史。

```bash
# 1. 确保工作区干净
git status   # 应为干净或先 commit/stash

# 2. 按扩展名将历史中的大文件迁入 LFS（仅当前分支）
git lfs migrate import --include="deploy/carla/deps/*.tar.gz" --everything

# 3. 检查
git lfs ls-files

# 4. 强制推送（会覆盖远程历史，需有权限）
git push --force-with-lease origin master
```

若只迁移单个文件：

```bash
git lfs migrate import --include="deploy/carla/deps/cmake.tar.gz" --everything
```

迁移后新克隆的仓库将直接得到 LFS 版本，旧 commit 中的大文件会变为 LFS 指针。

---

## 5. 常见问题

| 现象 | 处理 |
|------|------|
| `git lfs` 命令不存在 | 安装 Git LFS 并执行 `git lfs install` |
| 克隆后大文件是几行文本（指针） | 执行 `git lfs pull` 拉取真实文件 |
| 推送时报 “large files” | 确保已用 `git lfs track` / `.gitattributes` 跟踪该路径，并重新 `git add` 该文件后提交 |
| 想不再用 LFS 管理某类文件 | 从 `.gitattributes` 中删除对应行，并可选使用 `git lfs migrate export` 迁回普通 Git |

---

## 6. 参考

- [Git LFS 官方文档](https://git-lfs.github.com/)
- [Installing Git Large File Storage (GitHub)](https://docs.github.com/en/repositories/working-with-files/managing-large-files/installing-git-large-file-storage)
- [git lfs migrate](https://github.com/git-lfs/git-lfs/blob/main/docs/man/git-lfs-migrate.adoc)
