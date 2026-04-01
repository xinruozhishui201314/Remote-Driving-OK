#!/bin/bash

# 提交并推送 Security 和 Observability 改进

echo "=== 1. 添加修改的文件 ==="
git add Vehicle-side/src/vehicle_controller.cpp \
        Vehicle-side/src/mqtt_handler.cpp \
        Vehicle-side/src/common/logger.cpp \
        client/src/mqttcontroller.cpp \
        client/src/mqttcontroller.h \
        backend/src/session/teleop_state.cpp

echo "=== 2. 提交修改 ==="
git commit -m "Refactor: implement security and observability improvements per project_spec

- Vehicle-side: Add network quality (RTT) telemetry reporting.
- Vehicle-side: Integrate unified logging (spdlog) for structured output.
- Client-side: Ensure control commands use 'timestampMs' for anti-replay.
- Backend: Remove redundant in-memory session locking logic (activeVinSessions).

Ref: project_spec.md (v1.3)"

echo "=== 3. 推送到 GitHub ==="
# 提示：如果您的分支不是 main 或 master，请手动修改为正确的分支名
# git push origin <your-branch-name>
git push
