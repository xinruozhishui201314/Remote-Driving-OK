#include "rtmp_pusher.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <iostream>
#include <sstream>
#include <array>

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
      execl("/usr/bin/ffmpeg", "ffmpeg", "-y", "-f", "lavfi", "-i", optI.c_str(),
            "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
            "-pix_fmt", "yuv420p", "-g", optG.c_str(), "-f", "flv", m_rtmpUrl.c_str(),
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
  oss << "ffmpeg -y -f rawvideo -pix_fmt bgr24 -s " << m_width << "x" << m_height
      << " -r " << m_fps << " -i pipe:0"
      << " -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p"
      << " -g " << m_fps << " -keyint_min " << m_fps
      << " -f flv " << m_rtmpUrl << " 2>/dev/null";
  std::string cmd = oss.str();
  std::cout << logTag(m_streamId) << " ffmpeg 推流已启动 -> " << m_rtmpUrl << std::endl;

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
  const size_t frameSize = static_cast<size_t>(m_width) * m_height * 3;
  if (size < frameSize) return;
  std::vector<uint8_t> copy(bgr, bgr + frameSize);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (m_queue.size() >= 2) m_queue.pop();
    m_queue.push(std::move(copy));
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
