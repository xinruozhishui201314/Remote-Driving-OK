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
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui Quick Qml)

# ── Qt6 可选模块 ─────────────────────────────────────────────────────────────
foreach(_mod Multimedia WebSockets QuickControls2 ShaderTools OpenGL Test)
    find_package(Qt6 QUIET COMPONENTS ${_mod})
    if(Qt6${_mod}_FOUND)
        message(STATUS "Found Qt6${_mod}")
        set(ENABLE_QT6_${_mod} ON)
    else()
        message(WARNING "Qt6${_mod} not found")
        set(ENABLE_QT6_${_mod} OFF)
    endif()
endforeach()

# ── Qt ShaderTools 编译 ──────────────────────────────────────────────────────────
# 编译 GLSL → Qt Shader Binary (.qsb)，供 QSGMaterialShader 使用。
# qsb 查找顺序：① PATH / Qt 安装目录  ② FetchContent 从 GitHub 源码编译（离线 fallback）
# 若两者均失败 → FATAL_ERROR（视频无法渲染）
if(NOT ENABLE_QT6_ShaderTools)
    message(FATAL_ERROR "Qt6 ShaderTools (find_package) not found — GPU video rendering will FAIL! "
        "Install Qt6 ShaderTools in Docker image (see client/Dockerfile.client-dev).")
endif()

# 查找 qsb：优先 PATH → Qt bin 目录
find_program(QSB_EXECUTABLE NAMES qsb qmake-qsb qsb6 PATHS
    ${Qt6_DIR}/../../../bin
    ${Qt6_DIR}/../../bin
    $ENV{HOME}/Qt/6.*/gcc_64/bin
    $ENV{PATH}
    CMAKE_FIND_ROOT_PATH_BEFORE_MAKE
)

if(NOT QSB_EXECUTABLE)
    # qsb 不可用：从 GitHub FetchContent 编译 qsb（一次性，构建时需要 git + cmake）
    message(WARNING "qsb not in PATH — building from source via FetchContent (one-time setup)")
    include(FetchContent)
    FetchContent_Declare(
        qsb_src
        GIT_REPOSITORY https://github.com/qt/qttools.git
        GIT_TAG        v6.8.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  src/shaders
    )
    FetchContent_MakeAvailable(qsb_src)
    find_program(QSB_EXECUTABLE NAMES qsb PATHS
        "${CMAKE_BINARY_DIR}/qsb_src-src/src/shaders"
        "${CMAKE_BINARY_DIR}/qsb_src-prefix/src/qsb_src-build/src/shaders"
        CMAKE_FIND_ROOT_PATH_BEFORE_MAKE
    )
endif()

if(QSB_EXECUTABLE)
    message(STATUS "ShaderTools: qsb=${QSB_EXECUTABLE}")
    set(SHADER_GEN_DIR "${CMAKE_BINARY_DIR}/shaders_gen")
    file(MAKE_DIRECTORY "${SHADER_GEN_DIR}")

    set(VERT_QSB "${SHADER_GEN_DIR}/video.vert.qsb")
    set(FRAG_QSB "${SHADER_GEN_DIR}/video.frag.qsb")

    add_custom_command(OUTPUT "${VERT_QSB}"
        COMMAND ${QSB_EXECUTABLE} --glsl "440:core:es300" -o "${VERT_QSB}"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/video.vert"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/video.vert"
        COMMENT "qsb: video.vert → video.vert.qsb"
        VERBATIM COMMAND_ERROR_IS_FATAL ANY
    )
    add_custom_command(OUTPUT "${FRAG_QSB}"
        COMMAND ${QSB_EXECUTABLE} --glsl "440:core:es300" -o "${FRAG_QSB}"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/video.frag"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/video.frag"
        COMMENT "qsb: video.frag → video.frag.qsb"
        VERBATIM COMMAND_ERROR_IS_FATAL ANY
    )

    if(NOT TARGET shader_compilation)
        add_custom_target(shader_compilation ALL)
    endif()
    add_custom_target(shader_compilation_files DEPENDS "${VERT_QSB}" "${FRAG_QSB}")
    add_dependencies(shader_compilation shader_compilation_files)

    # 验证 .qsb 非空（缺失则 FATAL）
    add_custom_command(TARGET shader_compilation_files POST_BUILD
        COMMAND bash -c "f='${VERT_QSB}'; ff='${FRAG_QSB}'; "
            "test -s \"$f\" || { echo FATAL: video.vert.qsb missing; exit 1; }; "
            "test -s \"$ff\" || { echo FATAL: video.frag.qsb missing; exit 1; }; "
            "echo \"qsb OK: vert=\$(wc -c <\"\$f\") frag=\$(wc -c <\"\$ff\") bytes\""
        VERBATIM COMMAND_ERROR_IS_FATAL ANY
    )

    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shaders.qrc.in"
        "${CMAKE_BINARY_DIR}/shaders.qrc" @ONLY)
    message(STATUS "ShaderTools: enabled")
