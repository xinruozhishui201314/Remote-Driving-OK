#!/usr/bin/env python3
"""
自动诊断脚本：基于关键词与模式，从日志中识别常见问题并给出可能原因与排查步骤。

用法:
  ./scripts/auto_diagnose.py --log-dir ./diags/logs
  ./scripts/auto_diagnose.py --log-dir ./diags/logs --output ./diags/diagnosis.txt
"""

import argparse
import os
import re
from datetime import datetime, timedelta
from typing import List, Tuple

RULES = [
    {
        "name": "MQTT 连接失败",
        "patterns": [
            r"MQTT.*连接失败|Connection lost|Unable to connect",
            r"mosquitto_sub.*启动失败|connection refused",
        ],
        "tags": ["vehicle", "client"],
        "possible_causes": [
            "Broker 地址或端口错误",
            "网络不可达（防火墙/端口未开放）",
            "Broker 未启动或重启中",
        ],
        "actions": [
            "检查 docker compose ps mosquitto 是否 Up",
            "核对 MQTT_BROKER_URL/MQTT_BROKER 配置（协议/主机/端口）",
            "在 client/vehicle 所在网络尝试 telnet <broker> <port>",
            "查看 mosquitto 日志是否有认证失败或拒绝连接记录",
        ],
    },
    {
        "name": "WebRTC 拉流失败（state=Closed）",
        "patterns": [
            r"WEBRTC.*state=Closed|ConnectionClosed",
            r"拉流失败|play.*failed",
        ],
        "tags": ["client"],
        "possible_causes": [
            "WHEP URL 不正确或过期（session 结束后 URL 失效）",
            "ZLM 无该流（车端未推流或推流已停止）",
            "防火墙阻断 UDP/TCP 端口（TURN 未正确配置）",
        ],
        "actions": [
            "确认 session 是否仍然 ACTIVE（通过 backend GET /api/v1/sessions/{sessionId}）",
            "在 ZLM 中检查流是否存在：curl http://<ZLM>/index/api/getMediaList",
            "检查 Coturn 是否正常（TURN UDP/TCP 端口开放、COTURN_EXTERNAL_IP 正确）",
            "在浏览器 F12 查看 WebRTC 统计（RTT/丢包）",
        ],
    },
    {
        "name": "车端推流未到达 ZLM",
        "patterns": [
            r"ZLM.*首帧超时|queue.Empty",
            r"push.*failed|无法推流",
            r"RTMP.*connect.*refused",
        ],
        "tags": ["carla-bridge", "vehicle"],
        "possible_causes": [
            "ZLM 地址或端口错误（ZLM_HOST/ZLM_RTMP_PORT）",
            "网络不可达或端口被阻断",
            "ZLM 未启动或配置不允许该推流",
        ],
        "actions": [
            "核对车端/bridge 的 ZLM_HOST 与 ZLM_RTMP_PORT 是否与部署一致",
            "从车端尝试 telnet <ZLM_HOST> 1935",
            "检查 ZLM 日志是否有推流相关错误",
            "确认 ZLM 的 rtmp 配置是否启用（config.ini 中 rtmp.enable=1）",
        ],
    },
    {
        "name": "backend API 返回 403/401",
        "patterns": [
            r"401.*unauthorized|403.*forbidden",
            r"JWT.*invalid|token.*expired",
        ],
        "tags": ["backend", "client"],
        "possible_causes": [
            "JWT 过期或签名不匹配（iss/aud 配置错误）",
            "VIN 未授权给当前用户（无 vin.view/vin.control）",
            "session 被抢占或已结束",
        ],
        "actions": [
            "在 client 重新登录获取新 token",
            "确认 VIN 已在账号下授权（通过 backend GET /api/v1/vins）",
            "检查 backend 日志中该请求的 vin 与 user_id 是否匹配",
        ],
    },
    {
        "name": "client 退出/崩溃",
        "patterns": [
            r"segfault|Segmentation fault|Aborted|signal 11",
            r"QApplication.*exit|Client.*crash",
        ],
        "tags": ["client"],
        "possible_causes": [
            "QML 加载失败（缺少资源/路径错误）",
            "调用空指针或未初始化对象",
            "第三方库版本不兼容",
        ],
        "actions": [
            "在 client 容器内运行 gdb 或查看 core dump",
            "确认 QML 路径是否正确（是否在容器内可访问）",
            "检查 client 是否缺少依赖（如 libdatachannel）",
            "查看完整日志堆栈，定位崩溃处代码行",
        ],
    },
    {
        "name": "CARLA 连接失败",
        "patterns": [
            r"CARLA.*连接失败|CARLA.*timeout|RPC.*error",
        ],
        "tags": ["carla-bridge"],
        "possible_causes": [
            "CARLA 仿真未启动",
            "CARLA_HOST/CARLA_PORT 配置错误",
            "CARLA 版本与 client bridge 不兼容",
        ],
        "actions": [
            "检查 carla-server 容器是否运行且健康",
            "确认 CARLA_HOST 与 CARLA_PORT 是否正确",
            "在 carla-bridge 容器内尝试 ping/nc CARLA_HOST:2000",
            "查看 CARLA 日志是否有异常",
        ],
    },
    {
        "name": "数据库连接失败",
        "patterns": [
            r"database.*failed|PQconnectdb|connection.*refused",
            r"503.*internal.*database",
        ],
        "tags": ["backend"],
        "possible_causes": [
            "PostgreSQL 未启动",
            "DATABASE_URL 配置错误",
            "网络不可达或端口未开放",
        ],
        "actions": [
            "检查 docker compose ps postgres",
            "从 backend 所在网络尝试 telnet <postgres> 5432",
            "核对 DATABASE_URL 格式（host/port/dbname/user/password）",
        ],
    },
    {
        "name": "Keycloak 连接失败",
        "patterns": [
            r"Keycloak.*failed|KC_DB.*error",
        ],
        "tags": ["backend"],
        "possible_causes": [
            "Keycloak 未启动或未就绪",
            "KEYCLOAK_URL 或 realm 名称错误",
        ],
        "actions": [
            "检查 Keycloak 容器状态与 /health 端点",
            "确认 KEYCLOAK_REALM 与 KEYCLOAK_CLIENT_ID 配置正确",
            "在 backend 所在网络尝试 curl <KEYCLOAK_URL>/realms/<realm>/.well-known/openid-configuration",
        ],
    },
]


