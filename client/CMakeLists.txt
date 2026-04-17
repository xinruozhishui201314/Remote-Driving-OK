cmake_minimum_required(VERSION 3.21)
project(RemoteDrivingClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 严格编译规范 (2025/2026 远程驾驶规范) ───────────────────────────────────
# 禁用异常以确保确定性执行路径
add_compile_options(-fno-exceptions)
# 开启最高级别警告并视警告为错误
add_compile_options(-Wall -Wextra -Werror -Wpedantic -Weffc++)
# 禁用 RTTI (可选，视需要而定，通常与 no-exceptions 配合)
# add_compile_options(-fno-rtti)

# 性能优化编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
    if(NOT APPLE)
        add_compile_options(-flto)
    endif()
endif()

# ── OpenGL（解决 WrapOpenGL 依赖） ──────────────────────────────────────────────
find_package(OpenGL QUIET)
if(OpenGL_FOUND)
    if(NOT TARGET WrapOpenGL::WrapOpenGL)
        add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
        if(TARGET OpenGL::GL)
            target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE OpenGL::GL)
        else()
            find_library(OPENGL_LIBRARY GL)
            if(OPENGL_LIBRARY)
                target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE ${OPENGL_LIBRARY})
            endif()
        endif()
    endif()
    set(WrapOpenGL_FOUND TRUE)
else()
    message(WARNING "OpenGL not found, creating dummy WrapOpenGL target")
    if(NOT TARGET WrapOpenGL::WrapOpenGL)
        add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
    endif()
    set(WrapOpenGL_FOUND TRUE)
endif()

