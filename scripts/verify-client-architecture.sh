#!/usr/bin/env bash
# ============================================================
# 客户端架构合规验证脚本（《客户端架构设计》§7.2）
# 验证所有必须文件是否存在，并做简单的接口完整性检查。
# ============================================================
set -uo pipefail

CLIENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../client" && pwd)"
PASS=0; FAIL=0

check() {
    local desc="$1"
    local file="$2"
    if [[ -f "${CLIENT_DIR}/${file}" ]]; then
        echo "  ✅  ${desc}"
        ((PASS++))
    else
        echo "  ❌  ${desc} — MISSING: ${file}"
        ((FAIL++))
    fi
}

check_contains() {
    local desc="$1"
    local file="$2"
    local pattern="$3"
    if grep -q "${pattern}" "${CLIENT_DIR}/${file}" 2>/dev/null; then
        echo "  ✅  ${desc}"
        ((PASS++))
    else
        echo "  ❌  ${desc} — pattern '${pattern}' not found in ${file}"
        ((FAIL++))
    fi
}

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  RemoteDrivingClient 架构合规验证"
echo "════════════════════════════════════════════════════════════"

# ─── Phase 1: Core Framework ─────────────────────────────────
echo ""
echo "【Phase 1】核心框架层"
check "LockFreeQueue (SPSC)"       "src/utils/LockFreeQueue.h"
check "TripleBuffer"               "src/utils/TripleBuffer.h"
check "CircularBuffer"             "src/utils/CircularBuffer.h"
check "PercentileStats"            "src/utils/PercentileStats.h"
check "TimeUtils"                  "src/utils/TimeUtils.h"
check "EventBus (header)"          "src/core/eventbus.h"
check "EventBus (impl)"            "src/core/eventbus.cpp"
check "SystemStateMachine (header)" "src/core/systemstatemachine.h"
check "SystemStateMachine (impl)"   "src/core/systemstatemachine.cpp"
check "ThreadPool"                 "src/core/threadpool.h"
check "Configuration"             "src/core/configuration.h"
check "Logger"                    "src/core/logger.h"
check "PerformanceMonitor"        "src/core/performancemonitor.h"
check "IPlugin"                   "src/core/iplugin.h"
check "PluginContext"              "src/core/plugincontext.h"
check "PluginManager"             "src/core/pluginmanager.h"
check "MemoryManager"             "src/core/memorymanager.h"

check_contains "C++20 standard in CMake" "CMakeLists.txt" "CMAKE_CXX_STANDARD 20"
check_contains "FSM has CONNECTING state" "src/core/systemstatemachine.h" "CONNECTING"
check_contains "FSM has PRE_FLIGHT state" "src/core/systemstatemachine.h" "PRE_FLIGHT"
check_contains "EventBus type-safe template" "src/core/eventbus.h" "subscribe"

# ─── Phase 2: Infrastructure Layer ───────────────────────────
echo ""
echo "【Phase 2a】基础设施层 - 网络传输"
check "ITransportManager"         "src/infrastructure/itransportmanager.h"
check "TransportChannel enum"     "src/infrastructure/itransportmanager.h"
check "WebRTCChannel"             "src/infrastructure/network/WebRTCChannel.h"
check "UDPChannel"                "src/infrastructure/network/UDPChannel.h"
check "AdaptiveBitrate"           "src/infrastructure/network/AdaptiveBitrate.h"
check "FECEncoder"                "src/infrastructure/network/FECEncoder.h"

check_contains "TransportChannel enum" "src/infrastructure/itransportmanager.h" "CONTROL_CRITICAL"
check_contains "MultiChannel send"     "src/infrastructure/itransportmanager.h" "TransportChannel channel"

echo ""
echo "【Phase 2b】基础设施层 - 媒体管线"
check "IHardwareDecoder"          "src/infrastructure/media/IHardwareDecoder.h"
check "VideoFrame"                "src/infrastructure/media/IHardwareDecoder.h"
check "FramePool"                 "src/infrastructure/media/FramePool.h"
check "DecoderFactory"            "src/infrastructure/media/DecoderFactory.h"
check "MediaPipeline"             "src/infrastructure/media/MediaPipeline.h"
check "FFmpegSoftDecoder"         "src/infrastructure/media/FFmpegSoftDecoder.h"
check "VAAPIDecoder"              "src/infrastructure/media/VAAPIDecoder.h"