def match_in_line(line: str, patterns: List[str]) -> bool:
    return any(re.search(p, line, re.IGNORECASE) for p in patterns)


def diagnose_file(file_path: str) -> List[Tuple[str, int, str]]:
    matches = []
    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f, 1):
                line_stripped = line.strip()
                for rule in RULES:
                    if match_in_line(line_stripped, rule["patterns"]):
                        matches.append((rule["name"], i, line_stripped))
                        break
    except OSError:
        pass
    return matches


def diagnose_dir(log_dir: str) -> List[Tuple[str, int, str]]:
    matches = []
    if not os.path.isdir(log_dir):
        return matches
    for name in os.listdir(log_dir):
        path = os.path.join(log_dir, name)
        if os.path.isfile(path):
            matches.extend(diagnose_file(path))
    return matches


def format_output(matches: List[Tuple[str, int, str]], output: str):
    lines = []
    lines.append("=" * 60)
    lines.append("自动诊断结果")
    lines.append("=" * 60)
    lines.append("发现以下可能的问题（按规则匹配，非 100% 精准）：\n")

    grouped = {}
    for rule_name, line_no, line in matches:
        grouped.setdefault(rule_name, []).append((line_no, line))

    for rule_name in sorted(grouped.keys()):
        lines.append(f"\n【问题】{rule_name}")
        lines.append("  可能原因：")
        rule = next((r for r in RULES if r["name"] == rule_name), None)
        if rule:
            for c in rule["possible_causes"]:
                lines.append(f"    - {c}")
            lines.append("  排查步骤：")
            for a in rule["actions"]:
                lines.append(f"    - {a}")
        lines.append("  相关日志行：")
        for line_no, line in grouped[rule_name][:5]:
            lines.append(f"    L{line_no}: {line[:120]}...")
        if len(grouped[rule_name]) > 5:
            lines.append(f"    ... 还有 {len(grouped[rule_name]) - 5} 行")

    if not matches:
        lines.append("\n未发现匹配已知问题的日志，建议：")
        lines.append("  - 按时间范围过滤（例如最近 10 分钟）手动查看日志")
        lines.append("  - 使用 ./scripts/analyze_interaction_log.py 查看交互时间线")
        lines.append("  - 结合具体现象（黑屏/无控制/崩溃）查阅 docs/TROUBLESHOOTING_RUNBOOK.md")

    lines.append("\n" + "=" * 60)
    text = "\n".join(lines)
    if output:
        with open(output, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        print(text)


def main() -> int:
    ap = argparse.ArgumentParser(description="自动诊断脚本")
    ap.add_argument("--log-dir", default="./diags/logs", help="日志目录（包含 *.log）")
    ap.add_argument("--output", help="输出文件（默认 stdout）")
    args = ap.parse_args()

    matches = diagnose_dir(args.log_dir)
    format_output(matches, args.output)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
