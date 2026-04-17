#!/usr/bin/env bash
# 自动化探测：Docker 镜像名「省略端口」时默认走 https://HOST:443/v2/；
# 若 Registry（Harbor）仅在 HTTP/80 提供 /v2/，则 docker push/pull 会出现 EOF / Unavailable 等
# 症状。本脚本用 curl 对比 443 与 80 的 /v2/ 可达性与 Registry 响应头，失败时打印根因与修复建议。
#
# 用法：
#   REGISTRY_PROBE_HOST=192.168.2.10 ./scripts/verify-registry-docker-port-contract.sh
# 可选环境变量：
#   REGISTRY_HTTPS_PORT   默认 443
#   REGISTRY_HTTP_PORT    默认 80
#   CURL_CONNECT_TIMEOUT  默认 3（秒）
#   CURL_MAX_TIME         默认 8（秒）
#   REGISTRY_PROBE_STRICT_IMPLICIT_443 默认 1：仅 HTTP 可达且 HTTPS 不可用时 exit 2（门禁）。
#                         设为 0 时同场景仅打印 [WARN] 并 exit 0（仅表示「探测完成、Registry 在 HTTP 上活着」）。
#
# 退出码：0 通过；1 通用失败（不可达/ambiguous）；2 已识别「仅 HTTP/80」与 Docker 默认 443 冲突（根因类，仅 strict=1）

set -euo pipefail

HOST="${REGISTRY_PROBE_HOST:-}"
HTTPS_PORT="${REGISTRY_HTTPS_PORT:-443}"
HTTP_PORT="${REGISTRY_HTTP_PORT:-80}"
CTO="${CURL_CONNECT_TIMEOUT:-3}"
MTO="${CURL_MAX_TIME:-8}"

if [[ "${1:-}" == "--selftest" ]]; then
  if ! bash -n "$0"; then
    echo "[FAIL] bash -n 语法检查未通过" >&2
    exit 1
  fi
  if ! REGISTRY_PROBE_HOST= bash "$0" >/dev/null 2>&1; then
    echo "[FAIL] 未设置 REGISTRY_PROBE_HOST 时应 SKIP(exit 0)" >&2
    exit 1
  fi
  if REGISTRY_PROBE_HOST='192.168.2.10:80' bash "$0" >/dev/null 2>&1; then
    echo "[FAIL] 含端口的 REGISTRY_PROBE_HOST 应被拒绝(exit 非0)" >&2
    exit 1
  fi
  echo "[OK] --selftest 通过（语法 + SKIP + 拒绝非法 HOST）"
  exit 0
fi

if [[ -z "$HOST" ]]; then
  echo "[SKIP] verify-registry-docker-port-contract: 未设置 REGISTRY_PROBE_HOST（示例: REGISTRY_PROBE_HOST=192.168.2.10 $0）" >&2
  exit 0
fi

if [[ "$HOST" == *:* ]]; then
  echo "[FAIL] REGISTRY_PROBE_HOST 不得包含端口或方括号，请只填主机名或 IP（当前: $HOST）" >&2
  exit 1
fi

hdr_https="$(mktemp)"
hdr_http="$(mktemp)"
err_https="$(mktemp)"
err_http="$(mktemp)"
cleanup() {
  rm -f "$hdr_https" "$hdr_http" "$err_https" "$err_http"
}
trap cleanup EXIT

set +e
code_https="$(curl -g -skS -I --connect-timeout "$CTO" --max-time "$MTO" \
  -o "$hdr_https" -w '%{http_code}' "https://${HOST}:${HTTPS_PORT}/v2/" 2>"$err_https")"
ec_https=$?
code_http="$(curl -g -sS -I --connect-timeout "$CTO" --max-time "$MTO" \
  -o "$hdr_http" -w '%{http_code}' "http://${HOST}:${HTTP_PORT}/v2/" 2>"$err_http")"
ec_http=$?
set -e

registry_like() {
  local f=$1
  [[ -s "$f" ]] && grep -qi 'Docker-Distribution-Api-Version' "$f"
}

