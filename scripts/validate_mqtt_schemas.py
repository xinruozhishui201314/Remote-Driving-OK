#!/usr/bin/env python3
"""
验证各模块 MQTT 消息是否与 Schema 一致。

用法：
  ./scripts/validate_mqtt_schemas.py
  ./scripts/validate_mqtt_schemas.py --carla-bridge --vehicle-side

输出：
  - 扫描各模块代码，提取发布/订阅消息的字典/JSON 字面量。
  - 与 mqtt/schemas 对比：
    - 字段缺失
    - 字段多余
    - 类型不匹配
    - 必填字段缺失
"""

import argparse
import json
import os
import re
import sys
from typing import Dict, List, Set, Tuple

# 添加 mqtt/schemas 到路径
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SCHEMAS_DIR = os.path.join(PROJECT_ROOT, "mqtt/schemas")
sys.path.insert(0, SCHEMAS_DIR)

try:
    from vehicle_control import VehicleControlSchema
    from vehicle_status import VehicleStatusSchema
except ImportError as e:
    print(f"无法导入 MQTT Schema: {e}")
    sys.exit(1)


def extract_messages_from_file(file_path: str) -> List[Dict[str, any]]:
    """
    从 Python/C++ 文件中提取 MQTT 消息的字典/JSON 字面量。
    极简化实现：仅用于演示，生产环境建议用 parser/AST。
    """
    messages = []
    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
            # 匹配形如 {"type": "start_stream", "vin": ...} 的 JSON 字面量
            pattern = r'\{"[^}]*\}'
            for match in re.finditer(pattern, content):
                s = match.group()
                try:
                    obj = json.loads(s)
                    # 仅考虑包含 "type" 字段的消息（vehicle/control 或 vehicle/status）
                    if "type" in obj:
                        messages.append(obj)
                except json.JSONDecodeError:
                    continue
    except OSError:
        return []
    return messages


def extract_fields(msg: Dict[str, any]) -> Set[str]:
    """从消息中提取所有键（包括嵌套）。"""
    keys = set()
    def recurse(d):
        for k, v in d.items():
            keys.add(k)
            if isinstance(v, dict):
                recurse(v)
            elif isinstance(v, list):
                for item in v:
                    if isinstance(item, dict):
                        recurse(item)
    recurse(msg)
    return keys


def validate_against_schema(
    messages: List[Dict[str, any]],
    schema_type: str,  # "vehicle_control" 或 "vehicle_status"
) -> List[Tuple[str, str, str]]:
    """
    验证消息是否与 Schema 一致，返回 (文件, 问题, 详情) 列表。
    """
    issues = []
    schema = VehicleControlSchema.SCHEMA if schema_type == "vehicle_control" else VehicleStatusSchema.SCHEMA
    schema_required = set(schema.get("required", []))
    schema_props = schema.get("properties", {})
    additional_allowed = schema.get("additionalProperties", True)

    for msg in messages:
        msg_keys = extract_fields(msg)
        # 1. 必填字段缺失
        missing = schema_required - msg_keys
        if missing:
            issues.append((
                "",
                "missing_required",
                f"必填字段缺失: {sorted(missing)}"
            ))
        # 2. 多余字段（若 additionalProperties=False）
        if not additional_allowed:
            extra = msg_keys - set(schema_props.keys())
            if extra:
                issues.append((
                    "",
                    "extra_fields",
                    f"多余字段: {sorted(extra)}"
                ))
        # 3. 类型与范围检查（简化：仅检查存在字段是否符合 schema 中的 type/min/max/enum）
        for k, v in msg.items():
            if k not in schema_props:
                continue
            prop_schema = schema_props[k]
            expected_type = prop_schema.get("type")
            actual_type = type(v).__name__
            if expected_type == "number" and actual_type not in ("int", "float"):
                issues.append((
                    "",
                    "type_mismatch",
                    f"字段 {k} 类型应为 number，实际 {actual_type}"
                ))
            if expected_type == "boolean" and actual_type != "bool":
                issues.append((
                    "",
                    "type_mismatch",
                    f"字段 {k} 类型应为 boolean，实际 {actual_type}"
                ))
            if expected_type == "integer" and actual_type != "int":
                issues.append((
                    "",
                    "type_mismatch",
                    f"字段 {k} 类型应为 integer，实际 {actual_type}"
                ))
            # 最小/最大值检查
            if "minimum" in prop_schema and isinstance(v, (int, float)) and v < prop_schema["minimum"]:
                issues.append((
                    "",
                    "value_out_of_range",
                    f"字段 {k} 值 {v} 小于最小值 {prop_schema['minimum']}"
                ))
            if "maximum" in prop_schema and isinstance(v, (int, float)) and v > prop_schema["maximum"]:
                issues.append((
                    "",
                    "value_out_of_range",
                    f"字段 {k} 值 {v} 大于最大值 {prop_schema['maximum']}"
                ))
            # 枚举检查
            if "enum" in prop_schema:
                if v not in prop_schema["enum"]:
                    issues.append((
                        "",
                        "enum_mismatch",
                        f"字段 {k} 值 {v} 不在允许列表 {prop_schema['enum']}"
                    ))
    return issues


