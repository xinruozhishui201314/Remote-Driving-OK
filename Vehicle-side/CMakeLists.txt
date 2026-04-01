cmake_minimum_required(VERSION 3.16)
project(VehicleSide VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 依赖安装前缀
# Dockerfile.dev 将 paho.mqtt.c / paho.mqtt.cpp / googletest
# 构建并安装到 deps/install/；CMake 从此处找到它们
# ==========================================
set(DEPS_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deps/install" CACHE PATH "deps/install prefix")

# ==========================================
# 依赖项获取
# ==========================================

# fmt (header-only library for formatting)
set(FMT_DIR "${CMAKE_SOURCE_DIR}/../deps/fmt")
if(EXISTS "${FMT_DIR}/include/fmt/format.h")
    message(STATUS "Using fmt from: ${FMT_DIR}")
    set(FMT_INCLUDE_DIR "${FMT_DIR}/include")
else()
    message(FATAL_ERROR "fmt not found. Please ensure ../deps/fmt exists with include/fmt/format.h")
endif()

# ROS2 支持（如果可用）
find_package(ament_cmake QUIET)
if(ament_cmake_FOUND)
    message(STATUS "ROS2 detected, enabling ROS2 support")
    set(ENABLE_ROS2 ON)
    ament_package()
else()
    message(STATUS "ROS2 not found, building standalone")
    set(ENABLE_ROS2 OFF)
endif()

# MQTT 客户端库：从 deps/install 找 PahoMqttCpp
# Dockerfile.dev 中 paho.mqtt.cpp 执行了 make install 到 deps/install/
find_package(PahoMqttCpp QUIET)
if(PahoMqttCpp_FOUND)
    message(STATUS "Found PahoMqttCpp (${PahoMqttCpp_DIR})")
    set(ENABLE_MQTT_PAHO ON)
else()
    message(WARNING "PahoMqttCpp not found, MQTT features will be limited")
    set(ENABLE_MQTT_PAHO OFF)
endif()

# 引入 spdlog (结构化 JSON 日志)：先尝试系统/常见路径（Docker 内 libspdlog-dev 常见位置）
if(NOT spdlog_DIR)
  foreach(_path "/usr/lib/x86_64-linux-gnu/cmake/spdlog" "/usr/lib/cmake/spdlog" "/usr/lib64/cmake/spdlog" "/usr/local/lib/cmake/spdlog")
    if(EXISTS "${_path}/spdlogConfig.cmake")
      set(spdlog_DIR "${_path}" CACHE PATH "spdlog config")
      break()
    endif()
  endforeach()
endif()
find_package(spdlog REQUIRED)
if(spdlog_FOUND)
    message(STATUS "Found spdlog (system library)")
    set(ENABLE_SPDLOG ON)
else()
    message(FATAL_ERROR "spdlog library not found. Please install libspdlog-dev.")
endif()

# googletest：优先从 deps/install 使用 find_package（Dockerfile.dev 中已 make install）
# 其次用本地源码 add_subdirectory（宿主机开发），最后才 FetchContent
if(EXISTS "${DEPS_INSTALL_PREFIX}/lib/cmake/GTest/GTestConfig.cmake")
    message(STATUS "Using installed googletest: ${DEPS_INSTALL_PREFIX}")
    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "Found GTest via install: ${GTest_DIR}")
        set(GTEST_FROM_INSTALL ON)
    else()
        message(WARNING "GTest config exists but find_package failed; falling back to add_subdirectory")
        set(GTEST_FROM_INSTALL OFF)
    endif()
elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/googletest/CMakeLists.txt")
    message(STATUS "Using local googletest: ${CMAKE_SOURCE_DIR}/deps/googletest")
    add_subdirectory(${CMAKE_SOURCE_DIR}/deps/googletest ${CMAKE_BINARY_DIR}/googletest-build EXCLUDE_FROM_ALL)
    set(GTEST_FROM_INSTALL OFF)
else()
    message(WARNING "googletest not found locally; falling back to FetchContent (requires network)")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.12.1
    )
    FetchContent_MakeAvailable(googletest)
    set(GTEST_FROM_INSTALL OFF)
endif()

# cpp-httplib (header-only)
# 优先使用项目根目录的共享依赖 deps/
set(SHARED_DEPS_DIR "${CMAKE_SOURCE_DIR}/../deps")
set(LOCAL_DEPS_DIR "${CMAKE_SOURCE_DIR}/deps")