# ── XKB（Qt6Gui 依赖，Windows/macOS 跳过）───────────────────────────────────
# libxkbcommon-dev 提供 CMake 目标 XKB::XKB；若不存在则用 pkg-config 回退创建
find_package(XKB QUIET)
if(NOT XKB_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XKB_PC QUIET libxkbcommon)
        if(XKB_PC_FOUND)
            add_library(XKB::XKB INTERFACE IMPORTED)
            set_target_properties(XKB::XKB PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${XKB_PC_INCLUDE_DIRS}")
            set_target_properties(XKB::XKB PROPERTIES INTERFACE_LINK_LIBRARIES "${XKB_PC_LIBRARIES}")
            set(XKB_FOUND TRUE)
            message(STATUS "XKB: created XKB::XKB via pkg-config (libxkbcommon-dev recommended)")
        endif()
    endif()
endif()
if(NOT XKB_FOUND)
    message(WARNING "XKB not found — install libxkbcommon-dev for full Qt6 GUI support")
endif()

# ── Qt6 必需模块 ─────────────────────────────────────────────────────────────
# Multimedia + MultimediaQuick：QVideoSink / QML VideoOutput 官方显示路径（替代自研 VideoRenderer）
# 部分 Qt 6.8 发行版 / aqt 镜像仅有 libQt6MultimediaQuick.so + 头文件，但缺少 Qt6MultimediaQuickConfig.cmake
# （只带 Qt6MultimediaQuickPrivate）。此时用 IMPORTED 目标补齐，行为与官方包一致。
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui Quick Qml Multimedia Concurrent)
find_package(Qt6 QUIET COMPONENTS MultimediaQuick)
if(NOT TARGET Qt6::MultimediaQuick)
    get_filename_component(_QT6_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_library(_QT6_MULTIMEDIAQUICK_LIB
        NAMES Qt6MultimediaQuick
        HINTS "${_QT6_PREFIX}/lib"
        NO_DEFAULT_PATH)
    if(_QT6_MULTIMEDIAQUICK_LIB AND EXISTS "${_QT6_PREFIX}/include/QtMultimediaQuick")
        add_library(Qt6::MultimediaQuick SHARED IMPORTED)
        set_target_properties(Qt6::MultimediaQuick PROPERTIES
            IMPORTED_LOCATION "${_QT6_MULTIMEDIAQUICK_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${_QT6_PREFIX}/include/QtMultimediaQuick;${_QT6_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES "Qt6::Multimedia;Qt6::Quick;Qt6::OpenGL")
        message(STATUS "Qt6::MultimediaQuick: IMPORTED fallback (no Qt6MultimediaQuickConfig.cmake)")
    else()
        message(FATAL_ERROR
            "Qt6 MultimediaQuick required for QML video (QVideoSink / VideoOutput). "
            "Install Qt qtmultimedia with Quick QML support, or use a full Qt build.")
    endif()
    unset(_QT6_MULTIMEDIAQUICK_LIB CACHE)
endif()
unset(_QT6_PREFIX)

# ── Qt6 可选模块 ─────────────────────────────────────────────────────────────
foreach(_mod WebSockets QuickControls2 ShaderTools OpenGL Test)
    find_package(Qt6 QUIET COMPONENTS ${_mod})
    if(Qt6${_mod}_FOUND)
        message(STATUS "Found Qt6${_mod}")
        set(ENABLE_QT6_${_mod} ON)
    else()
        message(WARNING "Qt6${_mod} not found")
        set(ENABLE_QT6_${_mod} OFF)
    endif()
endforeach()

# 主路视频：Qt Multimedia（QVideoSink + VideoOutput）。可选：EGL DMA-BUF + ShaderTools 时编译 NV12
# Scene Graph 材质（DMA-BUF→EGLImage→GL，零 CPU YUV 转换）。
if(ENABLE_QT6_ShaderTools)
    message(STATUS "Video: Qt ShaderTools available — optional NV12 DMA-BUF qsb bundle enabled when EGL DMA-BUF=ON")
else()
    message(STATUS "Video: Qt Multimedia QVideoSink + VideoOutput（无 ShaderTools 则无 DMA-BUF SG qsb）")
endif()

# ── PkgConfig / FFmpeg / VA-API / EGL / NVDEC ───────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavutil libswscale libavformat)
    pkg_check_modules(VAAPI  QUIET libva libva-drm)
    pkg_check_modules(EGL    QUIET egl)
    pkg_check_modules(DRM    QUIET libdrm)
endif()

if(FFMPEG_FOUND)
    message(STATUS "Found FFmpeg: ${FFMPEG_VERSION}")
    set(ENABLE_FFMPEG ON)
else()
    message(WARNING "FFmpeg not found; software decode disabled")
    set(ENABLE_FFMPEG OFF)
endif()

if(VAAPI_FOUND)
    message(STATUS "Found VA-API")
    set(ENABLE_VAAPI ON)
else()
    set(ENABLE_VAAPI OFF)
endif()

# EGL DMA-BUF 零拷贝（需要 EGL + DRM + libavutil hwcontext_drm 支持）
option(ENABLE_EGL_DMABUF "Enable EGL DMA-BUF zero-copy path" OFF)
if(ENABLE_EGL_DMABUF AND EGL_FOUND AND DRM_FOUND AND FFMPEG_FOUND AND UNIX AND NOT APPLE)
    message(STATUS "Found EGL + DRM — enabling EGL DMA-BUF zero-copy interop")
else()
    set(ENABLE_EGL_DMABUF OFF)
endif()

# NVDEC（NVIDIA GPU 硬件解码，需 FFmpeg CUDA hwaccel）
# 运行期 DecoderFactory 会自动探测：VA-API → NVDEC → FFmpeg CPU（软解）
# 方案C：同时编译 VAAPI 和 NVDEC 支持，运行时自动选择可用的硬解器
# 推荐：cmake -DENABLE_VAAPI=ON -DENABLE_NVDEC=ON .. （或 -DENABLE_VAAPI_NVDEC_ALL=ON）
option(ENABLE_NVDEC "Enable NVIDIA NVDEC hardware decode via FFmpeg CUDA" OFF)
option(ENABLE_VAAPI_NVDEC_ALL "Enable both VA-API and NVDEC for maximum hardware support" OFF)

if(ENABLE_VAAPI_NVDEC_ALL)
    # 方案C：一键启用所有硬解
    set(ENABLE_VAAPI ON)
    set(ENABLE_NVDEC ON)
    message(STATUS "ENABLE_VAAPI_NVDEC_ALL: enabling both VA-API and NVDEC")
endif()

if(ENABLE_NVDEC AND FFMPEG_FOUND)
    message(STATUS "NVDEC enabled (NVIDIA hardware decode)")
else()
    set(ENABLE_NVDEC OFF)
endif()

# ── libdatachannel (WebRTC) ──────────────────────────────────────────────────
find_package(LibDataChannel QUIET)
if(NOT LibDataChannel_FOUND)
    find_package(libdatachannel QUIET)
endif()
if(LibDataChannel_FOUND OR libdatachannel_FOUND)
    message(STATUS "Found libdatachannel")
    set(ENABLE_WEBRTC_LIBDATACHANNEL ON)
else()
    message(WARNING "libdatachannel not found; WebRTC features limited")
    set(ENABLE_WEBRTC_LIBDATACHANNEL OFF)
endif()

# ── Paho MQTT C++ ────────────────────────────────────────────────────────────
find_package(PahoMqttCpp QUIET)
if(PahoMqttCpp_FOUND)
    message(STATUS "Found PahoMqttCpp")
    set(ENABLE_MQTT_PAHO ON)
else()
    message(WARNING "PahoMqttCpp not found; MQTT limited")
    set(ENABLE_MQTT_PAHO OFF)
endif()

# ── FlatBuffers (契约优先设计) ──────────────────────────────────────────────────
# --- FlatBuffers (Phase 1) ---
find_program(FLATBUFFERS_FLATC_EXECUTABLE flatc REQUIRED)
find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h REQUIRED)
find_library(FLATBUFFERS_LIBRARY flatbuffers REQUIRED)

function(compile_flatbuffers_schema SCHEMA_FILE OUTPUT_VAR)
    get_filename_component(SCHEMA_NAME ${SCHEMA_FILE} NAME_WE)
    set(HEADER "${CMAKE_CURRENT_BINARY_DIR}/${SCHEMA_NAME}_generated.h")
    add_custom_command(
        OUTPUT "${HEADER}"
        COMMAND ${FLATBUFFERS_FLATC_EXECUTABLE} --cpp --gen-object-api --no-includes --gen-compare 
                --no-prefix -o "${CMAKE_CURRENT_BINARY_DIR}" "${SCHEMA_FILE}"
        DEPENDS "${SCHEMA_FILE}"
        COMMENT "Generating FlatBuffers header for ${SCHEMA_FILE}"
    )
    set(${OUTPUT_VAR} ${HEADER} PARENT_SCOPE)
endfunction()

compile_flatbuffers_schema("${CMAKE_CURRENT_SOURCE_DIR}/../backend/src/protocol/message_types.fbs" PROTOCOL_FB_HEADERS)

# ── Boost.SML (状态机驱动架构) ──────────────────────────────────────────────
# 使用本地源码 (deps/sml)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../deps/sml" "${CMAKE_CURRENT_BINARY_DIR}/deps/sml" EXCLUDE_FROM_ALL)

# ── Google Benchmark (optional) ───────────────────────────────────────────────
option(BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(benchmark_FOUND)
        set(ENABLE_BENCHMARKS ON)
    else()
        message(WARNING "Google Benchmark not found; benchmarks disabled")
        set(ENABLE_BENCHMARKS OFF)
    endif()
endif()

qt_standard_project_setup()

# ─────────────────────────────────────────────────────────────────────────────
# Sources
# ─────────────────────────────────────────────────────────────────────────────
set(SOURCES
    # Entry point + 启动拆分（日志 / QML 注册 / 上下文）
    src/main.cpp
    src/utils/WebRtcUrlResolve.cpp
    src/utils/MqttControlEnvelope.cpp
    src/utils/WebRtcTransportDispatch.cpp
    src/app/client_logging_setup.cpp
    src/app/client_crash_diagnostics.cpp
    src/app/client_present_composition_diag.cpp
    src/app/client_present_health_auto_env.cpp
    src/app/client_x11_deep_diag.cpp
    src/app/client_x11_visual_probe.cpp
    src/app/client_system_stall_diag.cpp
    src/app/client_perf_diag_env.cpp
    src/app/client_app_bootstrap.cpp
    src/app/client_window_frame_policy.cpp
    src/app/client_platform_gate.cpp
    src/app/client_startup_tcp_gate.cpp
    src/app/client_startup_readiness_gate.cpp
    src/app/client_display_runtime_policy.cpp

    src/ui/RemoteVideoSurface.cpp
    src/ui/video_integrity_banner_bridge.cpp
    src/ui/VideoFramePresentWorker.cpp
    src/media/ClientVideoDiagCache.cpp
    src/media/VideoFrameFingerprintCache.cpp
    src/core/BlackBoxService.cpp
    src/core/TimeSyncService.cpp

    # Legacy network/media (kept as WebRTCChannel internals)
    src/webrtcclient.cpp
    src/webrtcstreammanager.cpp
    src/mqttcontroller.cpp

    # Domain models
    src/vehiclestatus.cpp
    src/authmanager.cpp
    src/vehiclemanager.cpp
    src/nodehealthchecker.cpp

    # ── Core Framework Layer ────────────────────────────────────────────────
    src/core/eventbus.cpp
    src/core/systemstatemachine.cpp
    src/core/networkqualityaggregator.cpp
    src/core/threadpool.cpp
    src/core/configuration.cpp
    src/core/logger.cpp
    src/core/performancemonitor.cpp
    src/core/antireplayguard.cpp
    src/core/commandsigner.cpp
    src/core/memorymanager.cpp
    src/core/plugincontext.cpp
    src/core/pluginmanager.cpp
    # 产品化新增
    src/core/healthchecker.cpp
    src/core/metricscollector.cpp
    src/core/errorregistry.cpp
    src/core/faultmanager.cpp
    src/core/modelbase.cpp
    src/core/tracing.cpp
    src/core/httpendpointserver.cpp

    # 安全模块（Phase2 Task 8: JWT 安全存储）
    src/security/keystoremanager.cpp

    # ── Infrastructure Layer ─────────────────────────────────────────────────
    # Network
    src/infrastructure/itransportmanager.cpp
    src/infrastructure/mqtttransportadapter.cpp
    src/infrastructure/controlloopticker.cpp
    # API 限流器（Phase2 Task 9）
    src/infrastructure/network/ratelimiter.cpp
    src/infrastructure/network/TransportAggregator.cpp
    src/adapters/network/WebRTCChannel.cpp
    src/adapters/network/UDPChannel.cpp
    src/infrastructure/network/AdaptiveBitrate.cpp
    src/infrastructure/network/FECEncoder.cpp

    # Media
    src/infrastructure/media/DecoderFactory.cpp
    src/infrastructure/media/FramePool.cpp
    src/infrastructure/media/MediaPipeline.cpp
    # Hardware input
    src/infrastructure/hardware/InputSampler.cpp
    src/adapters/hardware/KeyboardMouseInput.cpp

    # ── Application Service Layer ────────────────────────────────────────────
    src/services/sessionmanager.cpp
    src/services/httpnetworkhelpers.cpp
    src/services/vehicle_api_parsers.cpp
    src/services/vehiclecatalogclient.cpp
    src/services/remotesessionclient.cpp
    src/services/vehiclecontrolservice.cpp
    src/services/safetymonitorservice.cpp
    src/services/degradationmanager.cpp
    src/services/latencycompensator.cpp
    src/services/errorrecoverymanager.cpp
    src/services/diagnosticsservice.cpp
    ../backend/src/protocol/fault_code.cpp

    # ── Presentation Layer ───────────────────────────────────────────────────
    src/presentation/models/TelemetryModel.cpp
    src/presentation/models/NetworkStatusModel.cpp
    src/presentation/models/SafetyStatusModel.cpp
)

set(HEADERS
    src/app/client_logging_setup.h
    src/app/client_crash_diagnostics.h
    src/app/client_present_composition_diag.h
    src/app/client_present_health_auto_env.h
    src/app/client_x11_deep_diag.h
    src/app/client_x11_visual_probe.h
    src/app/client_system_stall_diag.h
    src/app/client_perf_diag_env.h
    src/app/client_app_bootstrap.h
    src/app/client_display_runtime_policy.h
    src/ui/RemoteVideoSurface.h
    src/ui/VideoFramePresentWorker.h
    src/media/ClientVideoDiagCache.h
    src/media/CpuVideoRgba8888Frame.h
    src/media/VideoFrameEvidenceDiag.h
    src/core/BlackBoxService.h
    src/core/TimeSyncService.h
    src/webrtcclient.h
    src/webrtcstreammanager.h
    src/mqttcontroller.h
    src/vehiclestatus.h
    src/authmanager.h
    src/vehiclemanager.h
    src/nodehealthchecker.h

    src/utils/LockFreeQueue.h
    src/utils/TripleBuffer.h
    src/utils/CircularBuffer.h
    src/utils/PercentileStats.h
    src/utils/TimeUtils.h
    src/utils/WebRtcUrlResolve.h

    src/core/eventbus.h
    src/core/systemstatemachine.h
    src/core/networkqualityaggregator.h
    src/core/threadpool.h
    src/core/configuration.h
    src/core/logger.h
    src/core/performancemonitor.h
    src/core/iplugin.h
    src/core/plugincontext.h
    src/core/pluginmanager.h
    src/core/memorymanager.h
    src/core/antireplayguard.h
    src/core/commandsigner.h
    # 产品化新增
    src/core/healthchecker.h
    src/core/metricscollector.h
    src/core/errorregistry.h
    src/core/modelbase.h
    src/core/tracing.h
    src/core/httpendpointserver.h

    # 安全模块（Phase2 Task 8: JWT 安全存储）
    src/security/keystoremanager.h

    src/infrastructure/itransportmanager.h
    src/infrastructure/mqtttransportadapter.h
    src/infrastructure/controlloopticker.h
    # API 限流器（Phase2 Task 9）
    src/infrastructure/network/ratelimiter.h
    src/infrastructure/network/TransportAggregator.h
    src/adapters/network/WebRTCChannel.h
    src/adapters/network/UDPChannel.h
    src/infrastructure/network/AdaptiveBitrate.h
    src/infrastructure/network/FECEncoder.h

    src/infrastructure/media/IHardwareDecoder.h
    src/infrastructure/media/DecoderFactory.h
    src/infrastructure/media/FramePool.h
    src/infrastructure/media/MediaPipeline.h
    src/infrastructure/hardware/IInputDevice.h
    src/infrastructure/hardware/InputSampler.h
    src/adapters/hardware/KeyboardMouseInput.h

    src/services/sessionmanager.h
    src/services/httpnetworkhelpers.h
    src/services/vehicle_api_parsers.h
    src/services/vehiclecatalogclient.h
    src/services/remotesessionclient.h
    src/services/vehiclecontrolservice.h
    src/services/safetymonitorservice.h
    src/services/degradationmanager.h
    src/services/latencycompensator.h
    src/services/errorrecoverymanager.h
    src/services/diagnosticsservice.h

    src/presentation/models/TelemetryModel.h
    src/presentation/models/NetworkStatusModel.h
    src/presentation/models/SafetyStatusModel.h
)

# Optional: FFmpeg decoder
if(ENABLE_FFMPEG)
    list(APPEND SOURCES
        src/h264decoder.cpp
        src/adapters/media/FFmpegSoftDecoder.cpp
        src/media/ClientMediaBudget.cpp
        src/media/RtpPacketSpscQueue.cpp
        src/media/RtpStreamClockContext.cpp
        src/media/RtcpCompoundParser.cpp
        src/media/RtpTrueJitterBuffer.cpp
        src/media/H264ClientDiag.cpp
        src/media/H264WebRtcHwBridge.cpp
        src/media/ClientVideoStreamHealth.cpp
        src/media/DmaBufFrameHandle.cpp
        src/media/VideoInterlacedPolicy.cpp
        src/media/VideoSwsColorHelper.cpp
    )
    list(APPEND HEADERS
        src/h264decoder.h
        src/adapters/media/FFmpegSoftDecoder.h
        src/media/ClientMediaBudget.h
        src/media/RtpIngressTypes.h
        src/media/RtpPacketSpscQueue.h
        src/media/RtpStreamClockContext.h
        src/media/RtcpCompoundParser.h
        src/media/RtpTrueJitterBuffer.h
        src/media/H264ClientDiag.h
        src/media/H264WebRtcHwBridge.h
        src/media/ClientVideoStreamHealth.h
        src/media/DmaBufFrameHandle.h
        src/media/VideoInterlacedPolicy.h
        src/media/VideoSwsColorHelper.h
    )
endif()

if(ENABLE_FFMPEG AND ENABLE_EGL_DMABUF AND ENABLE_QT6_ShaderTools)
    list(APPEND SOURCES src/adapters/media/Nv12DmaBufQsg.cpp)
    list(APPEND HEADERS src/adapters/media/Nv12DmaBufQsg.h)
endif()

# Optional: VAAPI decoder
if(ENABLE_VAAPI)
    list(APPEND SOURCES src/adapters/media/VAAPIDecoder.cpp)
    list(APPEND HEADERS src/adapters/media/VAAPIDecoder.h)
endif()

# Optional: NVDEC decoder
if(ENABLE_NVDEC)
    list(APPEND SOURCES src/adapters/media/NvdecDecoder.cpp)
    list(APPEND HEADERS src/adapters/media/NvdecDecoder.h)
endif()

# ── Executable ────────────────────────────────────────────────────────────────
qt_add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${PROTOCOL_FB_HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend/src/protocol
    ${CMAKE_CURRENT_BINARY_DIR} # 用于查找生成的 FlatBuffers 头文件
)

# ── Link FlatBuffers ─────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE ${FLATBUFFERS_LIBRARY})

# ── Link Boost.SML ───────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PUBLIC sml::sml)

# ── Link Qt (required) ────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui
    Qt6::Multimedia Qt6::MultimediaQuick Qt6::Concurrent
)
target_compile_options(${PROJECT_NAME} PRIVATE -Wno-effc++)

# Linux：Qt 6 gcc_64 的 libQt6Multimedia 常带 DT_NEEDED(libpulse)，但 CMake 目标未传播，
# 最终链接 RemoteDrivingClient 会出现大量 undefined reference to pa_*。显式链接 libpulse。
if(UNIX AND NOT APPLE)
    find_library(CLIENT_LIBPULSE NAMES pulse DOC "PulseAudio client library (Qt6Multimedia)")
    if(NOT CLIENT_LIBPULSE)
        # 仅安装 libpulse0、无 libpulse.so 开发符号链接时，直接链接 SONAME
        # Debian/Ubuntu 常把 .so.0 放在 /lib/<multiarch>/（不在 /usr/lib/*/）
        file(GLOB _client_libpulse_candidates
            "/lib/*/libpulse.so.0"
            "/usr/lib/*/libpulse.so.0"
            "/usr/lib/libpulse.so.0"
            "/usr/lib64/libpulse.so.0")
        if(_client_libpulse_candidates)
            list(SORT _client_libpulse_candidates)
            list(GET _client_libpulse_candidates 0 CLIENT_LIBPULSE)
        endif()
        unset(_client_libpulse_candidates)
    endif()
    if(CLIENT_LIBPULSE)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${CLIENT_LIBPULSE})
        message(STATUS "client: linked libpulse for Qt6Multimedia: ${CLIENT_LIBPULSE}")
    else()
        message(WARNING
            "client: libpulse not found (install libpulse0 + libpulse-dev) — link may fail with pa_* undefined references")
    endif()
