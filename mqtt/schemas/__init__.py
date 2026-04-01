"""
MQTT 消息 Schema 定义与验证
"""

from .vehicle_control import VehicleControlSchema
from .vehicle_status import VehicleStatusSchema

__all__ = ["VehicleControlSchema", "VehicleStatusSchema"]
