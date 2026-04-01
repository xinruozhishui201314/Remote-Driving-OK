# 档位显示与档位选择 修改验证清单

## 代码已修改项（已落实）

- [x] **档位显示**（左侧）：`Layout.preferredWidth: 100`、`Layout.minimumWidth: 100`、`Layout.maximumWidth: 100`，与档位选择等宽
- [x] **档位选择**（右侧）：`Layout.preferredWidth: 100`、`Layout.minimumWidth: 100`、`Layout.maximumWidth: 100`，内部 Row `anchors.centerIn: parent`、`spacing: 10`
- [x] 整行：`anchors.margins: 12`、`spacing: 14`
- [x] 所有竖线分隔：`Layout.maximumHeight: 44`、`opacity: 0.5`

## 自动化验证（必须通过）

**每次修改界面后请执行：**
```bash
cd /home/wqs/bigdata/Remote-Driving
bash scripts/verify-client-ui.sh
```
脚本会：1）检查/启动 client-dev 容器；2）编译客户端；3）运行客户端 6 秒。若输出 `VERIFY_OK` 且退出码为 0，则 QML 加载成功、无崩溃。

## 手动界面核对（可选）

1. 运行 `bash scripts/run-client-ui.sh --reset-login`，登录并进入主驾驶界面。
2. 查看主视图最下方一行：档位圆环与档位选择（P N R D）是否左右对称、等宽，中间分隔与间距是否均匀。

## 文件位置

- 实现：`client/qml/DrivingInterface.qml`
- 档位显示：约第 665–710 行
- 档位选择：约第 912–950 行
