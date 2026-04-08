cmake_minimum_required(VERSION 3.21)
project(RemoteDrivingClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui Quick Qml Multimedia)
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

# 视频显示已改为 Qt Multimedia（QVideoSink + QML VideoOutput），不再编译自定义 GLSL/qsb。
message(STATUS "Video: Qt Multimedia QVideoSink + VideoOutput（无 qsb 着色器依赖）")

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
if(EGL_FOUND AND DRM_FOUND AND FFMPEG_FOUND AND UNIX AND NOT APPLE)
    message(STATUS "Found EGL + DRM — enabling EGL DMA-BUF zero-copy interop")
    set(ENABLE_EGL_DMABUF ON)
else()
    message(STATUS "EGL DMA-BUF interop disabled (requires EGL + DRM + FFmpeg on Linux)")
    set(ENABLE_EGL_DMABUF OFF)
endif()

# NVDEC（NVIDIA GPU 硬件解码，需 FFmpeg CUDA hwaccel）
option(ENABLE_NVDEC "Enable NVIDIA NVDEC hardware decode via FFmpeg CUDA" OFF)
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
    src/app/client_logging_setup.cpp
    src/app/client_crash_diagnostics.cpp
    src/app/client_x11_deep_diag.cpp
    src/app/client_app_bootstrap.cpp
    src/app/client_display_runtime_policy.cpp

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
    src/infrastructure/network/WebRTCChannel.cpp
    src/infrastructure/network/UDPChannel.cpp
    src/infrastructure/network/AdaptiveBitrate.cpp
    src/infrastructure/network/FECEncoder.cpp

    # Media
    src/infrastructure/media/DecoderFactory.cpp
    src/infrastructure/media/FramePool.cpp
    src/infrastructure/media/MediaPipeline.cpp
    # Hardware input
    src/infrastructure/hardware/InputSampler.cpp
    src/infrastructure/hardware/KeyboardMouseInput.cpp

    # ── Application Service Layer ────────────────────────────────────────────
    src/services/sessionmanager.cpp
    src/services/vehiclecontrolservice.cpp
    src/services/safetymonitorservice.cpp
    src/services/degradationmanager.cpp
    src/services/latencycompensator.cpp
    src/services/errorrecoverymanager.cpp
    src/services/diagnosticsservice.cpp

    # ── Presentation Layer ───────────────────────────────────────────────────
    src/presentation/models/TelemetryModel.cpp
    src/presentation/models/NetworkStatusModel.cpp
    src/presentation/models/SafetyStatusModel.cpp
)

set(HEADERS
    src/app/client_logging_setup.h
    src/app/client_crash_diagnostics.h
    src/app/client_x11_deep_diag.h
    src/app/client_app_bootstrap.h
    src/app/client_display_runtime_policy.h
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
    src/infrastructure/network/WebRTCChannel.h
    src/infrastructure/network/UDPChannel.h
    src/infrastructure/network/AdaptiveBitrate.h
    src/infrastructure/network/FECEncoder.h

    src/infrastructure/media/IHardwareDecoder.h
    src/infrastructure/media/DecoderFactory.h
    src/infrastructure/media/FramePool.h
    src/infrastructure/media/MediaPipeline.h
    src/infrastructure/hardware/IInputDevice.h
    src/infrastructure/hardware/InputSampler.h
    src/infrastructure/hardware/KeyboardMouseInput.h

    src/services/sessionmanager.h
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
        src/infrastructure/media/FFmpegSoftDecoder.cpp
    )
    list(APPEND HEADERS
        src/h264decoder.h
        src/infrastructure/media/FFmpegSoftDecoder.h
    )
endif()

# Optional: VAAPI decoder
if(ENABLE_VAAPI)
    list(APPEND SOURCES src/infrastructure/media/VAAPIDecoder.cpp)
    list(APPEND HEADERS src/infrastructure/media/VAAPIDecoder.h)
endif()

# Optional: NVDEC decoder
if(ENABLE_NVDEC)
    list(APPEND SOURCES src/infrastructure/media/NvdecDecoder.cpp)
    list(APPEND HEADERS src/infrastructure/media/NvdecDecoder.h)
