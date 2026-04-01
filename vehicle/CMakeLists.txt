cmake_minimum_required(VERSION 3.10)
project(VehicleSide)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_SOURCE_DIR}/src)

file(GLOB SOURCES "src/*.cpp")
add_executable(vehicle ${SOURCES})

# 修复语法错误：标准的 install 命令格式为 install(TARGETS ... DESTINATION ...)
# 原日志显示缺少左括号或参数顺序错误。
install(TARGETS vehicle DESTINATION bin)
