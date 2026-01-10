#include "FilesystemWatcher.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace sync {

enum class SettleState { Polling, Settling };

struct PendingEvent {
  WatchEvent type;
  fs::file_time_type lastMTime;
  std::chrono::steady_clock::time_point nextCheck;
  SettleState state;
};

// Helper to check if file is locked on Windows
bool isFileAccessible(const std::string &path) {
#ifdef _WIN32
  // Try to open file with NO sharing allowed.
  // If this fails, someone else (like a copy process) still has it open.
  HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }
  CloseHandle(hFile);
#endif
  return true;
}

struct FilesystemWatcher::Impl : public efsw::FileWatchListener {
  efsw::FileWatcher watcher;
  efsw::WatchID watchId = 0;
  bool running = false;

  // Debouncing members
  std::map<std::string, PendingEvent> pendingEvents;
  std::mutex mtx;
  std::thread workerThread;
  std::atomic<bool> workerRunning{false};
  FilesystemWatcher::Callback callback;

  // Configurable intervals
  std::chrono::milliseconds pollInterval{100};
  std::chrono::milliseconds settleTime{2000};

  void workerLoop() {
    while (workerRunning) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      std::unique_lock<std::mutex> lock(mtx);
      auto now = std::chrono::steady_clock::now();

      for (auto it = pendingEvents.begin(); it != pendingEvents.end();) {
        if (now < it->second.nextCheck) {
          ++it;
          continue;
        }

        const std::string &path = it->first;

        // 1. Check if file still exists
        try {
          if (!fs::exists(path)) {
            it = pendingEvents.erase(it);
            continue;
          }

          auto currentMTime = fs::last_write_time(path);

          // 2. State Machine Logic
          if (currentMTime != it->second.lastMTime) {
            // File is changing! Reset to polling state
            it->second.lastMTime = currentMTime;
            it->second.nextCheck = now + pollInterval;
            it->second.state = SettleState::Polling;
            ++it;
          } else if (it->second.state == SettleState::Polling) {
            // MTime is stable for 'pollInterval', move to 'settleTime'
            it->second.state = SettleState::Settling;
            it->second.nextCheck = now + settleTime;
            ++it;
          } else if (it->second.state == SettleState::Settling) {
            // MTime has been stable for full 'settleTime'.
            // Final Check: Is it locked by another process?
            if (isFileAccessible(path)) {
              if (callback) {
                callback(path, "", it->second.type);
              }
              it = pendingEvents.erase(it);
            } else {
              // Still locked! Stay in Settling state and check again soon
              it->second.nextCheck = now + pollInterval;
              ++it;
            }
          }
        } catch (const fs::filesystem_error &e) {
          // Errors like permission denied while checking mtime
          it->second.nextCheck = now + pollInterval;
          ++it;
        }
      }
    }
  }

  void pushEvent(const std::string &path, WatchEvent event) {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = std::chrono::steady_clock::now();

    // Requirement: ignore Modified if Add is already pending
    if (event == WatchEvent::Modified) {
      auto existing = pendingEvents.find(path);
      if (existing != pendingEvents.end() &&
          existing->second.type == WatchEvent::Added) {
        return;
      }
    }

    try {
      fs::file_time_type mtime;
      if (fs::exists(path)) {
        mtime = fs::last_write_time(path);
      }
      pendingEvents[path] =
          PendingEvent{event, mtime, now + pollInterval, SettleState::Polling};
    } catch (...) {
      pendingEvents[path] =
          PendingEvent{event, (fs::file_time_type::min)(), now + pollInterval,
                       SettleState::Polling};
    }
  }

  // Implement FileWatchListener
  void handleFileAction(efsw::WatchID watchid, const std::string &dir,
                        const std::string &filename, efsw::Action action,
                        std::string oldFilename) override {
    std::string fullPath = dir + filename;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
      fullPath = dir + "/" + filename;
    fullPath =
        std::filesystem::path(fullPath).lexically_normal().generic_string();
    switch (action) {
    case efsw::Actions::Add:
      if (fs::is_directory(fullPath)) {
        if (callback) {
          callback(fullPath, oldFilename, WatchEvent::Added);
        }
      } else {
        pushEvent(fullPath, WatchEvent::Added);
      }
      break;
    case efsw::Actions::Delete:
      if (callback) {
        callback(fullPath, oldFilename, WatchEvent::Deleted);
      }
      /*      if (fs::is_directory(fullPath)) {
              std::cout << "[Watcher] folder deletion Detected on path: " <<
         fullPath
                        << std::endl;
              if (callback) {
                callback(fullPath, WatchEvent::Deleted);
              }
            } else {
              std::cout << "[Watcher] file deletion Detected on path: " <<
         fullPath
                        << std::endl;
              std::lock_guard<std::mutex> lock(mtx);
              pendingEvents.erase(fullPath);
              if (callback) {
                callback(fullPath, WatchEvent::Deleted);
              }
             }*/
      break;
    case efsw::Actions::Modified:
      if (!fs::is_directory(fullPath)) {
        pushEvent(fullPath, WatchEvent::Modified);
      }
      break;
    case efsw::Actions::Moved:
      if (!oldFilename.empty()) {
        std::string fullOldPath = dir + oldFilename;
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
          fullOldPath = dir + "/" + oldFilename;
        fullOldPath = std::filesystem::path(fullOldPath)
                          .lexically_normal()
                          .generic_string();
        if (callback)
          callback(fullPath, fullOldPath, WatchEvent::Moved);
      }
      pushEvent(fullPath, WatchEvent::Moved);
      break;
    default:
      break;
    }
  }
};

FilesystemWatcher::FilesystemWatcher(const std::string &path, Callback callback)
    : m_path(path), m_callback(callback), m_impl(std::make_unique<Impl>()) {
  m_impl->callback = m_callback;
}

FilesystemWatcher::~FilesystemWatcher() { stop(); }

void FilesystemWatcher::start() {
  if (m_impl->running)
    return;

  m_impl->workerRunning = true;
  m_impl->workerThread = std::thread(&Impl::workerLoop, m_impl.get());

  try {
    m_impl->watchId = m_impl->watcher.addWatch(m_path, m_impl.get(), true);
    if (m_impl->watchId < 0) {
      std::cerr << "[Watcher] Error starting watcher: "
                << efsw::Errors::Log::getLastErrorLog() << std::endl;
      return;
    }
    m_impl->watcher.watch();
    m_impl->running = true;
    std::cout << "[Watcher] Started monitoring (with debouncing): " << m_path
              << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[Watcher] Error starting watcher: " << e.what() << std::endl;
  }
}

void FilesystemWatcher::stop() {
  if (!m_impl->running)
    return;

  m_impl->watcher.removeWatch(m_impl->watchId);

  m_impl->workerRunning = false;
  if (m_impl->workerThread.joinable()) {
    m_impl->workerThread.join();
  }

  m_impl->running = false;
  std::cout << "[Watcher] Stopped monitoring: " << m_path << std::endl;
}

} // namespace sync
