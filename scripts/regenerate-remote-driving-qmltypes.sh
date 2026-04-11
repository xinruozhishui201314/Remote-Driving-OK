#!/usr/bin/env bash
# 兼容入口：请优先使用 regenerate-client-qmltypes.sh（同时刷新 remote-driving-cpp + driving-facade）。
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/regenerate-client-qmltypes.sh" "$@"