endif()

# Linux: libxcb — 用于闪退诊断（xcb_connection_has_error 轮询、X protocol error 事件日志）
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CLIENT_XCB QUIET xcb)
        if(CLIENT_XCB_FOUND)
            target_compile_definitions(${PROJECT_NAME} PRIVATE CLIENT_HAVE_XCB=1)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${CLIENT_XCB_LIBRARIES})
            target_include_directories(${PROJECT_NAME} PRIVATE ${CLIENT_XCB_INCLUDE_DIRS})
            message(STATUS "client: CLIENT_HAVE_XCB enabled (libxcb crash diagnostics)")
        else()
            message(STATUS "client: pkg-config 'xcb' not found — xcb native filter/poll disabled (optional)")
        endif()
    endif()
endif()

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<NOT:$<BOOL:${CMAKE_CROSSCOMPILING}>>:-rdynamic>)

# ── Link optional Qt modules ──────────────────────────────────────────────────
target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT6_MULTIMEDIA)
if(ENABLE_QT6_WebSockets)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::WebSockets)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT6_WEBSOCKETS)
endif()
if(ENABLE_QT6_QuickControls2)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::QuickControls2)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT6_QUICKCONTROLS2)
endif()
if(ENABLE_QT6_OpenGL)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::OpenGL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT6_OPENGL)
endif()
# ── Link FFmpeg ──────────────────────────────────────────────────────────────
if(ENABLE_FFMPEG)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_FFMPEG)
endif()

