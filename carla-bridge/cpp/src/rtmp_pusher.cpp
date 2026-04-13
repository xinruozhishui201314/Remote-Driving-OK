#include "rtmp_pusher.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <iostream>
#include <sstream>
#include <array>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <cctype>
#include <cstdlib>

#ifdef __linux__
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <spawn.h>
#include <errno.h>
#endif

namespace carla_bridge {

static std::string logTag(const std::string& streamId) {
  return "[CARLA-Bridge][ZLM][Push][" + streamId + "]";
}

static bool teleopPreEncodeSnapshotDisabled() {
  const char* e = std::getenv("TELEOP_VIDEO_SNAPSHOT_PRE_ENCODE");
  if (!e || !*e) return false;
  std::string v(e);
  for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return v == "0" || v == "false" || v == "no" || v == "off";
}

static bool teleopPreEncodeSnapshotEnabled() {
  return !teleopPreEncodeSnapshotDisabled();
}

static int teleopSnapshotIntervalSec() {
  const char* s = std::getenv("TELEOP_VIDEO_SNAPSHOT_INTERVAL_SEC");
  if (!s || !*s) return 10;
  int v = std::atoi(s);
  return v > 0 ? v : 10;
}

static std::string teleopLogRoot() {
  const char* explicitRoot = std::getenv("TELEOP_LOG_ROOT");
  if (explicitRoot && *explicitRoot) return std::string(explicitRoot);
  const char* runId = std::getenv("TELEOP_RUN_ID");
  if (runId && *runId) {
    namespace fs = std::filesystem;
    const fs::path base("/workspace/logs");
    if (fs::exists(base) && fs::is_directory(base)) {
      return (base / runId).string();
    }
  }
  return "/workspace/logs";
}

static std::string utcTimestampForFilename() {
  using clock = std::chrono::system_clock;
  const std::time_t t = clock::to_time_t(clock::now());
  std::tm tm{};
#if defined(_WIN32)
  if (gmtime_s(&tm, &t) != 0)
    return "badtime";
#else
  if (!gmtime_r(&t, &tm))
    return "badtime";
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
  return std::string(buf);
}

static std::string sanitizeStreamIdForPath(const std::string& id) {
  std::string o;
  o.reserve(id.size());
  for (char c : id) {
    if (c == '/' || c == '\\') o.push_back('_');
    else o.push_back(c);
  }
  return o;
}

/** 将 packed BGR（wxh*3）写成 P6 PPM（RGB），与 Python 桥 pre_encode 一致。 */
static bool writeBgrPackedAsPpm(const std::string& path, const uint8_t* bgr, int w, int h) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << "P6\n" << w << " " << h << "\n255\n";
  std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
  const size_t rowBGR = static_cast<size_t>(w) * 3;
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = bgr + static_cast<size_t>(y) * rowBGR;
    for (int x = 0; x < w; ++x) {
      row[static_cast<size_t>(x) * 3 + 0] = src[x * 3 + 2];
      row[static_cast<size_t>(x) * 3 + 1] = src[x * 3 + 1];
      row[static_cast<size_t>(x) * 3 + 2] = src[x * 3 + 0];
    }
    out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
  }
  return static_cast<bool>(out);
}

RtmpPusher::RtmpPusher(const std::string& streamId, const std::string& rtmpUrl,
                       int width, int height, int fps)
    : m_streamId(streamId), m_rtmpUrl(rtmpUrl),
      m_width(width), m_height(height), m_fps(fps) {}

RtmpPusher::~RtmpPusher() {
  stop();
}

void RtmpPusher::start() {
  if (m_running.exchange(true)) return;
  m_useTestPattern = false;
  m_lastPreEncodeSnapshot = std::chrono::steady_clock::now();
  m_writerThread = std::make_unique<std::thread>(&RtmpPusher::writerLoop, this);
}

