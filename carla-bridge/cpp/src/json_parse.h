#ifndef CARLA_BRIDGE_JSON_PARSE_H
#define CARLA_BRIDGE_JSON_PARSE_H

#include <string>

namespace carla_bridge {

struct ControlMessage {
  std::string type;
  std::string vin;
  bool enable = false;
  double steering = 0.0;
  double throttle = 0.0;
  double brake = 0.0;
  int gear = 1;
  /** type=="speed" 时有效，否则为 -1 */
  double ui_speed_kmh = -1.0;
};

bool parseControlMessage(const std::string& payload, ControlMessage& out);

}  // namespace carla_bridge

#endif
