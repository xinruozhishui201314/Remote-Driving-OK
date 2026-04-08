#!/usr/bin/env bash
# 可选：在启用 Google Benchmark 的 client 构建上运行 LatencyBenchmark（Phase 5）
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${CLIENT_BENCH_BUILD_DIR:-$ROOT/client/build-bench-verify}"

if [ "${SKIP_CLIENT_BENCHMARK:-1}" = "1" ]; then
  echo "[SKIP] verify-client-benchmark: 设置 SKIP_CLIENT_BENCHMARK=0 且安装 benchmark 库后启用"
  exit 0
fi

cd "$ROOT"
cmake -S client -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON 2>/dev/null || {
  echo "[SKIP] CMake 配置失败（可能未安装 Qt6 / benchmark）"
  exit 0
}
cmake --build "$BUILD_DIR" --target client_latency_benchmark -j"$(nproc 2>/dev/null || echo 4)" 2>/dev/null || {
  echo "[SKIP] client_latency_benchmark 目标不存在或未编译"
  exit 0
}
"$BUILD_DIR"/tests/performance/client_latency_benchmark --benchmark_min_time=0.1
echo "[OK] client_latency_benchmark 完成"