# ── Link VA-API ──────────────────────────────────────────────────────────────
if(ENABLE_VAAPI)
    target_include_directories(${PROJECT_NAME} PRIVATE ${VAAPI_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${VAAPI_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_VAAPI)
endif()

# ── Link EGL DMA-BUF ─────────────────────────────────────────────────────────
if(ENABLE_EGL_DMABUF)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${EGL_INCLUDE_DIRS} ${DRM_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${EGL_LIBRARIES} ${DRM_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_EGL_DMABUF)
    message(STATUS "EGL DMA-BUF: enabled (Intel/AMD zero-copy path)")
endif()

if(ENABLE_EGL_DMABUF AND ENABLE_QT6_ShaderTools)
    qt_add_shaders(${PROJECT_NAME} "nv12_dmabuf_shaders"
        GLSL_VERSION 440
        PREFIX "/client_dmabuf"
        FILES
            shaders/nv12_dmabuf.vert
            shaders/nv12_dmabuf.frag
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE CLIENT_HAVE_NV12_DMABUF_SG=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::ShaderTools)
    message(STATUS "CLIENT_HAVE_NV12_DMABUF_SG: NV12 DMA-BUF → Scene Graph (qsb in :/client_dmabuf/shaders/)")
endif()

# ── Link NVDEC ───────────────────────────────────────────────────────────────
if(ENABLE_NVDEC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_NVDEC)
    message(STATUS "NVDEC: enabled (NVIDIA hardware decode)")
endif()

# ── Link WebRTC (libdatachannel) ─────────────────────────────────────────────
if(ENABLE_WEBRTC_LIBDATACHANNEL)
    if(TARGET LibDataChannel::LibDataChannel)
        target_link_libraries(${PROJECT_NAME} PRIVATE LibDataChannel::LibDataChannel)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE datachannel)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_WEBRTC_LIBDATACHANNEL)
endif()

# ── Link MQTT ────────────────────────────────────────────────────────────────
if(ENABLE_MQTT_PAHO)
    # Paho MQTT C++ 通常依赖 C 库 (paho-mqtt3as)
    target_link_libraries(${PROJECT_NAME} PRIVATE paho-mqttpp3 paho-mqtt3as)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_MQTT_PAHO)
    # Paho MQTT C++ 依赖异常处理；尽管核心禁用了异常，但 Paho 接口必需启用
    target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions)
endif()

# ── Realtime scheduling (Linux) ───────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    # GCC：std::atomic<较大结构体> 可能生成 __atomic_load/store，需 -latomic
    target_link_libraries(${PROJECT_NAME} PRIVATE rt pthread atomic)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_REALTIME_SCHEDULING)
endif()

# ── QML path ─────────────────────────────────────────────────────────────────
set(QML_IMPORT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    QML_IMPORT_PATH="${QML_IMPORT_PATH}"
)