void RtmpPusher::startTestPattern() {
  if (m_running.exchange(true)) return;
  m_useTestPattern = true;
  m_writerThread = std::make_unique<std::thread>([this]() {
#ifdef __linux__
    pid_t pid = fork();
    if (pid == 0) {
      std::string optSize = std::to_string(m_width) + "x" + std::to_string(m_height);
      std::string optRate = std::to_string(m_fps);
      std::string optG = std::to_string(m_fps);
      std::string optI = "testsrc=size=" + optSize + ":rate=" + optRate;

      // 读取码率和切片环境变量，与 writerLoop/pushFrame 路径保持一致
      int bitrate_kbps = 2000;
      int slices = 1;
      if (const char* br_env = std::getenv("VIDEO_BITRATE_KBPS")) {
        try { bitrate_kbps = std::max(1, std::stoi(br_env)); } catch (...) {}
      }
      if (const char* sl_env = std::getenv("CARLA_X264_SLICES")) {
        try {
          int val = std::stoi(sl_env);
          if (val > 1) {
            std::cerr << logTag(m_streamId) << " [WARNING] CARLA_X264_SLICES=" << val
                      << " 多 slice 增加每帧 RTP 包数、恶化 NACK，强制为 1" << std::endl;
            slices = 1;
          } else {
            slices = std::max(1, val);
          }
        } catch (...) {
          slices = 1;
        }
      }
      int bufsize_kbps = bitrate_kbps * 2;
      std::string x264_params = "slices=" + std::to_string(slices);

      execl("/usr/bin/ffmpeg", "ffmpeg", "-y", "-f", "lavfi", "-i", optI.c_str(),
            "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
            "-profile:v", "baseline", "-level", "3.0",
            "-b:v", (std::to_string(bitrate_kbps) + "k").c_str(),
            "-maxrate", (std::to_string(bitrate_kbps) + "k").c_str(),
            "-bufsize", (std::to_string(bufsize_kbps) + "k").c_str(),
            "-pix_fmt", "yuv420p", "-g", optG.c_str(), "-keyint_min", optG.c_str(),
            "-x264-params", x264_params.c_str(),
            "-f", "flv", m_rtmpUrl.c_str(),
            static_cast<char*>(nullptr));
      _exit(127);
    }
    if (pid > 0) {
      {
        std::lock_guard<std::mutex> lock(m_testPidMutex);
        m_testPatternPid = static_cast<int>(pid);
      }
      std::cout << logTag(m_streamId) << " 启动 testsrc 推流 -> " << m_rtmpUrl << std::endl;
      int status = 0;
      while (m_running.load()) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
          if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == 127)
              std::cerr << logTag(m_streamId) << " 环节: ffmpeg 未找到或启动失败 exit=" << code << "，请确认容器内 /usr/bin/ffmpeg 存在" << std::endl;
            else if (code != 0)
              std::cerr << logTag(m_streamId) << " 环节: ffmpeg 子进程退出 exit=" << code << "（非 0 表示推流异常，如 RTMP 连接失败）" << std::endl;
          }
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      {
        std::lock_guard<std::mutex> lock(m_testPidMutex);
        m_testPatternPid = -1;
      }
      if (m_running.load())
        waitpid(pid, &status, 0);
    } else if (pid < 0) {
      std::cerr << logTag(m_streamId) << " fork 失败 errno=" << errno << std::endl;
    }
#endif
    m_running = false;
  });
}

