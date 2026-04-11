#!/usr/bin/env bash
# 从单次客户端日志中，按「链条上第一个异常」收敛视频黑屏/花屏/条状伪影的根因（辅助人工判决）。
#
# 用法:
#   ./scripts/analyze-video-pipeline-log.sh /path/to/client-*.log
#
# 复现前建议环境变量（一次会话即可）:
#   export CLIENT_VIDEO_EVIDENCE_CHAIN=1
#   （可选）export CLIENT_VIDEO_EVIDENCE_EARLY_MAX=30
#   （可选）export CLIENT_VIDEO_EVIDENCE_INTERVAL=120   # 0=仅早期帧
#   （可选）export CLIENT_VIDEO_EVIDENCE_STRIPE_ROWS=1   # 0=仅两行采样（旧哈希）
#   export CLIENT_VIDEO_PRESENT_TRACE=1
#   export CLIENT_MAIN_THREAD_STALL_DIAG=1
#   export CLIENT_VIDEO_FRAME_INTERVAL_TRACE=1
#   （可选）export CLIENT_WEBRTC_LOG_EVERY_RTP_CAM_FRONT=1   # 仅短时排障，日志极大
#
# 车端/推流侧需另开终端对照:
#   docker logs <carla-bridge 容器名> 2>&1 | tail -200   # 看 ffmpeg frame=/drop=

set -uo pipefail

LOG="${1:-}"
if [[ -z "$LOG" || ! -f "$LOG" ]]; then
  echo "用法: $0 <client-log-file>"
  exit 2
fi

echo "=========================================="
echo "视频链路根因分析（单日志文件）"
echo "文件: $LOG"
echo "=========================================="

count() { grep -c "$1" "$LOG" 2>/dev/null || true; }

echo ""
echo "--- A. 运行环境与 GL（软渲染放大卡顿/纹理压力，条状需结合证据链） ---"
grep -E "\[Client\]\[PlatformDiag\]|\[Client\]\[GLProbe\]|LIBGL_ALWAYS_SOFTWARE" "$LOG" | head -8 || true

echo ""
echo "--- B. 建链与解码器创建（无则信令/ZLM/未进远驾） ---"
grep -E "connectToStream 进入|setupVideoDecoder stream=" "$LOG" | head -12 || echo "  (无) → 未进入拉流或未打到该日志级别"

echo ""
echo "--- C. RTP 入站与入环丢弃（首因候选：预算/环满 → 丢包感/花屏） ---"
echo "  [RTP-Arrival] 行数: $(count '\[Client\]\[WebRTC\]\[RTP-Arrival\]')"
grep -E "\[Client\]\[Media\]\[Budget\]|\[Client\]\[Media\]\[Ring\]" "$LOG" | head -15 || echo "  (无 Budget/Ring 告警) → 入环丢弃不显著或未触发"

echo ""
echo "--- D. H264 解包/组装（FU-A、PT、乱序） ---"
grep -E "\[H264\]\[payload\]|\[H264\]\[WARN\]|droppedWrongPt|seqJumpResync|FU-A" "$LOG" | head -20 || echo "  (无显著 payload 告警)"

echo ""
echo "--- E. 解码输出与呈现路径 ---"
if grep -q '\[Client\]\[WebRTC\]\[Present\] first frame' "$LOG" 2>/dev/null; then
  grep -E "\[H264\].*emit frameReady|\[Client\]\[WebRTC\]\[Present\] first frame|\[Client\]\[VideoPresent\]\[Path\]" "$LOG" | head -25
else
  echo "  (无 first frame 行) → 未到首帧或日志被截断"
fi

echo ""
echo "--- F. 端到端像素证据链（需 CLIENT_VIDEO_EVIDENCE_CHAIN=1） ---"
EV=$(count '\[Client\]\[VideoEvidence\]')
echo "  [VideoEvidence] 行数: $EV"
if [[ "$EV" == "0" ]]; then
  echo "  ★ 未出现 VideoEvidence：请用 CLIENT_VIDEO_EVIDENCE_CHAIN=1 重跑同场景，否则无法对比 DECODE_OUT vs SG_UPLOAD"
else
  grep '\[Client\]\[VideoEvidence\]' "$LOG" | head -40
  echo "  …"
  echo "  判定提示: 同一 stream + 同一 fid 下 rowHash/bpl/stripe 应对齐；若 DECODE_OUT 已异常 → 编解码/RTP 域；若仅 SG 异常 → SceneGraph/纹理/几何"
