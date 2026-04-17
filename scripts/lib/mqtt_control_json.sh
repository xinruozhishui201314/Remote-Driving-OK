#!/usr/bin/env bash
# Canonical JSON payloads for topic vehicle/control (see mqtt/schemas/vehicle_control.json).
# Source from repo scripts after SCRIPT_DIR is set:
#   # shellcheck source=lib/mqtt_control_json.sh
#   source "$SCRIPT_DIR/lib/mqtt_control_json.sh"

MQTT_CTRL_SCHEMA_VERSION="${MQTT_CTRL_SCHEMA_VERSION:-1.2.0}"
MQTT_STATUS_SCHEMA_VERSION="${MQTT_STATUS_SCHEMA_VERSION:-1.2.0}"

_mqtt_ctrl_timestamp_ms() {
  echo "$(($(date +%s) * 1000 + RANDOM % 1000))"
}

# Note: helpers are often invoked inside "$(...)" (subshell). seq must not rely on a global
# counter; reuse the same ms value for seq and timestampMs (schema allows any integer >= 0).

mqtt_json_start_stream() {
  local vin="$1"
  local ts
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"start_stream","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts"
}

mqtt_json_stop_stream() {
  local vin="$1"
  local ts
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"stop_stream","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts"
}

mqtt_json_remote_control() {
  local vin="$1" enable="$2"
  local en_json ts
  if [ "$enable" = "true" ] || [ "$enable" = "1" ]; then
    en_json="true"
  else
    en_json="false"
  fi
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"remote_control","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"enable":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" "$en_json"
}

mqtt_json_drive() {
  local vin="$1" steering="$2" throttle="$3" brake="$4" gear="$5" emergency_stop="$6"
  local es_json ts
  if [ "$emergency_stop" = "true" ] || [ "$emergency_stop" = "1" ]; then
    es_json="true"
  else
    es_json="false"
  fi
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"drive","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"steering":%s,"throttle":%s,"brake":%s,"gear":%s,"emergency_stop":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" \
    "$steering" "$throttle" "$brake" "$gear" "$es_json"
}

mqtt_json_gear() {
  local vin="$1" value="$2"
  local ts
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"gear","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"value":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" "$value"
}

mqtt_json_sweep() {
  local vin="$1" sweep_type="$2" active="$3"
  local act_json ts
  if [ "$active" = "true" ] || [ "$active" = "1" ]; then
    act_json="true"
  else
    act_json="false"
  fi
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"sweep","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"sweepType":"%s","active":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" "$sweep_type" "$act_json"
}

mqtt_json_target_speed() {
  local vin="$1" value="$2"
  local ts
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"target_speed","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"value":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" "$value"
}

mqtt_json_brake() {
  local vin="$1" value="$2"
  local ts
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"brake","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"value":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" "$value"
}

mqtt_json_emergency_stop() {
  local vin="$1" enable="$2"
  local en_json ts
  if [ "$enable" = "true" ] || [ "$enable" = "1" ]; then
    en_json="true"
  else
    en_json="false"
  fi
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '{"type":"emergency_stop","vin":"%s","schemaVersion":"%s","seq":%s,"timestampMs":%s,"enable":%s}' \
    "$vin" "$MQTT_CTRL_SCHEMA_VERSION" "$ts" "$ts" "$en_json"
}

# Sample chassis row for vehicle/status (uses timestamp + schemaVersion, not timestampMs)
mqtt_json_vehicle_status_chassis_sample() {
  local vin="$1"
  local ts
  ts="$(_mqtt_ctrl_timestamp_ms)"
  printf '%s' '{"type":"vehicle_status","vin":"'"$vin"'","schemaVersion":"'"$MQTT_STATUS_SCHEMA_VERSION"'","timestamp":'"$ts"',"speed":25.5,"gear":1,"steering":0.2,"throttle":0.3,"brake":0.0,"battery":95.5,"odometer":1234.56,"voltage":48.2,"current":15.3,"temperature":28.5,"remote_control_enabled":false,"driving_mode":"自驾","streaming":false}'
}
