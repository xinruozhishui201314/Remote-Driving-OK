#!/bin/bash
# FIX_REMOTE_DRIVING_VIDEO_STREAKS.sh
# 彻底修复远驾视频流水平条纹（Streaks）的全链路环境配置方案

echo "--------------------------------------------------------"
echo "Remote Driving Video Pipeline - Streak Fix Configuration"
echo "--------------------------------------------------------"

# 1. 编码端配置 (Carla-Bridge / Pushing Side)
# 彻底消除条纹的根因：强制单切片编码 (Single Slice)
export CARLA_X264_SLICES=1
export VIDEO_BITRATE_KBPS=2000
export VIDEO_FPS=20

echo "[Encoder] Enforced CARLA_X264_SLICES=1 (Single Slice Encoding)"
echo "[Encoder] Target Bitrate: ${VIDEO_BITRATE_KBPS}kbps"

# 2. 客户端配置 (Client / Decoding Side)
# 双重保险：强制单线程解码，确保即使收到多 slice 流也不会产生同步带状伪影
export CLIENT_FFMPEG_DECODE_THREADS=1

# 额外优化：如果是在 Mesa 软件渲染环境下，强制 RGBA 契约以规避 Stride Bug
export CLIENT_VIDEO_CPU_PRESENT_FORMAT_STRICT=1

# 3. 取证与指纹校验 (Forensics & Verification)
# 开启端到端指纹追踪，用于验证像素数据一致性 (DECODE_OUT vs SG_UPLOAD)
export CLIENT_VIDEO_FORENSICS=1
# (可选) 将异常帧保存为 PNG，用于视觉核对
# export CLIENT_VIDEO_SAVE_FRAME=png

echo "[Decoder] Enforced CLIENT_FFMPEG_DECODE_THREADS=1 (Safe Sequential Decoding)"
echo "[Decoder] Enforced Format Strictness for Software GL Compatibility"
echo "[Verify]  Forensics MODE ENABLED: rowHash checksum validation active"

echo "--------------------------------------------------------"
echo "环境配置已准备就绪。请在此 Shell 中启动程序，或将上述变量加入 Dockerfile/systemd 配置。"
echo "--------------------------------------------------------"
