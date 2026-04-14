#include "json_parse.h"
#include <regex>

namespace carla_bridge {

static bool extractString(const std::string& json, const char* key, std::string& out) {
  std::string pattern = std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"";
  std::regex re(pattern);
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) {
    out = m[1].str();
    return true;
  }
  return false;
}

static bool extractBool(const std::string& json, const char* key, bool& out) {
  std::string pattern = std::string("\"") + key + "\"\\s*:\\s*(true|false)";
  std::regex re(pattern);
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) {
    out = (m[1].str() == "true");
    return true;
  }
  return false;
}

static bool extractNumber(const std::string& json, const char* key, double& out) {
  std::string pattern = std::string("\"") + key + "\"\\s*:\\s*([+-]?[0-9]*\\.?[0-9]+(?:[eE][+-]?[0-9]+)?)";
  std::regex re(pattern);
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) {
    try {
      out = std::stod(m[1].str());
      return true;
    } catch (...) {}
  }
  return false;
}

static bool extractInt(const std::string& json, const char* key, int& out) {
  std::string pattern = std::string("\"") + key + "\"\\s*:\\s*([+-]?[0-9]+)";
  std::regex re(pattern);
  std::smatch m;
  if (std::regex_search(json, m, re) && m.size() > 1) {
    try {
      out = std::stoi(m[1].str());
      return true;
    } catch (...) {}
  }
  return false;
}

bool parseControlMessage(const std::string& payload, ControlMessage& out) {
  out = ControlMessage();
  if (!extractString(payload, "type", out.type)) return false;
  extractString(payload, "vin", out.vin);
  extractBool(payload, "enable", out.enable);
  extractNumber(payload, "steering", out.steering);
  extractNumber(payload, "throttle", out.throttle);
  extractNumber(payload, "brake", out.brake);
  extractInt(payload, "gear", out.gear);
  if (out.type == "speed") {
    double v = 0.0;
    if (extractNumber(payload, "value", v)) {
      out.ui_speed_kmh = v;
    }
  }
  return true;
}

}  // namespace carla_bridge