check_contains "SPSC queue in MediaPipeline" "src/infrastructure/media/MediaPipeline.h" "SPSCQueue"
check_contains "FramePool shared_ptr"        "src/infrastructure/media/FramePool.h" "shared_from_this"

echo ""
echo "【Phase 2c】基础设施层 - 硬件输入"
check "IInputDevice"              "src/infrastructure/hardware/IInputDevice.h"
check "InputSampler"              "src/infrastructure/hardware/InputSampler.h"
check "KeyboardMouseInput"        "src/infrastructure/hardware/KeyboardMouseInput.h"

check_contains "200Hz in InputSampler" "src/infrastructure/hardware/InputSampler.h" "200"
check_contains "Deadzone filter"       "src/infrastructure/hardware/InputSampler.h" "deadzone"

# ─── Phase 3: Application Services ───────────────────────────
echo ""
echo "【Phase 3】应用服务层"
check "VehicleControlService"     "src/services/vehiclecontrolservice.h"
check "LatencyCompensator"        "src/services/latencycompensator.h"
check "SafetyMonitorService"      "src/services/safetymonitorservice.h"
check "DegradationManager"        "src/services/degradationmanager.h"
check "ErrorRecoveryManager"      "src/services/errorrecoverymanager.h"
check "DiagnosticsService"        "src/services/diagnosticsservice.h"

check_contains "100Hz control loop" "src/services/vehiclecontrolservice.h" "controlRateHz"
check_contains "50Hz safety checks" "src/services/safetymonitorservice.h" "kSafetyCheckHz = 50"
check_contains "6-level degradation" "src/services/degradationmanager.h" "SAFETY_STOP"
check_contains "4-level recovery"    "src/services/errorrecoverymanager.h" "SAFE_STOP"
check_contains "Quadratic prediction" "src/services/latencycompensator.cpp" "quadraticExtrapolation"

# ─── Phase 4: Presentation Layer ─────────────────────────────
echo ""
echo "【Phase 4】表现层"
check "VideoRenderer (QQuickItem)"  "src/presentation/renderers/VideoRenderer.h"
check "VideoSGNode"                 "src/presentation/renderers/VideoSGNode.h"
check "VideoMaterial"               "src/presentation/renderers/VideoMaterial.h"
check "GLSL vertex shader"          "shaders/video.vert"
check "GLSL fragment shader"        "shaders/video.frag"
check "TelemetryModel"              "src/presentation/models/TelemetryModel.h"
check "NetworkStatusModel"          "src/presentation/models/NetworkStatusModel.h"
check "SafetyStatusModel"           "src/presentation/models/SafetyStatusModel.h"
check "DrivingHUD.qml"              "qml/DrivingHUD.qml"
check "Theme.qml"                   "qml/styles/Theme.qml"
check "SpeedGauge.qml"              "qml/components/SpeedGauge.qml"
check "NetworkStatusBar.qml"        "qml/components/NetworkStatusBar.qml"
check "SafetyWarningOverlay.qml"    "qml/components/SafetyWarningOverlay.qml"
check "SteeringIndicator.qml"       "qml/components/SteeringIndicator.qml"
check "GearIndicator.qml"           "qml/components/GearIndicator.qml"

check_contains "Triple buffer in VideoRenderer" "src/presentation/renderers/VideoRenderer.h" "m_middleIdx"
check_contains "Zero-copy QSGNode"              "src/presentation/renderers/VideoSGNode.h" "QSGGeometryNode"
check_contains "YUV-RGB shader BT.709"          "shaders/video.frag" "BT.709"

# ─── Phase 5: Engineering ────────────────────────────────────
echo ""
echo "【Phase 5】工程化"
check "conanfile.py"               "conanfile.py"
check ".clang-format"              ".clang-format"
check ".clang-tidy"                ".clang-tidy"
check "MemoryManager"              "src/core/memorymanager.h"
check "LatencyBenchmark"           "tests/performance/LatencyBenchmark.cpp"

