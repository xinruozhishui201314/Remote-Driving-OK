#!/usr/bin/env python3
"""
载入模块间交互记录（NDJSON），按时间/模块/主题/VIN 过滤，输出时间线便于排查。

用法:
  ./scripts/analyze_interaction_log.py --dir ./recordings
  ./scripts/analyze_interaction_log.py --dir ./recordings --since "2026-02-10T12:00:00" --until "2026-02-10T12:05:00"
  ./scripts/analyze_interaction_log.py --dir ./recordings --module carla-bridge --topic "vehicle/control" --vin carla-sim-001 --out timeline.txt
"""

import argparse
import json
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import List, Optional


def parse_ts(ts_str: str) -> Optional[datetime]:
    if not ts_str:
        return None
    try:
        if ts_str.endswith("Z"):
            ts_str = ts_str[:-1] + "+00:00"
        return datetime.fromisoformat(ts_str.replace("Z", "+00:00"))
    except Exception:
        return None


def load_records(dir_path: str) -> List[dict]:
    records = []
    p = Path(dir_path)
    if not p.is_dir():
        return records
    for f in p.glob("*.jsonl"):
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fp:
                for line in fp:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        records.append(json.loads(line))
                    except json.JSONDecodeError:
                        continue
        except OSError:
            continue
    return records


def filter_records(
    records: List[dict],
    since: Optional[datetime] = None,
    until: Optional[datetime] = None,
    module: Optional[str] = None,
    topic: Optional[str] = None,
    vin: Optional[str] = None,
) -> List[dict]:
    out = []
    for r in records:
        ts = parse_ts(r.get("ts") or "")
        if since and ts and ts < since:
            continue
        if until and ts and ts > until:
            continue
        if module and (r.get("module") or "") != module:
            continue
        if topic and (r.get("topic_or_path") or "").find(topic) < 0:
            continue
        if vin and (r.get("vin") or "") != vin:
            continue
        out.append(r)
    return out


def sort_records(records: List[dict]) -> List[dict]:
    def key_fn(r):
        ts = parse_ts(r.get("ts") or "")
        return (ts or datetime.min).timestamp()

    return sorted(records, key=key_fn)


def format_line(r: dict) -> str:
    ts = r.get("ts") or ""
    mod = r.get("module") or ""
    direction = r.get("direction") or ""
    peer = r.get("peer") or ""
    topic = r.get("topic_or_path") or ""
    summary = (r.get("payload_summary") or "")[:80]
    vin = r.get("vin") or ""
    err = r.get("error") or ""
    parts = [ts, mod, direction, peer, topic]
    if summary:
        parts.append(summary)
    if vin:
        parts.append(f"vin={vin}")
    if err:
        parts.append(f"error={err}")
    return " | ".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyze interaction NDJSON logs")
    ap.add_argument("--dir", default="./recordings", help="Directory containing *.jsonl files")
    ap.add_argument("--since", help="ISO8601 start time (inclusive)")
    ap.add_argument("--until", help="ISO8601 end time (inclusive)")
    ap.add_argument("--module", help="Filter by module name")
    ap.add_argument("--topic", help="Filter by topic_or_path (substring)")
    ap.add_argument("--vin", help="Filter by VIN")
    ap.add_argument("--out", help="Write timeline to file (default: stdout)")
    args = ap.parse_args()

    since_dt = parse_ts(args.since) if args.since else None
    until_dt = parse_ts(args.until) if args.until else None

    records = load_records(args.dir)
    if not records:
        print("No records found in", args.dir, file=sys.stderr)
        return 0

    filtered = filter_records(
        records,
        since=since_dt,
        until=until_dt,
        module=args.module,
        topic=args.topic,
        vin=args.vin,
    )
    sorted_records = sort_records(filtered)

    lines = [format_line(r) for r in sorted_records]
    out_text = "\n".join(lines)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(out_text)
        print(f"Wrote {len(lines)} lines to {args.out}", file=sys.stderr)
    else:
        print(out_text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