https_err_msg() {
  if [[ -s "$err_https" ]]; then
    head -n 3 "$err_https"
  fi
}

echo "=== Registry / Docker 端口契约探测 ==="
echo "  HOST=$HOST  HTTPS_PORT=$HTTPS_PORT  HTTP_PORT=$HTTP_PORT"
echo "  https: curl exit=$ec_https http_code=${code_https:-}"
https_err_msg | sed 's/^/  https stderr: /' || true
if [[ -s "$hdr_https" ]]; then
  grep -i 'Docker-Distribution-Api-Version\|^HTTP/\|Www-Authenticate' "$hdr_https" | head -n 6 | sed 's/^/  https header: /' || true
fi
echo "  http:  curl exit=$ec_http http_code=${code_http:-}"
if [[ -s "$err_http" ]]; then
  head -n 3 "$err_http" | sed 's/^/  http stderr: /' || true
fi
if [[ -s "$hdr_http" ]]; then
  grep -i 'Docker-Distribution-Api-Version\|^HTTP/\|Www-Authenticate' "$hdr_http" | head -n 6 | sed 's/^/  http header: /' || true
fi

https_registry=false
if [[ "$ec_https" -eq 0 ]] && registry_like "$hdr_https"; then
  https_registry=true
fi

http_registry=false
if [[ "$ec_http" -eq 0 ]] && registry_like "$hdr_http"; then
  http_registry=true
fi

# 443 无服务：curl exit 7/28；或写入了 http_code 000（无有效 HTTP 响应）
https_transport_dead=false
if [[ "$ec_https" -eq 7 ]] || [[ "$ec_https" -eq 28 ]]; then
  https_transport_dead=true
fi
if [[ "${code_https:-}" == "000" ]] || [[ -z "${code_https:-}" ]]; then
  https_transport_dead=true
fi
# 偶发：TLS 对端立即关连接，ec 可能非 7；无 Api-Version 头且 http 侧 Registry 正常时仍判为冲突
if [[ "$ec_https" -ne 0 ]] && ! registry_like "$hdr_https"; then
  if [[ "$ec_http" -eq 0 ]] && registry_like "$hdr_http"; then
    https_transport_dead=true
  fi
fi

if [[ "$http_registry" == false ]]; then
  echo "[FAIL] http://${HOST}:${HTTP_PORT}/v2/ 未返回 Registry v2 特征头（Docker-Distribution-Api-Version）。请检查 Harbor/nginx 或端口。" >&2
  exit 1
fi

if [[ "$https_registry" == true ]]; then
  echo "[OK] HTTPS:${HTTPS_PORT} 上存在 Registry v2；镜像名使用 ${HOST}/project/repo 时与默认 443 一致。"
  exit 0
fi

if [[ "$https_transport_dead" == true ]] || [[ "$ec_https" -ne 0 ]]; then
  echo "[ROOT_CAUSE] Docker 对镜像引用「${HOST}/<project>/<repo>」（无显式端口）会请求 https://${HOST}:${HTTPS_PORT}/v2/ 。" >&2
  echo "             探测显示该 HTTPS 端点不可用（curl exit=$ec_https），而 HTTP:${HTTP_PORT} 上 /v2/ 为合法 Registry。" >&2
  echo "[FIX] 在 tag/pull/push 与编排清单中统一使用「${HOST}:${HTTP_PORT}/<project>/<repo>:<tag>」；" >&2
  echo "      或为该主机配置 443 TLS 终止并使 /v2/ 与 token realm 对外一致为 https。" >&2
  if [[ "${REGISTRY_PROBE_STRICT_IMPLICIT_443:-1}" == "1" ]]; then
    exit 2
  fi
  echo "[WARN] REGISTRY_PROBE_STRICT_IMPLICIT_443=0：不将本场景视为失败退出（镜像引用若仍无端口号，push 仍可能 EOF）。" >&2
  exit 0
fi

echo "[FAIL] 无法归类：HTTPS 未返回 Registry 头但传输未判为死（curl exit=$ec_https）。请人工查看上述输出。" >&2
exit 1
