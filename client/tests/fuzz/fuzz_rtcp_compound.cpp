/**
 * V2：libFuzzer 入口，针对 RTCP compound 解析（无网络 I/O）。
 * 构建（需 Clang）：
 *   cmake -DENABLE_CLIENT_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ ...
 *   cmake --build . --target fuzz_rtcp_compound
 * 运行：
 *   ./fuzz_rtcp_compound -runs=10000
 */
#include "media/RtcpCompoundParser.h"
#include "media/RtpStreamClockContext.h"

#include <QString>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  RtpStreamClockContext ctx;
  QString log;
  (void)rtcpCompoundTryConsumeAndUpdateClock(data, size, &ctx, 0, 0, &log);
  return 0;
}