# 先尝试共享依赖
set(LOCAL_HTTPLIB_DIR "${SHARED_DEPS_DIR}/cpp-httplib")
if(EXISTS "${LOCAL_HTTPLIB_DIR}/httplib.h")
    message(STATUS "Using shared cpp-httplib: ${LOCAL_HTTPLIB_DIR}")
    set(httplib_SOURCE_DIR "${LOCAL_HTTPLIB_DIR}")
elseif(EXISTS "${LOCAL_DEPS_DIR}/cpp-httplib/httplib.h")
    # 回退到本地依赖
    message(STATUS "Using local cpp-httplib: ${LOCAL_DEPS_DIR}/cpp-httplib")
    set(httplib_SOURCE_DIR "${LOCAL_DEPS_DIR}/cpp-httplib")
else()
    message(FATAL_ERROR "未找到 cpp-httplib，请确保 ../deps/cpp-httplib 或 deps/cpp-httplib 存在（含 httplib.h）")
endif()

# nlohmann_json：优先使用共享依赖（纯头文件库，直接加 include 目录）
set(LOCAL_NLOHMANN_JSON_DIR "${SHARED_DEPS_DIR}/nlohmann_json")
if(EXISTS "${LOCAL_NLOHMANN_JSON_DIR}/include/nlohmann/json.hpp")
    message(STATUS "Using shared nlohmann_json: ${LOCAL_NLOHMANN_JSON_DIR}")
    set(nlohmann_json_INCLUDE_DIR "${LOCAL_NLOHMANN_JSON_DIR}/include")
elseif(EXISTS "${LOCAL_DEPS_DIR}/nlohmann_json/include/nlohmann/json.hpp")
    message(STATUS "Using local nlohmann_json: ${LOCAL_DEPS_DIR}/nlohmann_json")
    set(nlohmann_json_INCLUDE_DIR "${LOCAL_DEPS_DIR}/nlohmann_json/include")
else()
    message(FATAL_ERROR "未找到 nlohmann_json，请确保 ../deps/nlohmann_json 或 deps/nlohmann_json 存在（含 include/nlohmann/json.hpp）")
endif()

# PostgreSQL libpq（/ready DB 健康检查）：优先 FindPostgreSQL，否则 pkg-config
set(PostgreSQL_FOUND FALSE)
find_package(PostgreSQL QUIET)
if(NOT PostgreSQL_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBPQ libpq)
    if(LIBPQ_FOUND)
      set(PostgreSQL_INCLUDE_DIRS ${LIBPQ_INCLUDE_DIRS})
      set(PostgreSQL_LIBRARIES ${LIBPQ_LIBRARIES})
      set(PostgreSQL_FOUND TRUE)
    endif()
  endif()
endif()
if(NOT PostgreSQL_FOUND)
  message(FATAL_ERROR "PostgreSQL/libpq not found. Install libpq-dev (apt) or PostgreSQL development package.")
endif()

# libcurl（HTTP/WebSocket）
# CMake 3.24+ 提供 FindCURL 并定义 CURL::libcurl 导入目标
# CMake 3.22 需要手动从 curl 导入目标构造或使用 curl 导入目标
find_package(CURL QUIET)
if(CURL_FOUND)
    message(STATUS "Found CURL (${CURL_VERSION}): include=${CURL_INCLUDE_DIRS}")
    set(ENABLE_CURL ON)
    # CMake < 3.24: curl 库本身不导出导入目标，使用变量
    if(NOT TARGET CURL::libcurl)
        message(STATUS "CURL::libcurl not defined; using CURL::CURL from imported target")
        find_library(CURL_LIBRARY NAMES curl libcurl)
        if(CURL_LIBRARY)
            add_library(CURL::libcurl SHARED IMPORTED)
            set_target_properties(CURL::libcurl PROPERTIES
                IMPORTED_LOCATION "${CURL_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}"
            )
        endif()
    endif()
else()
    message(FATAL_ERROR "CURL library not found. Install libcurl4-openssl-dev.")
endif()

# ==========================================
# 源文件列表（新增 common/logger.cpp）
# ==========================================
set(SOURCES
    src/main.cpp
    src/vehicle_controller.cpp
    src/mqtt_handler.cpp
    src/control_protocol.cpp
    src/zlm_control_channel.cpp
    src/ros2_bridge.cpp
    src/whip_publisher.cpp
    src/vehicle_config.cpp
    src/chassis_data_generator.cpp
    src/common/logger.cpp
)

