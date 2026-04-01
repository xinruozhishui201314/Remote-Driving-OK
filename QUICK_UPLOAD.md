# 快速上传到 GitHub

## ⚠️ 重要：需要先修复权限

由于 `.git` 目录的所有者是 `root`，需要先修复权限才能执行 Git 操作。

## 一键执行命令

**请按顺序执行以下命令：**

```bash
# 1. 修复权限（需要输入 sudo 密码）
sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving/.git

# 2. 执行上传脚本
cd /home/wqs/bigdata/Remote-Driving
./UPLOAD_COMMANDS.sh
```

## 或者手动执行

如果脚本无法运行，可以手动执行以下命令：

```bash
cd /home/wqs/bigdata/Remote-Driving

# 1. 修复权限
sudo chown -R wqs:wqs .git

# 2. 添加所有文件
git add -A

# 3. 查看将要提交的文件
git status

# 4. 提交更改
git commit -m "Update: Complete M0 milestone and add documentation

- Add M0 verification reports and summaries
- Add backend service implementation  
- Add deployment configuration (Docker Compose)
- Add project documentation and structure
- Update .gitignore to exclude sensitive files
- Add scripts for verification and deployment"

# 5. 推送到 GitHub
git push -u origin master
```

## 验证

上传成功后，访问以下 URL 验证：

**https://github.com/xinruozhishui201314/Remote-Driving**

## 如果遇到问题

### SSH 密钥问题

如果推送时提示权限错误，测试 SSH 连接：

```bash
ssh -T git@github.com
```

如果失败，需要配置 SSH 密钥（参考 GitHub 文档）。

### 仍然无法写入 .git

如果修复权限后仍然无法写入，尝试：

```bash
sudo chown -R wqs:wqs /home/wqs/bigdata/Remote-Driving
```