check_contains "ObjectPool template"    "src/core/memorymanager.h" "ObjectPool"
check_contains "mlockall"               "src/core/memorymanager.cpp" "mlockall"

# ─── Phase 6: Anti-Replay Security ───────────────────────────
echo ""
echo "【Phase 6】反重放安全"
check "AntiReplayGuard header"     "src/core/antireplayguard.h"
check "AntiReplayGuard impl"       "src/core/antireplayguard.cpp"
check "CommandSigner header"       "src/core/commandsigner.h"
check "CommandSigner impl"         "src/core/commandsigner.cpp"

check_contains "Sliding window 1024"   "src/core/antireplayguard.h"  "WINDOW_SIZE"
check_contains "Timestamp drift check" "src/core/antireplayguard.h"  "MAX_TIMESTAMP_DRIFT_MS"
check_contains "HMAC-SHA256"           "src/core/commandsigner.cpp"  "Sha256"
check_contains "Constant-time compare" "src/core/commandsigner.cpp"  "diff |="
check_contains "Sign in VehicleControl" "src/services/vehiclecontrolservice.cpp" "m_signer.sign"
check_contains "Session creds VCS"     "src/services/vehiclecontrolservice.cpp" "setSessionCredentials"
check_contains "Replay block in MQTT"  "src/infrastructure/mqtttransportadapter.cpp" "REPLAY BLOCKED"
check_contains "HMAC verify in MQTT"   "src/infrastructure/mqtttransportadapter.cpp" "verify"

# ─── Phase 7: GPU Zero-Copy ──────────────────────────────────
echo ""
echo "【Phase 7】GPU 零拷贝"
check "IGpuInterop interface"      "src/infrastructure/media/gpu/IGpuInterop.h"
check "EGLDmaBufInterop header"    "src/infrastructure/media/gpu/EGLDmaBufInterop.h"
check "EGLDmaBufInterop impl"      "src/infrastructure/media/gpu/EGLDmaBufInterop.cpp"
check "CpuUploadInterop"           "src/infrastructure/media/gpu/CpuUploadInterop.h"
check "GpuInteropFactory header"   "src/infrastructure/media/gpu/GpuInteropFactory.h"
check "GpuInteropFactory impl"     "src/infrastructure/media/gpu/GpuInteropFactory.cpp"
check "NvdecDecoder header"        "src/infrastructure/media/NvdecDecoder.h"
check "NvdecDecoder impl"          "src/infrastructure/media/NvdecDecoder.cpp"

check_contains "DmaBufInfo in VideoFrame"  "src/infrastructure/media/IHardwareDecoder.h" "DmaBufInfo"
check_contains "PixelFormat NV12"          "src/infrastructure/media/IHardwareDecoder.h" "NV12"
check_contains "DRM Prime export in VAAPI" "src/infrastructure/media/VAAPIDecoder.cpp"   "DRM_PRIME"
check_contains "Zero-copy EGL DMA-BUF"    "src/infrastructure/media/gpu/EGLDmaBufInterop.cpp" "eglCreateImage"
check_contains "GPU interop in SGNode"     "src/presentation/renderers/VideoSGNode.h"    "IGpuInterop"
check_contains "GPU interop in Renderer"   "src/presentation/renderers/VideoRenderer.h"  "IGpuInterop"
check_contains "NV12 uniform in shader"    "shaders/video.frag"                          "isNv12"
check_contains "NV12 branch in shader"     "shaders/video.frag"                          "isNv12 == 1"
check_contains "GpuInteropFactory create"  "src/presentation/renderers/VideoRenderer.cpp" "GpuInteropFactory"
check_contains "NVDEC CUDA device"         "src/infrastructure/media/NvdecDecoder.cpp"   "AV_HWDEVICE_TYPE_CUDA"

# ─── Summary ─────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  总计: ${TOTAL} 项检查"
echo "  通过: ${PASS} ✅"
echo "  失败: ${FAIL} ❌"
echo "════════════════════════════════════════════════════════════"
echo ""

if [[ ${FAIL} -gt 0 ]]; then
    echo "⚠️  架构验证失败：${FAIL} 项缺失"
    exit 1
else
    echo "🎉  架构验证通过！所有 ${PASS} 项检查均满足"
    exit 0
fi
