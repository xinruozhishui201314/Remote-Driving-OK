# 用于在镜像内已安装 Paho MQTT C 到 /usr/local 时，让 paho.mqtt.cpp 的 find_package(PahoMqttC) 直接通过。
# 用法：cmake -DCMAKE_MODULE_PATH=/path/to/deps ..
set(PAHO_MQTT_C_INCLUDE_DIRS "/usr/local/include")
set(PAHO_MQTT_C_LIBRARIES "/usr/local/lib/libpaho-mqtt3a.so")
include_directories(${PAHO_MQTT_C_INCLUDE_DIRS})
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PahoMqttC DEFAULT_MSG PAHO_MQTT_C_LIBRARIES PAHO_MQTT_C_INCLUDE_DIRS)
if(PahoMqttC_FOUND AND NOT TARGET PahoMqttC::PahoMqttC)
  add_library(PahoMqttC::PahoMqttC SHARED IMPORTED)
  set_target_properties(PahoMqttC::PahoMqttC PROPERTIES
    IMPORTED_LOCATION "${PAHO_MQTT_C_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${PAHO_MQTT_C_INCLUDE_DIRS}"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C")
endif()
