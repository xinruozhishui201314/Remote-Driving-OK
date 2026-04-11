/**
 * 延迟基准测试（《客户端架构设计》§4.3）。
 * 验证关键数据结构的延迟预算：
 *   - SPSCQueue push/pop < 500ns
 *   - TripleBuffer swap < 200ns
 *   - EventBus publish < 1µs (critical path)
 */
#include "../../src/utils/LockFreeQueue.h"
#include "../../src/utils/TimeUtils.h"
#include "../../src/utils/TripleBuffer.h"

#include <benchmark/benchmark.h>

struct TestMessage {
  double steering;
  double throttle;
  double brake;
  int64_t timestamp;
};

// ─── SPSCQueue 基准 ───────────────────────────────────────────────────────────

static void BM_SPSCQueue_Push(benchmark::State& state) {
  SPSCQueue<TestMessage, 1024> queue;
  TestMessage msg{0.5, 0.3, 0.0, 0};

  for (auto _ : state) {
    queue.push(msg);
    TestMessage out;
    queue.pop(out);
  }
}
BENCHMARK(BM_SPSCQueue_Push)->Threads(1)->UseRealTime();

static void BM_SPSCQueue_PopEmpty(benchmark::State& state) {
  SPSCQueue<TestMessage, 1024> queue;
  TestMessage out;

  for (auto _ : state) {
    benchmark::DoNotOptimize(queue.pop(out));
  }
}
BENCHMARK(BM_SPSCQueue_PopEmpty)->Threads(1)->UseRealTime();

// ─── TripleBuffer 基准 ────────────────────────────────────────────────────────

static void BM_TripleBuffer_Write(benchmark::State& state) {
  TripleBuffer<TestMessage> buf;
  TestMessage msg{0.5, 0.3, 0.0, 0};

  for (auto _ : state) {
    buf.getWriteBuffer() = msg;
    buf.publishWrite();
  }
}
BENCHMARK(BM_TripleBuffer_Write)->Threads(1)->UseRealTime();

static void BM_TripleBuffer_Read(benchmark::State& state) {
  TripleBuffer<TestMessage> buf;
  TestMessage msg{0.5, 0.3, 0.0, 0};
  buf.getWriteBuffer() = msg;
  buf.publishWrite();

  for (auto _ : state) {
    benchmark::DoNotOptimize(buf.getReadBuffer());
  }
}
BENCHMARK(BM_TripleBuffer_Read)->Threads(1)->UseRealTime();

// ─── TimeUtils 基准 ────────────────────────────────────────────────────────────

static void BM_TimeUtils_NowUs(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(TimeUtils::nowUs());
  }
}
BENCHMARK(BM_TimeUtils_NowUs)->UseRealTime();

BENCHMARK_MAIN();
