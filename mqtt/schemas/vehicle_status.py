"""
MQTT vehicle/status 消息 Schema 定义与验证
"""

from typing import Dict, Any

SCHEMA: Dict[str, Any] = {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "$id": "urn:teleop:schemas:vehicle_status",
    "title": "MQTT vehicle/status message",
    "type": "object",
    "required": ["vin", "type", "timestamp"],
    "properties": {
        "vin": {"type": "string"},
        "type": {"type": "string", "const": "vehicle_status"},
        "timestamp": {"type": "integer"},
        "speed": {"type": "number", "minimum": 0.0},
        "battery": {"type": "number", "minimum": 0.0, "maximum": 100.0},
        "brake": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "throttle": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "steering": {"type": "number", "minimum": -1.0, "maximum": 1.0},
        "gear": {"type": "integer", "enum": [-1, 0, 1, 2, 3, 4]},
        "odometer": {"type": "number", "minimum": 0.0},
        "temperature": {"type": "number"},
        "voltage": {"type": "number"},
        "remote_control_enabled": {"type": "boolean"},
        "driving_mode": {"type": "string", "enum": ["远驾", "自驾"]},
        "streaming": {"type": "boolean"},
    },
    "additionalProperties": False,
}


def validate(msg: Any) -> bool:
    """验证消息是否符合 vehicle_status Schema。"""
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
    return True


class VehicleStatusSchema:
    validate = staticmethod(validate)
    SCHEMA = SCHEMA
