#!/usr/bin/env python3
"""
MVP：契约真源静态校验（由 verify-contract-artifacts.sh 使用 venv 运行）
- OpenAPI：openapi-spec_validator
- MQTT：jsonschema Draft-07 + mqtt/schemas/examples/manifest.json golden
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import yaml
from jsonschema import Draft7Validator
from openapi_spec_validator import validate_spec


def fail(msg: str) -> None:
    print(f"[Contract][FAIL] {msg}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    openapi_path = root / "backend" / "api" / "openapi.yaml"
    schemas_dir = root / "mqtt" / "schemas"

    if not openapi_path.is_file():
        fail(f"OpenAPI 真源缺失: {openapi_path}")

    print(f"[Contract] OpenAPI: {openapi_path}")
    with openapi_path.open("r", encoding="utf-8") as f:
        spec = yaml.safe_load(f)
    try:
        validate_spec(spec)
    except Exception as e:
        fail(f"OpenAPI 校验失败: {e}")
    print("[Contract] OpenAPI: validate_spec OK")

    schema_files = sorted(p for p in schemas_dir.glob("*.json") if p.is_file())
    if not schema_files:
        fail(f"未找到 MQTT JSON Schema: {schemas_dir}")

    validators: dict[str, Draft7Validator] = {}
    for sf in schema_files:
        with sf.open("r", encoding="utf-8") as f:
            schema = json.load(f)
        try:
            Draft7Validator.check_schema(schema)
        except Exception as e:
            fail(f"Schema 元校验失败 {sf.name}: {e}")
        validators[sf.name] = Draft7Validator(schema)
        print(f"[Contract] MQTT schema OK: {sf.name}")

    manifest = schemas_dir / "examples" / "manifest.json"
    if not manifest.is_file():
        fail(f"缺少 golden 清单: {manifest}")

    with manifest.open("r", encoding="utf-8") as f:
        man = json.load(f)
    examples = man.get("examples")
    if not isinstance(examples, list) or not examples:
        fail("manifest.json 中 examples 须为非空数组")

    for entry in examples:
        if not isinstance(entry, dict):
            fail("manifest 每条 example 须为对象")
        sname = entry.get("schema")
        iname = entry.get("instance")
        if not isinstance(sname, str) or not isinstance(iname, str):
            fail("每条 example 须含 schema、instance 字符串字段")
        if sname not in validators:
            fail(f"未知 schema: {sname}（相对 mqtt/schemas/）")
        inst_path = schemas_dir / "examples" / iname
        if not inst_path.is_file():
            fail(f"实例文件缺失: examples/{iname}")
        with inst_path.open("r", encoding="utf-8") as f:
            instance = json.load(f)
        validator = validators[sname]
        errs = sorted(validator.iter_errors(instance), key=lambda e: list(e.path))
        if errs:
            fail(f"{iname} 不符合 {sname}: {errs[0].message}")
        print(f"[Contract] golden OK: {iname} -> {sname}")

    print("[Contract] 全部契约静态校验通过")


if __name__ == "__main__":
    main()
