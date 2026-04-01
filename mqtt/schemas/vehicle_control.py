"""
MQTT vehicle/control 消息 Schema 定义与验证
"""

import json
from typing import Dict, Any

SCHEMA: Dict[str, Any] = {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "$id": "urn:teleop:schemas:vehicle_control",
    "title": "MQTT vehicle/control message",
    "type": "object",
    "required": ["type", "vin"],
    "properties": {
        "type": {"type": "string", "enum": ["start_stream", "stop_stream", "remote_control", "remote_control_ack"]},
        "vin": {"type": "string", "minLength": 1},
        "steering": {"type": "number", "minimum": -1.0, "maximum": 1.0},
        "throttle": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "brake": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "gear": {"type": "integer", "enum": [-1, 0, 1, 2, 3, 4]},
        "remote_enabled": {"type": "boolean", "default": True},
        "streaming": {"type": "boolean", "default": False},
    },
    "additionalProperties": False,
}


def validate(msg: Any) -> bool:
    """
    验证消息是否符合 vehicle_control Schema。
    返回 True 若合法；False 否。
    """
    if not isinstance(msg, dict):
        return False
    required = SCHEMA.get("required", [])
    for r in required:
        if r not in msg:
            return False
    props = SCHEMA.get("properties", {})
    additional_allowed = not SCHEMA.get("additionalProperties", True)
    for k, v in msg.items():
        if k not in props:
            if not additional_allowed:
                return False
            continue
        prop_schema = props[k]
        expected_type = prop_schema.get("type")
        if expected_type == "string" and not isinstance(v, str):
            return False
        if expected_type == "number" and not isinstance(v, (int, float)):
            return False
        if expected_type == "boolean" and not isinstance(v, bool):
            return False
        if expected_type == "integer" and not isinstance(v, int):
            return False
        if "minimum" in prop_schema and isinstance(v, (int, float)) and v < prop_schema["minimum"]:
            return False
        if "maximum" in prop_schema and isinstance(v, (int, float)) and v > prop_schema["maximum"]:
            return False
        if "enum" in prop_schema and v not in prop_schema["enum"]:
            return False
        if "minLength" in prop_schema and isinstance(v, str) and len(v) < prop_schema["minLength"]:
            return False
    return True


class VehicleControlSchema:
    """包装类，便于导入使用"""
    validate = staticmethod(validate)
    SCHEMA = SCHEMA
