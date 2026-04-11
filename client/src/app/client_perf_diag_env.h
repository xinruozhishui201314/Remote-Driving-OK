#pragma once

/**
 * 深度排障 + SLO/可观测性一键环境变量（须在 QGuiApplication 之前调用）。
 *
 * CLIENT_PERF_DIAG=1 时，若下列变量「未设置」（qgetenv 为空），则写入默认值：
 *   - CLIENT_VIDEO_PRESENT_1HZ_SUMMARY=1
 *   - CLIENT_MAIN_THREAD_STALL_DIAG=1
 *   - CLIENT_VIDEO_SCENE_GL_LOG=1
 *   - CLIENT_VIDEO_PERF_JSON_1HZ=1        — 每秒一行 JSON（[Client][VideoPerf][JSON]）
 *   - CLIENT_VIDEO_PERF_METRICS_1HZ=1     — 写入 MetricsCollector（/metrics 同源 singleton）
 *   - CLIENT_VIDEO_PERF_TRACE_SPAN=1      — 每秒 OpenTelemetry 风格 Span（含标签）
 *   - CLIENT_VIDEO_PERF_RUSAGE_1HZ=1      — JSON 内附本进程 getrusage 增量（Unix）
 *
 * 编解码健康（默认开启，与 H264Decoder 1s 窗一致）：
 *   - CLIENT_CODEC_HEALTH_1HZ=0           — 关闭每秒 [Client][CodecHealth][1Hz] 摘要行
 *
 * 显式设置过子变量时不会被覆盖（便于生产关闭某一项）。
 */
namespace ClientPerfDiagEnv {

void applyBeforeQGuiApplication();

}  // namespace ClientPerfDiagEnv