else()
    message(FATAL_ERROR "qsb not found and FetchContent build failed — video rendering will FAIL! "
        "Install qsb in Docker: see client/Dockerfile.client-dev for installation commands.")
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
    # Entry point
    src/main.cpp

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

    # ── Infrastructure Layer ─────────────────────────────────────────────────
    # Network
    src/infrastructure/itransportmanager.cpp
    src/infrastructure/mqtttransportadapter.cpp
    src/infrastructure/controlloopticker.cpp
    src/infrastructure/network/WebRTCChannel.cpp
    src/infrastructure/network/UDPChannel.cpp
    src/infrastructure/network/AdaptiveBitrate.cpp
    src/infrastructure/network/FECEncoder.cpp

    # Media
    src/infrastructure/media/DecoderFactory.cpp
    src/infrastructure/media/FramePool.cpp
    src/infrastructure/media/MediaPipeline.cpp
    # GPU Interop
    src/infrastructure/media/gpu/GpuInteropFactory.cpp

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
    src/presentation/renderers/VideoRenderer.cpp
    src/presentation/renderers/VideoSGNode.cpp
    src/presentation/renderers/VideoMaterial.cpp
    src/presentation/models/TelemetryModel.cpp
    src/presentation/models/NetworkStatusModel.cpp
    src/presentation/models/SafetyStatusModel.cpp
)

set(HEADERS
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

    src/infrastructure/itransportmanager.h
    src/infrastructure/mqtttransportadapter.h
    src/infrastructure/controlloopticker.h
    src/infrastructure/network/WebRTCChannel.h
    src/infrastructure/network/UDPChannel.h
    src/infrastructure/network/AdaptiveBitrate.h
    src/infrastructure/network/FECEncoder.h

    src/infrastructure/media/IHardwareDecoder.h
    src/infrastructure/media/DecoderFactory.h
    src/infrastructure/media/FramePool.h
    src/infrastructure/media/MediaPipeline.h
    src/infrastructure/media/gpu/IGpuInterop.h
    src/infrastructure/media/gpu/EGLDmaBufInterop.h
    src/infrastructure/media/gpu/CpuUploadInterop.h
    src/infrastructure/media/gpu/GpuInteropFactory.h

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

    src/presentation/renderers/VideoRenderer.h
    src/presentation/renderers/VideoSGNode.h
    src/presentation/renderers/VideoMaterial.h
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

# Optional: EGL DMA-BUF interop
if(ENABLE_EGL_DMABUF)
    list(APPEND SOURCES src/infrastructure/media/gpu/EGLDmaBufInterop.cpp)
endif()

# ── Executable ────────────────────────────────────────────────────────────────
qt_add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 将 qsb 着色器资源文件加入可执行目标（必须在 qt_add_executable 之后）
if(QSB_EXECUTABLE AND EXISTS "${CMAKE_BINARY_DIR}/shaders.qrc")
    qt_add_resources(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/shaders.qrc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/media/gpu
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation
)

# QShader 头文件在 QtGui/6.8.0/QtGui/rhi/（版本化私有路径），client-dev 镜像中 Qt 路径固定
if(ENABLE_QT6_ShaderTools)
    set(_qt6_install_prefix "/opt/Qt/6.8.0/gcc_64")
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${_qt6_install_prefix}/include/QtGui/6.8.0/QtGui"
    )
endif()

# ── Link Qt (required) ────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui
)
target_link_options(${PROJECT_NAME} PRIVATE
    $<$<NOT:$<BOOL:${CMAKE_CROSSCOMPILING}>>:-rdynamic>)

# ── Link optional Qt modules ──────────────────────────────────────────────────
if(ENABLE_QT6_Multimedia)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Multimedia)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT6_MULTIMEDIA)
endif()
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
if(ENABLE_QT6_ShaderTools)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::ShaderToolsPrivate Qt6::ShaderTools)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT6_SHADERTOOLS)
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

    # State machine tests
    add_executable(client_systemstatemachine_test
        tests/test_systemstatemachine.cpp
        src/core/eventbus.cpp
        src/core/systemstatemachine.cpp
    )
    target_include_directories(client_systemstatemachine_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(client_systemstatemachine_test PRIVATE
        Qt6::Core Qt6::Test)
    add_test(NAME client_systemstatemachine_test
        COMMAND client_systemstatemachine_test)

    # Unit tests for services
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
        file(GLOB UNIT_TEST_SOURCES tests/unit/*.cpp)
        if(UNIT_TEST_SOURCES)
            add_executable(client_unit_tests ${UNIT_TEST_SOURCES}
                src/core/eventbus.cpp
                src/core/systemstatemachine.cpp
                src/core/configuration.cpp
                src/core/logger.cpp
                src/core/threadpool.cpp
            )
            target_include_directories(client_unit_tests PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src)
            target_link_libraries(client_unit_tests PRIVATE
                Qt6::Core Qt6::Test Qt6::Network)
            add_test(NAME client_unit_tests COMMAND client_unit_tests)
        endif()
    endif()
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
message(STATUS "  Benchmarks:        ${ENABLE_BENCHMARKS}")
