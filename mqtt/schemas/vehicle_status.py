"""
MQTT vehicle/status：以 vehicle_status.json 为真源（oneOf：chassis / ack / offline 等）。
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict

_SCHEMA_PATH = Path(__file__).resolve().parent / "vehicle_status.json"
with _SCHEMA_PATH.open(encoding="utf-8") as _f:
    SCHEMA: Dict[str, Any] = json.load(_f)

try:
    from jsonschema import Draft7Validator

    _VALIDATOR = Draft7Validator(SCHEMA)
except ImportError:  # pragma: no cover
    _VALIDATOR = None


def validate(msg: Any) -> bool:
    if not isinstance(msg, dict):
        return False
    if _VALIDATOR is None:
        return True
    return not any(_VALIDATOR.iter_errors(msg))


class VehicleStatusSchema:
    validate = staticmethod(validate)
    SCHEMA = SCHEMA
