#ifndef CARLA_BRIDGE_RTMP_PUSHER_H
#define CARLA_BRIDGE_RTMP_PUSHER_H

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

namespace carla_bridge {

class RtmpPusher {
 public:
  RtmpPusher(const std::string& streamId, const std::string& rtmpUrl,
             int width, int height, int fps);
  ~RtmpPusher();

  void start();
  void stop();
  bool isRunning() const { return m_running.load(); }
  void pushFrame(const uint8_t* bgr, size_t size);
  void startTestPattern();

 private:
  void writerLoop();

  std::string m_streamId;
  std::string m_rtmpUrl;
  int m_width, m_height, m_fps;
  std::atomic<bool> m_running{false};
  std::unique_ptr<std::thread> m_writerThread;
  std::queue<std::vector<uint8_t>> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  FILE* m_ffmpegStdin = nullptr;
  bool m_useTestPattern = false;
#ifdef __linux__
  std::mutex m_testPidMutex;
  int m_testPatternPid = -1;  // 子进程 pid，用于 stop() 时 SIGTERM
#endif
};

}  // namespace carla_bridge

#endif