fi

echo ""
echo "--- G. Scene Graph / 纹理 ---"
grep -E "createTextureFromImage returned null|sceneGraphError|\[Client\]\[UI\]\[RemoteVideoSurface\].*exception" "$LOG" | head -10 || echo "  (无显式纹理失败)"

echo ""
echo "--- H. UI 遮挡（黑屏但解码可能正常） ---"
grep -E "\[Client\]\[UI\]\[Video\]\[Placeholder\]|\[Client\]\[UI\]\[Video\]\[Overlay\]|盖住视频" "$LOG" | head -12 || echo "  (无)"

echo ""
echo "--- I. 主线程积压（与卡顿/排队相关） ---"
grep -E "\[SysStall\]|\[Client\]\[VideoPresent\]\[QueuedLag\]|\[Client\]\[WebRTC\]\[MainThreadBudget\]" "$LOG" | head -12 || echo "  (无或未开 STALL_DIAG)"

echo ""
echo "--- J. H264 每秒统计（丢 IDR、needKeyframe、乱序） ---"
grep '\[H264\]\[Stats\]' "$LOG" | tail -6 || echo "  (无 Stats 行) → 会话太短或未跨 1s 窗"

echo ""
echo "=========================================="
echo "启发式结论（必须结合画面与时间轴人工确认）"
echo "=========================================="

has_decoder=$(grep -q "setupVideoDecoder stream=" "$LOG" && echo yes || echo no)
has_rtp=$(grep -q '\[Client\]\[WebRTC\]\[RTP-Arrival\]' "$LOG" && echo yes || echo no)
has_first=$(grep -q '\[Client\]\[WebRTC\]\[Present\] first frame' "$LOG" && echo yes || echo no)
has_path=$(grep -q 'branch= remote_surface' "$LOG" && echo yes || echo no)
has_budget=$(grep -q '\[Client\]\[Media\]\[Budget\]' "$LOG" && echo yes || echo no)
has_ring=$(grep -q '\[Client\]\[Media\]\[Ring\]' "$LOG" && echo yes || echo no)
has_bpl_warn=$(grep -q 'BGR0 stride 异常' "$LOG" && echo yes || echo no)
has_tex_null=$(grep -q 'createTextureFromImage returned null' "$LOG" && echo yes || echo no)

if [[ "$has_decoder" == "no" ]]; then
  echo "→ 优先查: 未创建解码器（信令、onTrack、是否进入远驾、stream 名）。"
elif [[ "$has_rtp" == "no" ]]; then
  echo "→ 优先查: 无 RTP 到达（ICE、ZLM 流、网络、PeerConnection 状态）。"
elif [[ "$has_budget" == "yes" || "$has_ring" == "yes" ]]; then
  echo "→ 优先查: 入环前丢弃（媒体预算或 SPSC 满）→ 码流不完整 → 花屏/条状；对照降码率/ CAMERA_FPS / 预算 env。"
elif [[ "$has_bpl_warn" == "yes" ]]; then
  echo "→ 优先查: 解码后 QImage stride 异常（色彩转换/帧池/宽度不一致）。"
elif [[ "$has_first" == "no" ]]; then
  echo "→ 优先查: 有 RTP 但无首帧（关键帧、FFmpeg 打开、needKeyframe、SPS/PPS）。"
elif [[ "$has_path" == "no" ]]; then
  echo "→ 优先查: 未走 remote_surface（bindVideoSurface、与 VideoOutput 互斥、指针）。"
elif [[ "$has_tex_null" == "yes" ]]; then
  echo "→ 优先查: createTextureFromImage 失败（格式、GL、尺寸、驱动）。"
elif [[ "$EV" != "0" ]]; then
  echo "→ 用 VideoEvidence 逐路对比 DECODE_OUT 与 SG_UPLOAD；一致则疑 UI/遮挡/缩放，不一致则断在对应 stage。"
else
  echo "→ 建议开启 CLIENT_VIDEO_EVIDENCE_CHAIN=1 重跑后再次执行本脚本；当前仅能依赖 Path/Stats/占位层日志。"
fi

echo ""
echo "完成。若需车端对照: docker logs <carla-bridge> 2>&1 | grep -E 'frame=|drop=' | tail -30"
exit 0