void RtmpPusher::writerLoop() {
  std::ostringstream oss;
  
  // ─ 读取编码参数环境变量（与 Python carla_bridge.py 对齐）
  int bitrate_kbps = 2000;  // 默认值
  int slices = 1;           // 强制单切片，彻底消除水平条纹

  if (const char* br_env = std::getenv("VIDEO_BITRATE_KBPS")) {
    try { bitrate_kbps = std::max(1, std::stoi(br_env)); } catch (...) {}
  }
  // 即使设置了 CARLA_X264_SLICES，也强制设为 1，除非开发者明确知道自己在做什么并设为 1
  if (const char* sl_env = std::getenv("CARLA_X264_SLICES")) {
    try {
      int val = std::stoi(sl_env);
      if (val > 1) {
        std::cerr << "[WARNING] CARLA_X264_SLICES=" << val << " 会产生条纹，强制修正为 1" << std::endl;
        slices = 1;
      } else {
        slices = std::max(1, val);
      }
    } catch (...) { slices = 1; }
  }

  int bufsize_kbps = bitrate_kbps * 2;

  oss << "ffmpeg -y -f rawvideo -pix_fmt bgr24 -s " << m_width << "x" << m_height
      << " -r " << m_fps << " -i pipe:0"
      << " -c:v libx264 -preset ultrafast -tune zerolatency"
      << " -profile:v baseline -level 3.0"           // ✅ 强制 Baseline Profile 确保客户端兼容性
      << " -b:v " << bitrate_kbps << "k"
      << " -maxrate " << bitrate_kbps << "k"
      << " -bufsize " << bufsize_kbps << "k"
      << " -pix_fmt yuv420p"
      << " -g " << m_fps << " -keyint_min " << m_fps
      << " -x264-params \"slices=" << slices << ":threads=1\"" // ✅ 强制单线程+单切片，彻底杜绝条纹源头
      << " -f flv " << m_rtmpUrl << " 2>/workspace/logs/ffmpeg_" << m_streamId << ".log"; // ✅ 记录推流侧日志便于追查
  std::string cmd = oss.str();
  std::cout << logTag(m_streamId) << " ffmpeg 推流启动，参数: " << cmd << std::endl;

#ifdef __linux__
  FILE* fp = popen(cmd.c_str(), "w");
  if (!fp) {
    std::cerr << logTag(m_streamId) << " popen ffmpeg 失败 errno=" << errno << std::endl;
    m_running = false;
    m_cv.notify_one();  // 通知 writerLoop 线程退出（防止 wait 永久阻塞）
    return;
  }
  m_ffmpegStdin = fp;
  const size_t frameSize = static_cast<size_t>(m_width) * m_height * 3;
  int framesWritten = 0;
  int fwriteErrors = 0;
  while (m_running.load()) {
    std::vector<uint8_t> frame;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait_for(lock, std::chrono::milliseconds(200), [this] { return !m_queue.empty() || !m_running.load(); });
      if (!m_running.load()) break;
      if (m_queue.empty()) continue;
      frame = std::move(m_queue.front());
      m_queue.pop();
    }
    if (frame.size() >= frameSize && m_ffmpegStdin) {
      size_t written = fwrite(frame.data(), 1, frameSize, m_ffmpegStdin);
      if (written != frameSize) {
        // broken pipe (ffmpeg 崩溃/退出) 或写入错误；记录并停止推流
        if (++fwriteErrors == 1) {
          std::cerr << logTag(m_streamId) << " fwrite 写入失败 written=" << written
                    << " expected=" << frameSize << " errno=" << errno
                    << "（ffmpeg 可能已退出，停止推流）" << std::endl;
        }
        m_running = false;
        m_cv.notify_one();
        break;
      }
      framesWritten++;
    }
  }
  if (m_ffmpegStdin) {
    int pcloseRet = pclose(m_ffmpegStdin);
    if (pcloseRet != 0) {
      std::cerr << logTag(m_streamId) << " ffmpeg 退出 pclose=" << pcloseRet
                << "（正常退出返回 0，非 0 表示异常）" << std::endl;
    }
    m_ffmpegStdin = nullptr;
  }
#endif
  std::cout << logTag(m_streamId) << " 推流已停止 frames_written=" << framesWritten
            << " fwrite_errors=" << fwriteErrors << std::endl;
}

