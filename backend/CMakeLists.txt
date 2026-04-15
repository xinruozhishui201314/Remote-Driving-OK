cmake_minimum_required(VERSION 3.10)
project(RemoteDrivingBackend)

set(CMAKE_CXX_STANDARD 17)

# 修复路径：根据日志中的文件复制操作（COPY deps/ ./deps），
# Docker 构建上下文为 backend 目录，deps 被复制到容器内的 /app/deps。
# CMAKE_SOURCE_DIR 为 /app，因此查找路径应为 deps（当前目录）而非 ../deps（上一级目录）。
set(CPP_HTTPLIB_DIR ${CMAKE_SOURCE_DIR}/deps/cpp-httplib)

if(EXISTS ${CPP_HTTPLIB_DIR}/httplib.h)
    message(STATUS "Found local cpp-httplib at ${CPP_HTTPLIB_DIR}")
    include_directories(${CPP_HTTPLIB_DIR})
else()
    message(FATAL_ERROR "未找到本地 cpp-httplib，请确保 backend/deps/cpp-httplib 存在（含 httplib.h）。不再从网络自动下载。")
endif()

# 修复：添加 nlohmann/json 依赖路径
# 优化查找逻辑，优先检查标准 include 目录结构
set(NLOHMANN_JSON_DIR ${CMAKE_SOURCE_DIR}/deps/nlohmann_json)

# 1. 检查标准仓库结构 include/nlohmann/json.hpp
set(NLOHMANN_INCLUDE_FOUND FALSE)
if(EXISTS ${NLOHMANN_JSON_DIR}/include/nlohmann/json.hpp)
    set(NLOHMANN_INCLUDE_DIR ${NLOHMANN_JSON_DIR}/include)
    set(NLOHMANN_INCLUDE_FOUND TRUE)
    message(STATUS "Found nlohmann/json (include) at ${NLOHMANN_INCLUDE_DIR}")
endif()

# 2. 检查扁平化结构 nlohmann/json.hpp
if(NOT NLOHMANN_INCLUDE_FOUND)
    if(EXISTS ${NLOHMANN_JSON_DIR}/nlohmann/json.hpp)
        set(NLOHMANN_INCLUDE_DIR ${NLOHMANN_JSON_DIR})
        set(NLOHMANN_INCLUDE_FOUND TRUE)
        message(STATUS "Found nlohmann/json (flat) at ${NLOHMANN_INCLUDE_DIR}")
    endif()
endif()

# 3. 检查单文件结构 json.hpp
if(NOT NLOHMANN_INCLUDE_FOUND)
    if(EXISTS ${NLOHMANN_JSON_DIR}/json.hpp)
        set(NLOHMANN_INCLUDE_DIR ${NLOHMANN_JSON_DIR})
        set(NLOHMANN_INCLUDE_FOUND TRUE)
        message(STATUS "Found nlohmann/json (single file) at ${NLOHMANN_INCLUDE_DIR}")
    endif()
endif()

# 如果找到目录，添加包含路径；如果都没找到，仍然添加目录以防报错更清晰
if(NLOHMANN_INCLUDE_FOUND)
    include_directories(${NLOHMANN_INCLUDE_DIR})
else()
    # 如果结构未知，尝试包含根目录和 include 子目录
    include_directories(${NLOHMANN_JSON_DIR})
    include_directories(${NLOHMANN_JSON_DIR}/include)
    message(WARNING "nlohmann/json header structure check failed, adding path anyway: ${NLOHMANN_JSON_DIR}")
endif()

# PostgreSQL (libpq)，编译/运行 backend 需 libpq-dev
find_package(PostgreSQL REQUIRED)
if(PostgreSQL_INCLUDE_DIRS)
    set(PG_INC ${PostgreSQL_INCLUDE_DIRS})
else()
    set(PG_INC ${PostgreSQL_INCLUDE_DIR})
endif()
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/include)

# 与 main.cpp 实际依赖一致；勿 GLOB_RECURSE 全 src（api/vin_handler 等与头文件不同步会编译失败）
file(GLOB SOURCES
  "src/*.cpp"
  "src/auth/*.cpp"
  "src/middleware/*.cpp"
  "src/protocol/fault_code.cpp"
  "src/common/utils.cpp"
)
add_executable(backend ${SOURCES})
target_include_directories(backend PRIVATE ${PG_INC})
# 使用变量形式链接，兼容各版本 FindPostgreSQL（避免 PRIVATE 与空变量导致的 signature 报错）
target_link_libraries(backend ${PostgreSQL_LIBRARIES})

if(UNIX)
    target_link_libraries(backend pthread)
endif()
