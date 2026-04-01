# GitHub 上传指南

## 问题诊断

检测到 `.git` 目录的所有者是 `root`，而当前用户是 `wqs`，导致无法执行 Git 操作。

## 解决方案

### 方法 1: 使用提供的脚本（推荐）

1. **修复权限**（需要 sudo 权限）：
   ```bash
   sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving/.git
   ```

2. **运行上传脚本**：
   ```bash
   ./scripts/upload-to-github.sh "Initial commit: Complete M0 milestone"
   ```

### 方法 2: 手动执行步骤

1. **修复权限**：
   ```bash
   sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving/.git
   ```

2. **添加所有文件**：
   ```bash
   cd /home/wqs/bigdata/Remote-Driving
   git add -A
   ```

3. **检查将要提交的文件**：
   ```bash
   git status
   ```

4. **提交更改**：
   ```bash
   git commit -m "Update: Add latest changes and documentation"
   ```

5. **推送到 GitHub**：
   ```bash
   git push -u origin master
   ```

## 已配置的远程仓库

- **远程 URL**: `git@github.com:xinruozhishui201314/Remote-Driving.git`
- **当前分支**: `master`

## 注意事项

### 已排除的文件（通过 .gitignore）

以下文件/目录**不会**被上传：

- `.env` 文件（包含敏感信息）
- `media/ZLMediaKit/`（第三方库，25MB，建议作为子模块或单独处理）
- 构建产物（`build/`, `bin/`, `lib/` 等）
- 日志文件（`*.log`）
- IDE 配置文件（`.vscode/`, `.idea/`）

### ZLMediaKit 处理建议

`media/ZLMediaKit/` 是一个第三方库（有自己的 Git 仓库），建议：

1. **作为 Git 子模块**（推荐）：
   ```bash
   # 先移除现有目录
   rm -rf media/ZLMediaKit
   
   # 添加为子模块
   git submodule add https://github.com/ZLMediaKit/ZLMediaKit.git media/ZLMediaKit
   git commit -m "Add ZLMediaKit as submodule"
   ```

2. **或者在部署文档中说明**：
   在 README 或部署文档中说明如何获取 ZLMediaKit：
   ```bash
   git clone https://github.com/ZLMediaKit/ZLMediaKit.git media/ZLMediaKit
   ```

## 验证上传

上传成功后，访问以下 URL 验证：

```
https://github.com/xinruozhishui201314/Remote-Driving
```

## 故障排查

### SSH 密钥问题

如果推送时提示权限问题，检查 SSH 密钥：

```bash
# 测试 GitHub SSH 连接
ssh -T git@github.com

# 如果失败，需要配置 SSH 密钥
# 参考: https://docs.github.com/en/authentication/connecting-to-github-with-ssh
```

### 权限问题

如果仍然遇到权限问题：

```bash
# 检查 .git 目录所有者
ls -ld .git

# 修复整个项目目录权限（谨慎使用）
sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving
```

### 大文件问题

如果遇到大文件推送失败，检查文件大小：

```bash
# 查找大于 50MB 的文件
find . -type f -size +50M -not -path "./.git/*"
```

GitHub 限制单个文件最大 100MB，建议使用 Git LFS 处理大文件。