endif()

# ── Executable ────────────────────────────────────────────────────────────────
qt_add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/media/gpu
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation
)

# ── Link Qt (required) ────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui
    Qt6::Multimedia Qt6::MultimediaQuick
)

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
    target_link_libraries(${PROJECT_NAME} PRIVATE paho-mqttpp3)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_MQTT_PAHO)
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

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# ── Tests ────────────────────────────────────────────────────────────────────
if(ENABLE_QT6_Test)
    enable_testing()

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
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(client_systemstatemachine_test PRIVATE
        Qt6::Core Qt6::Test)
    add_test(NAME client_systemstatemachine_test
        COMMAND client_systemstatemachine_test)

    # Unit tests for services — 每个测试文件有 QTEST_MAIN，各自独立可执行
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
        set(_TEST_DEPS
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
        )
        set(_TEST_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
        set(_TEST_LIBS Qt6::Core Qt6::Test Qt6::Network)

        # 通用测试目标宏（避免重复代码）
        function(add_client_unit_test NAME TEST_FILE)
            add_executable(${NAME} ${TEST_FILE} ${_TEST_DEPS})
            target_include_directories(${NAME} PRIVATE ${_TEST_INCLUDES})
            target_link_libraries(${NAME} PRIVATE ${_TEST_LIBS})
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
            src/services/vehiclecontrolservice.cpp
            src/services/latencycompensator.cpp
            src/core/commandsigner.cpp
            src/vehiclemanager.cpp
            src/infrastructure/itransportmanager.cpp
        )
        target_include_directories(test_vehiclecontrolservice PRIVATE ${_TEST_INCLUDES})
        target_compile_definitions(test_vehiclecontrolservice PRIVATE VEHICLE_CONTROL_SERVICE_UNIT_TEST)
        # vehiclecontrolservice → webrtcstreammanager.h → webrtcclient.h: QVideoSink (Multimedia), QImage (Gui), QQuickItem (Quick)
        target_link_libraries(test_vehiclecontrolservice PRIVATE ${_TEST_LIBS} Qt6::Gui Qt6::Quick Qt6::Multimedia)
        if(UNIX AND NOT APPLE)
            target_link_libraries(test_vehiclecontrolservice PRIVATE atomic)
        endif()
        add_test(NAME test_vehiclecontrolservice COMMAND test_vehiclecontrolservice)

        # CTest 全局配置
        include(CTest)
        list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")

        # 统一运行目标
        add_custom_target(run-unit-tests
            COMMAND ctest --output-on-failure -L "test_configuration;test_threadpool;test_networkquality;test_logger"
            COMMENT "Running all unit tests with output on failure"
            DEPENDS test_configuration test_threadpool test_networkquality test_logger test_vehiclecontrolservice
        )
    endif()
    
    add_custom_target(run-tests-verbose
        COMMAND ctest --output-on-failure -V
        COMMENT "Running all tests verbosely"
        DEPENDS client_systemstatemachine_test test_configuration test_threadpool test_networkquality test_logger test_vehiclecontrolservice
    )
    
    # 覆盖率报告目标（需要 lcov + genhtml）
    if(ENABLE_COVERAGE AND LCOV_BIN AND GENHTML_BIN)
        add_custom_target(coverage-report
            COMMAND ${LCOV_BIN} --capture --directory . --output-file coverage.info
            COMMAND ${GENHTML_BIN} coverage.info --output-directory coverage-html
            COMMAND echo "Coverage report generated in coverage-html/"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report"
            DEPENDS client_systemstatemachine_test test_configuration test_threadpool test_networkquality test_logger test_vehiclecontrolservice
        )
        add_custom_target(coverage-clean
            COMMAND rm -rf coverage.info coverage-html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Cleaning coverage data"
        )
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
message(STATUS "  WebRTC:            ${ENABLE_WEBRTC_LIBDATACHANNEL}")
message(STATUS "  MQTT Paho:         ${ENABLE_MQTT_PAHO}")
message(STATUS "  Example plugin:    ${CLIENT_BUILD_EXAMPLE_PLUGIN}")
message(STATUS "  Benchmarks:        ${ENABLE_BENCHMARKS}")