# qmltyperegistrar 生成物默认同步回 client/qml 供 qmllint / git；源码只读挂载（如部分 CI/Docker）请关闭：
#   cmake -DCLIENT_SYNC_GENERATED_QMLTYPES_TO_SOURCE=OFF ...
set(_CLIENT_QML_IS_WRITABLE OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_SOURCE_DIR}/qml/.cmake_qmltypes_sync_probe"
        RESULT_VARIABLE _CLIENT_QML_TOUCH_RC
        OUTPUT_QUIET ERROR_QUIET)
    if(_CLIENT_QML_TOUCH_RC EQUAL 0 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/.cmake_qmltypes_sync_probe")
        file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/qml/.cmake_qmltypes_sync_probe")
        set(_CLIENT_QML_IS_WRITABLE ON)
    endif()
endif()
set(_CLIENT_SYNC_QMLTYPES_HELP
    "POST_BUILD: copy generated .qmltypes into client/qml (disable on read-only source trees)")
if(_CLIENT_QML_IS_WRITABLE)
    option(CLIENT_SYNC_GENERATED_QMLTYPES_TO_SOURCE "${_CLIENT_SYNC_QMLTYPES_HELP}" ON)
else()
    set(CLIENT_SYNC_GENERATED_QMLTYPES_TO_SOURCE OFF CACHE BOOL "${_CLIENT_SYNC_QMLTYPES_HELP}" FORCE)
    message(STATUS "client/qml is not writable; CLIENT_SYNC_GENERATED_QMLTYPES_TO_SOURCE=OFF (skip sync; typical for Docker ro bind mounts)")
endif()

# C++ QML 类型：QML_ELEMENT + qmltyperegistrar 生成 remote-driving-cpp.qmltypes（qmldir typeinfo 引用）。
# OUTPUT_DIRECTORY 必须在可写目录：Qt 会在配置/生成阶段写入 *_qml_module_dir_map.qrc.tmp，指到源码树会在只读挂载下失败。
set(_REMOTEDRIVING_CPP_QML_STAGING "${CMAKE_CURRENT_BINARY_DIR}/RemoteDrivingCppQml-staging")
file(MAKE_DIRECTORY "${_REMOTEDRIVING_CPP_QML_STAGING}")
qt_add_qml_module(${PROJECT_NAME}
    URI RemoteDriving
    VERSION 1.0
    NO_RESOURCE_TARGET_PATH
    NO_GENERATE_QMLDIR
    OUTPUT_DIRECTORY "${_REMOTEDRIVING_CPP_QML_STAGING}"
    TYPEINFO remote-driving-cpp.qmltypes
    NO_LINT
    IMPORTS
        QtQuick
        QtQml
        QtMultimedia
)
set(_REMOTEDRIVING_CPP_TYPEINFO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/qml/remote-driving-cpp.qmltypes")
if(CLIENT_SYNC_GENERATED_QMLTYPES_TO_SOURCE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_REMOTEDRIVING_CPP_QML_STAGING}/remote-driving-cpp.qmltypes"
            "${_REMOTEDRIVING_CPP_TYPEINFO_SRC}"
        COMMENT "Sync remote-driving-cpp.qmltypes into source tree for version control / qmllint"
        VERBATIM)
endif()

# Facade 独立 URI：qmltyperegistrar 生成 driving-facade.qmltypes（提交到 qml/DrivingFacade/ 供 qmllint）。
# 静态 QML 模块不得把 OUTPUT_DIRECTORY 指到源码树（会写入 .a / 资源副本）；在构建目录放 staging qmldir（相对路径指向源码中的 DrivingInterface.qml）。
set(_DRIVING_FACADE_QML_STAGING "${CMAKE_CURRENT_BINARY_DIR}/DrivingFacadeQml-staging")
file(MAKE_DIRECTORY "${_DRIVING_FACADE_QML_STAGING}")
file(RELATIVE_PATH _DRIVING_FACADE_DI_REL
    "${_DRIVING_FACADE_QML_STAGING}"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/DrivingInterface.qml")
file(WRITE "${_DRIVING_FACADE_QML_STAGING}/qmldir"
    "module DrivingFacade\nDrivingInterface 1.0 ${_DRIVING_FACADE_DI_REL}\ntypeinfo driving-facade.qmltypes\n")
qt_add_library(DrivingFacadeQml STATIC)
qt_add_qml_module(DrivingFacadeQml
    URI DrivingFacade
    VERSION 1.0
    NO_GENERATE_QMLDIR
    OUTPUT_DIRECTORY "${_DRIVING_FACADE_QML_STAGING}"
    TYPEINFO driving-facade.qmltypes
    NO_LINT
    QML_FILES
        qml/DrivingInterface.qml
    IMPORTS
        QtQuick
        QtQml
        RemoteDriving
)
target_link_libraries(DrivingFacadeQml PRIVATE Qt6::Quick)
target_compile_options(DrivingFacadeQml PRIVATE -Wno-effc++)
target_link_libraries(${PROJECT_NAME} PRIVATE DrivingFacadeQmlplugin)
set(_DRIVING_FACADE_TYPEINFO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/qml/DrivingFacade/driving-facade.qmltypes")
if(CLIENT_SYNC_GENERATED_QMLTYPES_TO_SOURCE)
    add_custom_command(TARGET DrivingFacadeQml POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_DRIVING_FACADE_QML_STAGING}/driving-facade.qmltypes"
            "${_DRIVING_FACADE_TYPEINFO_SRC}"
        COMMENT "Sync driving-facade.qmltypes into source tree for version control / qmllint"
        VERBATIM)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# ── Tests ────────────────────────────────────────────────────────────────────
if(ENABLE_QT6_Test)
    enable_testing()
    include(CTest)

    # 测试覆盖率选项
    option(ENABLE_COVERAGE "Enable code coverage reporting with gcov" OFF)
    if(ENABLE_COVERAGE)
        find_program(GCOV_BIN gcov)
        find_program(LCOV_BIN lcov)
        find_program(GENHTML_BIN genhtml)
        if(NOT GCOV_BIN)
            message(WARNING "gcov not found, coverage disabled")
            set(ENABLE_COVERAGE OFF)
        endif()
    endif()

    # State machine tests
    add_executable(client_systemstatemachine_test
        tests/test_systemstatemachine.cpp
        src/core/eventbus.cpp
        src/core/systemstatemachine.cpp
        src/core/metricscollector.cpp
        src/core/configuration.cpp
    )
    target_include_directories(client_systemstatemachine_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/sml/include)
    target_link_libraries(client_systemstatemachine_test PRIVATE
        Qt6::Core Qt6::Test sml::sml)
    target_compile_options(client_systemstatemachine_test PRIVATE -Wno-effc++)
    add_test(NAME client_systemstatemachine_test
        COMMAND client_systemstatemachine_test)

    # Unit tests for services — 每个测试文件有 QTEST_MAIN，各自独立可执行
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
        set(_TEST_DEPS
            ${PROTOCOL_FB_HEADERS}
            src/core/eventbus.cpp
            src/core/systemstatemachine.cpp
            src/core/configuration.cpp
            src/core/logger.cpp
            src/core/threadpool.cpp
            src/core/networkqualityaggregator.cpp
            src/core/tracing.cpp
            src/core/metricscollector.cpp
            src/vehiclestatus.cpp
            src/nodehealthchecker.cpp
            src/core/errorregistry.cpp
            ../backend/src/protocol/fault_code.cpp
        )
        set(_TEST_INCLUDES 
            ${CMAKE_CURRENT_SOURCE_DIR}/src 
            ${CMAKE_CURRENT_SOURCE_DIR}/../backend/src/protocol
            ${CMAKE_CURRENT_SOURCE_DIR}/../deps/sml/include
            ${CMAKE_CURRENT_BINARY_DIR}
        )
        set(_TEST_LIBS Qt6::Core Qt6::Test Qt6::Network sml::sml)

        # 通用测试目标宏（避免重复代码）
        function(add_client_unit_test NAME TEST_FILE)
            add_executable(${NAME} ${TEST_FILE} ${_TEST_DEPS})
            target_include_directories(${NAME} PRIVATE ${_TEST_INCLUDES})
            target_compile_definitions(${NAME} PRIVATE REMOTE_DRIVING_UNIT_TEST)
            target_link_libraries(${NAME} PRIVATE ${_TEST_LIBS})
            set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
            if(ENABLE_COVERAGE)
                target_compile_options(${NAME} PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
                target_link_options(${NAME} PRIVATE -fprofile-arcs -ftest-coverage)
            endif()
            add_test(NAME ${NAME} COMMAND ${NAME})
        endfunction()

        add_client_unit_test(test_configuration   tests/unit/test_configuration.cpp)
        add_client_unit_test(test_threadpool       tests/unit/test_threadpool.cpp)
        add_client_unit_test(test_networkquality   tests/unit/test_networkqualityaggregator.cpp)
        add_client_unit_test(test_logger           tests/unit/test_logger.cpp)

        add_executable(test_vehiclecontrolservice
            tests/unit/test_vehiclecontrolservice.cpp
            ${_TEST_DEPS}
            src/utils/MqttControlEnvelope.cpp
            src/services/vehiclecontrolservice.cpp
            src/services/safetymonitorservice.cpp
            src/services/latencycompensator.cpp
            src/core/commandsigner.cpp
            src/vehiclemanager.cpp
            src/services/httpnetworkhelpers.cpp
            src/services/vehicle_api_parsers.cpp
            src/services/vehiclecatalogclient.cpp
            src/services/remotesessionclient.cpp
            src/infrastructure/itransportmanager.cpp
        )
        target_include_directories(test_vehiclecontrolservice PRIVATE ${_TEST_INCLUDES})
        target_compile_definitions(test_vehiclecontrolservice PRIVATE VEHICLE_CONTROL_SERVICE_UNIT_TEST REMOTE_DRIVING_UNIT_TEST)
        # vehiclecontrolservice → webrtcstreammanager.h → webrtcclient.h: QVideoSink (Multimedia), QImage (Gui), QQuickItem (Quick)
        target_link_libraries(test_vehiclecontrolservice PRIVATE ${_TEST_LIBS} Qt6::Gui Qt6::Quick Qt6::Multimedia)
        if(UNIX AND NOT APPLE)
            target_link_libraries(test_vehiclecontrolservice PRIVATE atomic)
        endif()
        set_target_properties(test_vehiclecontrolservice PROPERTIES AUTOMOC ON)
        add_test(NAME test_vehiclecontrolservice COMMAND test_vehiclecontrolservice)
        # 链接 Qt Gui/Quick/Multimedia 时默认会走 xcb；CI/Docker 无 DISPLAY 时必须 offscreen，否则进程 abort
        set_tests_properties(test_vehiclecontrolservice PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # SessionManager：异常路径与登录/选车/会话编排（链接与主程序一致，排除 main.cpp）
        set(_TEST_SESSION_MGR_SOURCES ${SOURCES})
        list(REMOVE_ITEM _TEST_SESSION_MGR_SOURCES "src/main.cpp")
        qt_add_executable(test_sessionmanager
            tests/unit/test_sessionmanager.cpp
            ${_TEST_SESSION_MGR_SOURCES}
            ${HEADERS}
        )
        target_include_directories(test_sessionmanager PRIVATE
            $<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(test_sessionmanager PRIVATE
            $<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>
        )
        target_compile_options(test_sessionmanager PRIVATE
            $<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_OPTIONS>
        )
        target_link_libraries(test_sessionmanager PRIVATE
            Qt6::Test
            $<TARGET_PROPERTY:${PROJECT_NAME},LINK_LIBRARIES>
        )
        target_link_options(test_sessionmanager PRIVATE
            $<TARGET_PROPERTY:${PROJECT_NAME},LINK_OPTIONS>
        )
        set_target_properties(test_sessionmanager PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_sessionmanager PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_sessionmanager PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_sessionmanager COMMAND test_sessionmanager)
        set_tests_properties(test_sessionmanager PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # ── 单文件单模块测试（仅链接被测 .cpp + Qt Test）────────────────────────
        function(add_client_isolated_module_test _name _testcpp)
            set(_extra_srcs ${ARGN})
            add_executable(${_name} ${_testcpp} ${_extra_srcs})
            target_include_directories(${_name} PRIVATE
                ${_TEST_INCLUDES}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
            )
            target_link_libraries(${_name} PRIVATE Qt6::Core Qt6::Test)
            if(ENABLE_COVERAGE)
                target_compile_options(${_name} PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
                target_link_options(${_name} PRIVATE -fprofile-arcs -ftest-coverage)
            endif()
            add_test(NAME ${_name} COMMAND ${_name})
        endfunction()

        add_client_isolated_module_test(test_antireplayguard tests/unit/test_antireplayguard.cpp
            src/core/antireplayguard.cpp)
        add_client_isolated_module_test(test_commandsigner tests/unit/test_commandsigner.cpp
            src/core/commandsigner.cpp)

        add_client_isolated_module_test(test_vehicle_api_parsers tests/unit/test_vehicle_api_parsers.cpp
            src/services/vehicle_api_parsers.cpp)
        set_target_properties(test_vehicle_api_parsers PROPERTIES AUTOMOC ON)

        add_executable(test_errorregistry
            tests/unit/test_errorregistry.cpp
            src/core/errorregistry.cpp
            src/core/errorregistry.h
            ../backend/src/protocol/fault_code.cpp
        )
        target_include_directories(test_errorregistry PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_errorregistry PRIVATE ${_TEST_LIBS})
        set_target_properties(test_errorregistry PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_errorregistry PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_errorregistry PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_errorregistry COMMAND test_errorregistry)

        add_client_isolated_module_test(test_clientmediabudget tests/unit/test_clientmediabudget.cpp
            src/media/ClientMediaBudget.cpp)
        add_client_isolated_module_test(test_rtcpcompoundparser tests/unit/test_rtcpcompoundparser.cpp
            src/media/RtcpCompoundParser.cpp
            src/media/RtpStreamClockContext.cpp)
        add_client_isolated_module_test(test_latencycompensator tests/unit/test_latencycompensator.cpp
            src/services/latencycompensator.cpp)
        add_client_isolated_module_test(test_rtppacketspscqueue tests/unit/test_rtppacketspscqueue.cpp
            src/media/RtpPacketSpscQueue.cpp
            src/media/ClientMediaBudget.cpp)

        add_client_isolated_module_test(test_rtptruejitterbuffer tests/unit/test_rtptruejitterbuffer.cpp
            src/media/RtpTrueJitterBuffer.cpp
            src/media/RtpStreamClockContext.cpp)

        add_client_isolated_module_test(test_webrtcurlresolve tests/unit/test_webrtcurlresolve.cpp
            src/utils/WebRtcUrlResolve.cpp)

        add_client_isolated_module_test(test_mqttcontrolenvelope tests/unit/test_mqttcontrolenvelope.cpp
            src/utils/MqttControlEnvelope.cpp)

        add_executable(test_webrtctransportdispatch
            tests/unit/test_webrtctransportdispatch.cpp
            src/utils/WebRtcTransportDispatch.cpp
        )
        target_include_directories(test_webrtctransportdispatch PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_webrtctransportdispatch PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_webrtctransportdispatch PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_webrtctransportdispatch PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_webrtctransportdispatch PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_webrtctransportdispatch COMMAND test_webrtctransportdispatch)

        add_executable(test_degradationmanager
            tests/unit/test_degradationmanager.cpp
            src/services/degradationmanager.cpp
            ${_TEST_DEPS}
        )
        target_include_directories(test_degradationmanager PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_degradationmanager PRIVATE ${_TEST_LIBS})
        set_target_properties(test_degradationmanager PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_degradationmanager PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_degradationmanager PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_degradationmanager COMMAND test_degradationmanager)

        add_executable(test_safetymonitorservice
            tests/unit/test_safetymonitorservice.cpp
            src/services/safetymonitorservice.cpp
            ${_TEST_DEPS}
        )
        target_include_directories(test_safetymonitorservice PRIVATE ${_TEST_INCLUDES})
        target_compile_definitions(test_safetymonitorservice PRIVATE REMOTE_DRIVING_UNIT_TEST)
        target_link_libraries(test_safetymonitorservice PRIVATE ${_TEST_LIBS})
        set_target_properties(test_safetymonitorservice PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_safetymonitorservice PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_safetymonitorservice PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_safetymonitorservice COMMAND test_safetymonitorservice)
        set_tests_properties(test_safetymonitorservice PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(test_errorrecoverymanager
            tests/unit/test_errorrecoverymanager.cpp
            src/services/errorrecoverymanager.cpp
        )
        target_include_directories(test_errorrecoverymanager PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_errorrecoverymanager PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_errorrecoverymanager PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_errorrecoverymanager PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_errorrecoverymanager PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_errorrecoverymanager COMMAND test_errorrecoverymanager)

        add_executable(test_diagnosticsservice
            tests/unit/test_diagnosticsservice.cpp
            src/services/diagnosticsservice.cpp
            src/core/performancemonitor.cpp
        )
        target_include_directories(test_diagnosticsservice PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_diagnosticsservice PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_diagnosticsservice PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_diagnosticsservice PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_diagnosticsservice PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_diagnosticsservice COMMAND test_diagnosticsservice)

        add_executable(test_httpendpointserver
            tests/unit/test_httpendpointserver.cpp
            src/core/httpendpointserver.cpp
            src/core/httpendpointserver.h
            src/core/metricscollector.cpp
            src/core/metricscollector.h
            src/core/configuration.cpp
            src/core/configuration.h
        )
        target_include_directories(test_httpendpointserver PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_httpendpointserver PRIVATE Qt6::Core Qt6::Network Qt6::Test)
        set_target_properties(test_httpendpointserver PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_httpendpointserver PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_httpendpointserver PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_httpendpointserver COMMAND test_httpendpointserver)

        add_executable(test_ratelimiter
            tests/unit/test_ratelimiter.cpp
            src/infrastructure/network/ratelimiter.cpp
            src/infrastructure/network/ratelimiter.h
            src/core/metricscollector.cpp
            src/core/metricscollector.h
            src/core/configuration.cpp
            src/core/configuration.h
        )
        target_include_directories(test_ratelimiter PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_ratelimiter PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_ratelimiter PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_ratelimiter PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_ratelimiter PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_ratelimiter COMMAND test_ratelimiter)

        # L3：Paho 与本地/容器 broker 的 TCP 连接烟测（无 MQTT_TEST_BROKER 时 QSKIP）
        if(ENABLE_MQTT_PAHO)
            add_executable(test_mqtt_paho_broker_smoke
                tests/integration/test_mqtt_paho_broker_smoke.cpp
            )
            target_compile_options(test_mqtt_paho_broker_smoke PRIVATE -fexceptions)
            target_include_directories(test_mqtt_paho_broker_smoke PRIVATE ${_TEST_INCLUDES})
            target_compile_definitions(test_mqtt_paho_broker_smoke PRIVATE ENABLE_MQTT_PAHO)
            target_link_libraries(test_mqtt_paho_broker_smoke PRIVATE Qt6::Core Qt6::Test paho-mqttpp3 paho-mqtt3as)
            if(UNIX AND NOT APPLE)
                target_link_libraries(test_mqtt_paho_broker_smoke PRIVATE pthread)
            endif()
            if(ENABLE_COVERAGE)
                target_compile_options(test_mqtt_paho_broker_smoke PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
                target_link_options(test_mqtt_paho_broker_smoke PRIVATE -fprofile-arcs -ftest-coverage)
            endif()
            add_test(NAME test_mqtt_paho_broker_smoke COMMAND test_mqtt_paho_broker_smoke)
        endif()

        add_executable(test_window_frame_policy
            tests/unit/test_window_frame_policy.cpp
            src/app/client_window_frame_policy.cpp
        )
        target_include_directories(test_window_frame_policy PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_window_frame_policy PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_window_frame_policy PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_window_frame_policy PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_window_frame_policy PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_window_frame_policy COMMAND test_window_frame_policy)

        add_executable(test_startup_readiness_profile
            tests/unit/test_startup_readiness_profile.cpp
            src/app/client_startup_readiness_gate.cpp
            src/core/configuration.cpp
        )
        target_include_directories(test_startup_readiness_profile PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_startup_readiness_profile PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_startup_readiness_profile PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_startup_readiness_profile PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_startup_readiness_profile PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_startup_readiness_profile COMMAND test_startup_readiness_profile)

        add_executable(test_present_health_auto_env
            tests/unit/test_present_health_auto_env.cpp
            src/app/client_present_health_auto_env.cpp
        )
        target_include_directories(test_present_health_auto_env PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_present_health_auto_env PRIVATE Qt6::Core Qt6::Test)
        set_target_properties(test_present_health_auto_env PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_present_health_auto_env PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_present_health_auto_env PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_present_health_auto_env COMMAND test_present_health_auto_env)

        add_client_isolated_module_test(test_videoframefingerprintcache
            tests/unit/test_videoframefingerprintcache.cpp
            src/media/VideoFrameFingerprintCache.cpp)

        add_executable(test_clientvideodiagcache
            tests/unit/test_clientvideodiagcache.cpp
            src/media/ClientVideoDiagCache.cpp
        )
        target_include_directories(test_clientvideodiagcache PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_clientvideodiagcache PRIVATE Qt6::Core Qt6::Test Qt6::Quick)
        set_target_properties(test_clientvideodiagcache PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_clientvideodiagcache PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_clientvideodiagcache PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_clientvideodiagcache COMMAND test_clientvideodiagcache)
        set_tests_properties(test_clientvideodiagcache PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(test_h264clientdiag
            tests/unit/test_h264clientdiag.cpp
            src/media/H264ClientDiag.cpp
        )
        target_include_directories(test_h264clientdiag PRIVATE ${_TEST_INCLUDES})
        target_link_libraries(test_h264clientdiag PRIVATE Qt6::Core Qt6::Test Qt6::Gui)
        set_target_properties(test_h264clientdiag PROPERTIES AUTOMOC ON)
        if(ENABLE_COVERAGE)
            target_compile_options(test_h264clientdiag PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
            target_link_options(test_h264clientdiag PRIVATE -fprofile-arcs -ftest-coverage)
        endif()
        add_test(NAME test_h264clientdiag COMMAND test_h264clientdiag)

        if(ENABLE_FFMPEG)
            add_executable(test_h264decoder
                tests/unit/test_h264decoder.cpp
                src/core/configuration.cpp
                src/h264decoder.cpp
                src/media/H264WebRtcHwBridge.cpp
                src/media/ClientVideoDiagCache.cpp
                src/media/ClientVideoStreamHealth.cpp
                src/media/H264ClientDiag.cpp
                src/media/RtpPacketSpscQueue.cpp
                src/media/ClientMediaBudget.cpp
                src/media/RtpStreamClockContext.cpp
                src/media/RtcpCompoundParser.cpp
                src/media/RtpTrueJitterBuffer.cpp
                src/media/VideoFrameFingerprintCache.cpp
                src/media/VideoInterlacedPolicy.cpp
                src/media/VideoSwsColorHelper.cpp
            )
            target_include_directories(test_h264decoder PRIVATE ${_TEST_INCLUDES} ${FFMPEG_INCLUDE_DIRS})
            target_link_libraries(test_h264decoder PRIVATE Qt6::Core Qt6::Test Qt6::Gui Qt6::Quick
                ${FFMPEG_LIBRARIES})
            target_compile_definitions(test_h264decoder PRIVATE ENABLE_FFMPEG)
            set_target_properties(test_h264decoder PROPERTIES AUTOMOC ON)
            if(ENABLE_COVERAGE)
                target_compile_options(test_h264decoder PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
                target_link_options(test_h264decoder PRIVATE -fprofile-arcs -ftest-coverage)
            endif()
            add_test(NAME test_h264decoder COMMAND test_h264decoder)
            set_tests_properties(test_h264decoder PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

            add_executable(test_video_interlaced_policy
                tests/unit/test_video_interlaced_policy.cpp
                src/media/VideoInterlacedPolicy.cpp
            )
            target_include_directories(test_video_interlaced_policy PRIVATE ${_TEST_INCLUDES} ${FFMPEG_INCLUDE_DIRS})
            target_link_libraries(test_video_interlaced_policy PRIVATE Qt6::Core Qt6::Test ${FFMPEG_LIBRARIES})
            target_compile_definitions(test_video_interlaced_policy PRIVATE ENABLE_FFMPEG)
            set_target_properties(test_video_interlaced_policy PROPERTIES AUTOMOC ON)
            if(ENABLE_COVERAGE)
                target_compile_options(test_video_interlaced_policy PRIVATE -fprofile-arcs -ftest-coverage -g -O0)
                target_link_options(test_video_interlaced_policy PRIVATE -fprofile-arcs -ftest-coverage)
            endif()
            add_test(NAME test_video_interlaced_policy COMMAND test_video_interlaced_policy)
        endif()

        set(_CLIENT_ALL_UNIT_TEST_TARGETS
            client_systemstatemachine_test
            test_configuration
            test_threadpool
            test_networkquality
            test_logger
            test_vehiclecontrolservice
            test_sessionmanager
            test_antireplayguard
            test_commandsigner
            test_vehicle_api_parsers
            test_errorregistry
            test_clientmediabudget
            test_rtcpcompoundparser
            test_latencycompensator
            test_rtppacketspscqueue
            test_rtptruejitterbuffer
            test_webrtcurlresolve
            test_mqttcontrolenvelope
            test_webrtctransportdispatch
            test_degradationmanager
            test_safetymonitorservice
            test_errorrecoverymanager
            test_diagnosticsservice
            test_httpendpointserver
            test_ratelimiter
            test_window_frame_policy
            test_startup_readiness_profile
            test_present_health_auto_env
            test_videoframefingerprintcache
            test_clientvideodiagcache
            test_h264clientdiag
        )
        if(ENABLE_FFMPEG)
            list(APPEND _CLIENT_ALL_UNIT_TEST_TARGETS test_h264decoder test_video_interlaced_policy)
        endif()
        if(ENABLE_MQTT_PAHO)
            list(APPEND _CLIENT_ALL_UNIT_TEST_TARGETS test_mqtt_paho_broker_smoke)
        endif()

        list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")

        # 一键：构建目录下运行全部客户端 CTest（标签过滤已移除，避免漏跑）
        add_custom_target(run-unit-tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running all client CTest unit tests"
            DEPENDS ${_CLIENT_ALL_UNIT_TEST_TARGETS}
        )
    endif()
    
    add_custom_target(run-tests-verbose
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -V
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all tests verbosely"
        DEPENDS
            client_systemstatemachine_test
            test_configuration
            test_threadpool
            test_networkquality
            test_logger
            test_vehiclecontrolservice
            test_sessionmanager
            test_antireplayguard
            test_commandsigner
            test_vehicle_api_parsers
            test_errorregistry
            test_clientmediabudget
            test_rtcpcompoundparser
            test_latencycompensator
            test_rtppacketspscqueue
            test_rtptruejitterbuffer
            test_webrtcurlresolve
            test_mqttcontrolenvelope
            test_webrtctransportdispatch
            test_degradationmanager
            test_safetymonitorservice
            test_errorrecoverymanager
            test_diagnosticsservice
            test_httpendpointserver
            test_ratelimiter
            test_window_frame_policy
            test_startup_readiness_profile
            test_present_health_auto_env
            test_videoframefingerprintcache
            test_clientvideodiagcache
            test_h264clientdiag
    )
    if(ENABLE_FFMPEG AND TARGET test_h264decoder)
        add_dependencies(run-tests-verbose test_h264decoder)
    endif()
    if(ENABLE_MQTT_PAHO AND TARGET test_mqtt_paho_broker_smoke)
        add_dependencies(run-tests-verbose test_mqtt_paho_broker_smoke)
    endif()
    
    # 覆盖率报告目标（需要 lcov + genhtml）
    if(ENABLE_COVERAGE AND LCOV_BIN AND GENHTML_BIN)
        add_custom_target(coverage-report
            COMMAND ${LCOV_BIN} --capture --directory . --output-file coverage.info
            COMMAND ${GENHTML_BIN} coverage.info --output-directory coverage-html
            COMMAND echo "Coverage report generated in coverage-html/"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report"
            DEPENDS
                client_systemstatemachine_test
                test_configuration
                test_threadpool
                test_networkquality
                test_logger
                test_vehiclecontrolservice
                test_sessionmanager
                test_antireplayguard
                test_commandsigner
                test_vehicle_api_parsers
                test_errorregistry
                test_clientmediabudget
                test_rtcpcompoundparser
                test_latencycompensator
                test_rtppacketspscqueue
                test_rtptruejitterbuffer
                test_webrtcurlresolve
                test_mqttcontrolenvelope
                test_webrtctransportdispatch
                test_degradationmanager
                test_safetymonitorservice
                test_errorrecoverymanager
                test_diagnosticsservice
                test_httpendpointserver
                test_ratelimiter
                test_window_frame_policy
                test_startup_readiness_profile
                test_present_health_auto_env
                test_videoframefingerprintcache
                test_clientvideodiagcache
                test_h264clientdiag
        )
        if(ENABLE_FFMPEG AND TARGET test_h264decoder)
            add_dependencies(coverage-report test_h264decoder)
        endif()
        if(ENABLE_MQTT_PAHO AND TARGET test_mqtt_paho_broker_smoke)
            add_dependencies(coverage-report test_mqtt_paho_broker_smoke)
        endif()
        add_custom_target(coverage-clean
            COMMAND rm -rf coverage.info coverage-html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Cleaning coverage data"
        )
    endif()
endif()

# V2：RTCP compound 解析 fuzz（Clang + libFuzzer；不依赖 ENABLE_QT6_Test）
option(ENABLE_CLIENT_FUZZ "Build fuzz_rtcp_compound (Clang libFuzzer)" OFF)
if(ENABLE_CLIENT_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "ENABLE_CLIENT_FUZZ requires Clang; skipping fuzz_rtcp_compound")
    else()
        add_executable(fuzz_rtcp_compound
            tests/fuzz/fuzz_rtcp_compound.cpp
            src/media/RtcpCompoundParser.cpp
            src/media/RtpStreamClockContext.cpp
        )
        target_include_directories(fuzz_rtcp_compound PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(fuzz_rtcp_compound PRIVATE Qt6::Core)
        target_compile_options(fuzz_rtcp_compound PRIVATE -fsanitize=fuzzer-no-link,address -g -O1)
        target_link_options(fuzz_rtcp_compound PRIVATE -fsanitize=fuzzer,address)
    endif()
endif()

# ── Example plugin (IPlugin / CLIENT_PLUGIN_DIR) ─────────────────────────────
option(CLIENT_BUILD_EXAMPLE_PLUGIN "Build noop IPlugin sample (.so in build/plugins)" OFF)
if(CLIENT_BUILD_EXAMPLE_PLUGIN)
    add_library(client_noop_plugin MODULE
        plugins/noop/noop_plugin.cpp
    )
    set_target_properties(client_noop_plugin PROPERTIES
        AUTOMOC ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
    target_include_directories(client_noop_plugin PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(client_noop_plugin PRIVATE Qt6::Core)
endif()

# ── Benchmarks ───────────────────────────────────────────────────────────────
if(ENABLE_BENCHMARKS)
    add_subdirectory(tests/performance)
endif()

# ── Status ───────────────────────────────────────────────────────────────────
message(STATUS "=== RemoteDrivingClient Build Configuration ===")
message(STATUS "  Qt6 version:       ${Qt6_VERSION}")
message(STATUS "  C++ standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  FFmpeg:            ${ENABLE_FFMPEG}")
message(STATUS "  VA-API:            ${ENABLE_VAAPI}")
message(STATUS "  EGL DMA-BUF:       ${ENABLE_EGL_DMABUF}")
message(STATUS "  NVDEC:             ${ENABLE_NVDEC}")
# CI / 人工 grep：单行明确 ON/OFF（与 message(STATUS) 布尔输出一致）
message(STATUS "Client HW decode (CI grep): VA-API=${ENABLE_VAAPI} NVDEC=${ENABLE_NVDEC}")
message(STATUS "  WebRTC:            ${ENABLE_WEBRTC_LIBDATACHANNEL}")
message(STATUS "  MQTT Paho:         ${ENABLE_MQTT_PAHO}")
message(STATUS "  Client fuzz:       ${ENABLE_CLIENT_FUZZ}")
message(STATUS "  Example plugin:    ${CLIENT_BUILD_EXAMPLE_PLUGIN}")
message(STATUS "  Benchmarks:        ${ENABLE_BENCHMARKS}")