set(HEADERS
    src/vehicle_controller.h
    src/mqtt_handler.h
    src/control_protocol.h
    src/zlm_control_channel.h
    src/ros2_bridge.h
    src/whip_publisher.h
    src/vehicle_config.h
    src/chassis_data_generator.h
    src/common/logger.h
    src/common/error_code.h
    src/common/log_macros.h
)

# ==========================================
# 可执行文件与库链接
# ==========================================

# 创建可执行文件
add_executable(VehicleSide
    ${SOURCES}
    ${HEADERS}
)

# 设置包含目录
if(httplib_SOURCE_DIR)
    target_include_directories(VehicleSide PRIVATE ${httplib_SOURCE_DIR})
else()
    # 如果使用本地依赖项
    if(EXISTS "${CMAKE_SOURCE_DIR}/deps/cpp-httplib")
        target_include_directories(VehicleSide PRIVATE "${CMAKE_SOURCE_DIR}/deps/cpp-httplib")
    endif()
endif()

target_include_directories(VehicleSide PRIVATE
  ${PostgreSQL_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/common
  ${nlohmann_json_INCLUDE_DIR}
  ${FMT_INCLUDE_DIR}
)

# 链接 spdlog
if(TARGET spdlog::spdlog)
    target_link_libraries(VehicleSide PRIVATE spdlog::spdlog)
else()
    # 如果使用 find_package
    target_link_libraries(VehicleSide PRIVATE spdlog)
endif()

target_link_libraries(VehicleSide PRIVATE
    pthread
    stdc++fs
    CURL::libcurl
    ${PostgreSQL_LIBRARIES}
)

# ROS2 支持
if(ENABLE_ROS2)
    find_package(rclcpp REQUIRED)
    find_package(std_msgs REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        rclcpp::rclcpp
        std_msgs::std_msgs
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_ROS2)
endif()

# MQTT 支持
if(ENABLE_MQTT_PAHO)
    # PahoMqttCpp CMake target 已通过 find_package 导入，自动携带 include 路径
    # 直接使用 CMake target 链接（不要用裸库名 paho-mqttpp3）
    target_link_libraries(VehicleSide PRIVATE PahoMqttCpp::paho-mqttpp3)
    target_compile_definitions(VehicleSide PRIVATE ENABLE_MQTT_PAHO)
endif()

# ==========================================
# 单元测试 (可选)
# ==========================================
enable_testing()
if(BUILD_TESTING)
    if(GTEST_FROM_INSTALL AND GTest_FOUND)
        # googletest 已通过 make install 安装到 deps/install/，直接 link
        message(STATUS "Linking tests with installed GTest")
        add_executable(vehicle_tests tests/test_vehicle_controller.cpp)
        target_link_libraries(vehicle_tests PRIVATE
            ${PROJECT_NAME}
            GTest::gtest GTest::gtest_main
            pthread
        )
    elseif(TARGET gtest_main)
        # googletest 通过 add_subdirectory 引入
        message(STATUS "Linking tests with subdirectory GTest")
        add_executable(vehicle_tests tests/test_vehicle_controller.cpp)
        target_link_libraries(vehicle_tests PRIVATE
            ${PROJECT_NAME}
            gtest gtest_main
            pthread
        )
    else()
        message(WARNING "GTest not available; tests will not be built")
    endif()
    if(TARGET vehicle_tests)
        gtest_discover_tests(vehicle_tests)
    endif()
endif()

# ==========================================
# 安装规则
# ==========================================
# 修复：使用明确的 target 名称 VehicleSide，RUNTIME DESTINATION 须与 install(TARGETS) 同句
install(TARGETS VehicleSide RUNTIME DESTINATION bin)

# 输出信息
message(STATUS "========================================")
message(STATUS "Build Configuration:")
message(STATUS "  Type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "  ROS2 Support:   ${ENABLE_ROS2}")
message(STATUS "  MQTT Support:   ${ENABLE_MQTT_PAHO}")
message(STATUS "  Structured Log: ON (spdlog find_package)")
message(STATUS "  Unit Tests:     ${BUILD_TESTING}")
message(STATUS "  GTest Source:   ${GTEST_FROM_INSTALL} (installed=${GTest_FOUND}, subdir=$<TARGET_EXISTS:gtest_main>)")
message(STATUS "  PahoMqttCpp:    ${PahoMqttCpp_FOUND}")
message(STATUS "  CURL:          ${ENABLE_CURL}")
message(STATUS "========================================")
