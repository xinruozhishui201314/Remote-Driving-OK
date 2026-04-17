#!/usr/bin/env python3
"""
从客户端日志中判定「四路视频是否均有解码/呈现活动」。

依据 WebRtcStreamManager 每秒输出的行：
  [Client][VideoPresent][1Hz] ... Fr{nX,...} Re{...} Le{...} Ri{...}
其中 n = 该秒内 QVideoSink::setVideoFrame 成功次数，dE = 解码线程 emit 次数。
任一路在某一秒内 n>0 或 dE>0 即视为该路「有视频数据在动」。

用法:
  python3 scripts/lib/verify_four_view_client_log.py /path/to/client.log
  cat client.log | python3 scripts/lib/verify_four_view_client_log.py -
"""
from __future__ import annotations

import re
import sys


def arm_segment_ok(inner: str) -> bool:
    """inner 为 Fr{ 与 } 之间的内容。"""
    m = re.search(r"n(\d+),", inner)
    if m and int(m.group(1), 10) > 0:
        return True
    m = re.search(r"dE(\d+)", inner)
    if m and int(m.group(1), 10) > 0:
        return True
    return False


def line_has_four_arms_ok(line: str) -> bool:
    if "[Client][VideoPresent][1Hz]" not in line or "libgl_sw=" not in line:
        return False
    for abbr in ("Fr", "Re", "Le", "Ri"):
        m = re.search(rf"(?<![A-Za-z]){abbr}" + r"\{([^}]*)\}", line)
        if not m:
            return False
        if not arm_segment_ok(m.group(1)):
            return False
    return True


def main() -> int:
    if len(sys.argv) != 2:
        print("用法: verify_four_view_client_log.py <logfile|->", file=sys.stderr)
        return 2
    path = sys.argv[1]
    if path == "-":
        text = sys.stdin.read()
    else:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()

    matching = [ln for ln in text.splitlines() if line_has_four_arms_ok(ln)]
    if not matching:
        print(
            "[verify_four-view] FAIL: 未找到任一 [Client][VideoPresent][1Hz] 行，"
            "使 Fr/Re/Le/Ri 同时满足 n>0 或 dE>0",
            file=sys.stderr,
        )
        print(
            "[verify_four-view] 提示: 确认 ZLM 上四路在推流、VIN 与 CLIENT_AUTO_CONNECT_TEST_VIN 一致、"
            "ZLM_VIDEO_URL 在容器内可访问；适当增大 RUN_TIMEOUT。",
            file=sys.stderr,
        )
        return 1

    print("[verify_four-view] OK: 四路在同一 1s 窗口内均有呈现/解码计数")
    print("[verify_four-view] 样例行:", matching[-1][:240] + ("…" if len(matching[-1]) > 240 else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
