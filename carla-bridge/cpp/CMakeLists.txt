# CARLA Bridge (C++)：订阅 MQTT vehicle/control，连接 CARLA，四路相机推流到 ZLM，发布 vehicle/status
# 依赖：Paho MQTT C++、可选 CARLA LibCarla（未设置 CARLA_ROOT 时仅构建 MQTT+推流框架，需配合 CARLA 容器内 Python 取帧或后续链接 LibCarla）
cmake_minimum_required(VERSION 3.16)
project(CarlaBridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PahoMqttCpp QUIET)
if(NOT PahoMqttCpp_FOUND)
  message(FATAL_ERROR "PahoMqttCpp required. Install libpaho-mqtt-dev and Paho MQTT C++ wrapper.")
endif()

set(CARLA_ROOT "$ENV{CARLA_ROOT}" CACHE PATH "CARLA repo root (optional; if set, link LibCarla)")
if(CARLA_ROOT AND EXISTS "${CARLA_ROOT}/LibCarla/source/carla/client/Client.h")
  set(ENABLE_LIBCARLA ON)
  set(CARLA_BUILD "${CARLA_ROOT}/Build" CACHE PATH "CARLA build dir")
  set(CARLA_INCLUDE "${CARLA_ROOT}/LibCarla/source")
  if(EXISTS "${CARLA_ROOT}/Build/libcarla_client.a")
    set(CARLA_LIB "${CARLA_ROOT}/Build/libcarla_client.a")
  else()
    set(CARLA_LIB "${CARLA_ROOT}/Build/LibCarla/cmake/client/libcarla_client.a")
  endif()
  if(NOT EXISTS "${CARLA_LIB}")
    message(WARNING "CARLA_ROOT set but libcarla_client.a not found at ${CARLA_LIB}; building without CARLA")
    set(ENABLE_LIBCARLA OFF)
  endif()
  find_package(Boost QUIET)
  if(ENABLE_LIBCARLA AND NOT Boost_FOUND)
    set(ENABLE_LIBCARLA OFF)
    message(WARNING "Boost not found; building without CARLA")
  endif()
else()
  set(ENABLE_LIBCARLA OFF)
endif()

add_executable(carla_bridge
  src/main.cpp
  src/carla_runner.cpp
  src/mqtt_bridge.cpp
  src/rtmp_pusher.cpp
  src/json_parse.cpp
)

target_include_directories(carla_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(carla_bridge PRIVATE ENABLE_MQTT_PAHO $<$<BOOL:${ENABLE_LIBCARLA}>:ENABLE_LIBCARLA>)
# 支持从源码安装的 Paho（PahoMqttCpp::paho-mqttpp3）与系统包（paho-mqttpp3）
if(TARGET PahoMqttCpp::paho-mqttpp3)
  target_link_libraries(carla_bridge PRIVATE PahoMqttCpp::paho-mqttpp3 pthread)
else()
  target_link_libraries(carla_bridge PRIVATE paho-mqttpp3 paho-mqtt3as pthread)
endif()

if(ENABLE_LIBCARLA)
  target_include_directories(carla_bridge PRIVATE ${CARLA_INCLUDE} ${Boost_INCLUDE_DIRS})
  target_link_libraries(carla_bridge PRIVATE ${CARLA_LIB} Boost::system)
endif()

install(TARGETS carla_bridge RUNTIME DESTINATION bin)