void RtmpPusher::pushFrame(const uint8_t* bgr, size_t size) {
  if (!m_running.load() || m_useTestPattern) return;
  const size_t packedRowSize = static_cast<size_t>(m_width) * 3;
  const size_t expectedPackedSize = packedRowSize * m_height;

  if (size < expectedPackedSize) {
    // 数据量不足，忽略本帧防止 ffmpeg 阻塞或显示异常
    return;
  }

  std::vector<uint8_t> packedBuffer;
  if (size == expectedPackedSize) {
    // 情况 A：数据已是完美 packed (无行填充)，执行快速整体拷贝
    packedBuffer.assign(bgr, bgr + expectedPackedSize);
  } else {
    // 情况 B：存在行对齐 (Stride > Width*3)。
    // 根本原因分析：若直接暴力拷贝 size 字节，ffmpeg 按 rawvideo wxh 解析时会产生水平偏移（条纹）。
    // 解决：按行采样拷贝，剔除 Padding，确保推给 ffmpeg 的是严格连续的像素。
    packedBuffer.resize(expectedPackedSize);
    const size_t actualStride = size / m_height;
    
    // 关键日志：只在 stride 与 width*3 不一致且非 testsrc 时打印一次
    static bool loggedStride = false;
    if (!loggedStride && actualStride != packedRowSize) {
      std::cout << logTag(m_streamId) << " [STRIDE_AUDIT] 检测到 Padding! width=" << m_width 
                << " width*3=" << packedRowSize << " actualStride=" << actualStride 
                << " size=" << size << " (已在 pushFrame 路径修复)" << std::endl;
      loggedStride = true;
    }

    for (int i = 0; i < m_height; ++i) {
      std::memcpy(packedBuffer.data() + i * packedRowSize,
                  bgr + i * actualStride,
                  packedRowSize);
    }
  }

  if (teleopPreEncodeSnapshotEnabled()) {
    const auto nowSteady = std::chrono::steady_clock::now();
    const int intervalSec = teleopSnapshotIntervalSec();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        nowSteady - m_lastPreEncodeSnapshot);
    if (elapsed.count() >= intervalSec) {
      m_lastPreEncodeSnapshot = nowSteady;
      const std::string root = teleopLogRoot();
      const std::string dir = root + "/video_debug/pre_encode";
      try {
        std::filesystem::create_directories(dir);
      } catch (const std::exception& e) {
        std::cerr << logTag(m_streamId) << " [VideoDebug] pre_encode mkdir failed " << dir
                  << " err=" << e.what() << std::endl;
      }
      const std::string path = dir + "/" + sanitizeStreamIdForPath(m_streamId) + "_" +
                               utcTimestampForFilename() + "_w" + std::to_string(m_width) + "_h" +
                               std::to_string(m_height) + ".ppm";
      if (writeBgrPackedAsPpm(path, packedBuffer.data(), m_width, m_height)) {
        std::cout << logTag(m_streamId) << " [VideoDebug] pre_encode snapshot path=" << path
                  << std::endl;
      } else {
        static int errCount = 0;
        if (++errCount <= 3)
          std::cerr << logTag(m_streamId) << " [VideoDebug] pre_encode snapshot write failed path="
                    << path << std::endl;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (m_queue.size() >= 2) m_queue.pop();
    m_queue.push(std::move(packedBuffer));
    m_cv.notify_one();
  }
}

void RtmpPusher::stop() {
  if (!m_running.exchange(false)) return;
#ifdef __linux__
  if (m_useTestPattern) {
    pid_t pid;
    {
      std::lock_guard<std::mutex> lock(m_testPidMutex);
      pid = m_testPatternPid;
    }
    if (pid > 0) {
      if (kill(pid, SIGTERM) != 0) {
        std::cerr << logTag(m_streamId) << " kill SIGTERM pid=" << pid << " failed errno=" << errno << std::endl;
      }
    }
  }
#endif
  m_cv.notify_all();
  if (m_writerThread) {
    if (m_writerThread->joinable()) {
      m_writerThread->join();
    } else {
      std::cerr << logTag(m_streamId) << " stop: writer thread not joinable (was never started), skipping join" << std::endl;
    }
    m_writerThread.reset();
  }
}

}  // namespace carla_bridge