def scan_directory(
    root_dir: str,
    module_filter: List[str] = None,
) -> List[Tuple[str, List[Dict[str, any]]]]:
    """
    扫描目录下的文件，提取 MQTT 消息。
    返回 (文件路径, 消息列表) 列表。
    """
    results = []
    for dirpath, _, filenames in os.walk(root_dir):
        # 过滤模块
        if module_filter:
            rel = os.path.relpath(dirpath, root_dir)
            if not any(m in rel for m in module_filter):
                continue
        for fn in filenames:
            if fn.endswith(".py") or fn.endswith(".cpp") or fn.endswith(".h"):
                fp = os.path.join(dirpath, fn)
                msgs = extract_messages_from_file(fp)
                if msgs:
                    results.append((fp, msgs))
    return results


def main() -> int:
    ap = argparse.ArgumentParser(description="MQTT 消息 Schema 验证工具")
    ap.add_argument("--carla-bridge", action="store_true", help="只检查 carla-bridge")
    ap.add_argument("--vehicle-side", action="store_true", help="只检查 Vehicle-side")
    ap.add_argument("--client", action="store_true", help="只检查 client")
    args = ap.parse_args()

    module_filter = []
    if args.carla_bridge:
        module_filter.append("carla-bridge")
    if args.vehicle_side:
        module_filter.append("Vehicle-side")
    if args.client:
        module_filter.append("client")
    if not module_filter:
        # 默认检查所有模块
        module_filter = ["carla-bridge", "Vehicle-side", "client"]

    print("=========================================")
    print("MQTT 消息 Schema 验证")
    print("=========================================")

    # 扫描
    results = scan_directory(PROJECT_ROOT, module_filter=module_filter)

    all_issues = []
    for fp, msgs in results:
        if not msgs:
            continue
        # 根据 "type" 字段判断 schema 类型
        sample = msgs[0]
        schema_type = "vehicle_control" if sample.get("type") in [
            "start_stream", "stop_stream", "remote_control", "remote_control_ack"
        ] else "vehicle_status"

        issues = validate_against_schema(msgs, schema_type)
        for issue in issues:
            rel_path = os.path.relpath(fp, PROJECT_ROOT)
            all_issues.append((rel_path, schema_type, issue[0], issue[1]))

    # 输出
    if not all_issues:
        print("✓ 未发现 Schema 不一致问题")
        return 0

    print(f"\n发现 {len(all_issues)} 个问题：\n")
    for i, (fp, st, category, detail) in enumerate(all_issues, 1):
        print(f"{i}. [{st}] {fp}")
        print(f"   类别: {category}")
        print(f"   详情: {detail}")
        print()

    print("=========================================")
    print("建议：")
    print("  - 修改 mqtt/schemas/*.json 以统一契约")
    print("  - 各模块使用 Schema 进行验证或生成结构体")
    print("  - 运行 scripts/validate_mqtt_schemas.py 直到无问题")
    print("=========================================")
    return 1


if __name__ == "__main__":
    sys.exit(main())
