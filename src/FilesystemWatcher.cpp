#include "FilesystemWatcher.hpp"
#include "efsw/efsw.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace sync_app {

enum class SettleState { Polling, Settling };

struct PendingEvent {
  WatchEvent type;
  fs::file_time_type lastMTime;
  std::chrono::steady_clock::time_point nextCheck;
  std::chrono::steady_clock::time_point deletedTime;
  SettleState state;
  std::string oldPath;
  bool isMoved = false;
};

struct DeletedItem {
  std::string inode;
  std::string path;
  std::chrono::steady_clock::time_point time;
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
  std::condition_variable cv;
  std::atomic<bool> workerRunning{false};
  std::atomic<int> pendingCount{0};
  std::map<std::string, DeletedItem> deletedItems;
  std::unordered_map<std::string, std::string> &m_inodesCache;
  FilesystemWatcher::Callback callback;

  // Configurable intervals
  std::chrono::milliseconds pollInterval{100};
  std::chrono::milliseconds settleTime{2000};

  Impl(std::unordered_map<std::string, std::string> &inodesCache,
       FilesystemWatcher::Callback callback)
      : m_inodesCache(inodesCache), callback(callback) {}
  ~Impl() = default;

  std::string getInode(const std::string &absPath) {
#ifdef _WIN32
    HANDLE hFile = CreateFileW(
        std::wstring(absPath.begin(), absPath.end())
            .c_str(), // Basic conversion, assuming ASCII/UTF8 overlap for now
        0,            // No access rights needed for attributes
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
      return "";

    BY_HANDLE_FILE_INFORMATION fileInfo;
    std::string inodeStr = "";
    if (GetFileInformationByHandle(hFile, &fileInfo)) {
      inodeStr = std::to_string(fileInfo.nFileIndexHigh) + "-" +
                 std::to_string(fileInfo.nFileIndexLow);
    }
    CloseHandle(hFile);
    return inodeStr;
#else
    struct stat st;
    if (stat(absPath.c_str(), &st) == 0) {
      return std::to_string(st.st_ino);
    }
    return "";
#endif
  }

  void workerLoop() {
    while (workerRunning) {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [&] { return !workerRunning || !pendingEvents.empty(); });
      if (!workerRunning)
        break;
      auto now = std::chrono::steady_clock::now();

      for (auto it = pendingEvents.begin(); it != pendingEvents.end();) {
        const std::string &path = it->first;
        auto &ev = it->second;
        if (ev.type == WatchEvent::Renamed) {
          m_inodesCache.erase(ev.oldPath);
          m_inodesCache[path] = getInode(path);
          if (callback)
            callback(path, ev.oldPath, WatchEvent::Renamed);
          it = pendingEvents.erase(it);
          continue;
        }
        if (ev.type == WatchEvent::Added || ev.type == WatchEvent::Modified) {
          bool isDir = fs::is_directory(path);
          auto inode = getInode(path);
          if (isDir && ev.type == WatchEvent::Modified) {
            it = pendingEvents.erase(it);
            continue;
          }
          if (isDir) {
            // folder add event
            auto delIt = deletedItems.find(inode);
            m_inodesCache[path] = inode;
            if (delIt != deletedItems.end()) {
              // folder move detected dispatch moved event
              std::string oldPath = delIt->second.path;
              if (callback)
                callback(path, oldPath, WatchEvent::Moved);
              pendingEvents.erase(oldPath);
              m_inodesCache.erase(oldPath);
              deletedItems.erase(inode);
              it = pendingEvents.erase(it);
              continue;
            } else {
              // its a new event. dispatch add event
              if (callback)
                callback(path, "", WatchEvent::Added);
              it = pendingEvents.erase(it);
              continue;
            }
          } else {
            // file add event
            auto delIt = deletedItems.find(inode);
            if (delIt != deletedItems.end()) {
              // file move detected wait for file to stabilize
              std::string oldPath = delIt->second.path;
              it->second.oldPath = oldPath;
              it->second.isMoved = true;
            }
            if (now < it->second.nextCheck) {
              ++it;
              continue;
            }
            // 1. Check if file still exists

            try {
              if (!fs::exists(path)) {
                auto iIt = m_inodesCache.find(path);
                if (iIt != m_inodesCache.end()) {
                  m_inodesCache.erase(iIt);
                } else {
                  if (callback) {
                    callback(path, "", WatchEvent::Deleted);
                  }
                }
                it = pendingEvents.erase(it);
                continue;
              }

              auto currentMTime = fs::last_write_time(path);

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
                  m_inodesCache[path] = inode;
                  if (callback) {
                    if (ev.isMoved) {
                      callback(path, ev.oldPath, WatchEvent::Moved);
                      m_inodesCache.erase(ev.oldPath);
                      pendingEvents.erase(ev.oldPath);
                      deletedItems.erase(inode);
                    } else {
                      callback(path, "", ev.type);
                    }
                  }
                  it = pendingEvents.erase(it);
                  continue;
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

        if (ev.type == WatchEvent::Deleted) {
          auto cacheIt = m_inodesCache.find(path);
          if (cacheIt != m_inodesCache.end()) {
            // found the deleted file in cache.
            std::string inode = cacheIt->second;
            if (deletedItems.find(inode) == deletedItems.end()) {
              deletedItems[inode] = {inode, path,
                                     std::chrono::steady_clock::now()};
              it->second.deletedTime = deletedItems[inode].time;

            } else {
              auto elapsed =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - deletedItems[inode].time)
                      .count();

              if (elapsed >
                  (settleTime + std::chrono::milliseconds(500)).count()) {
                if (callback) {
                  callback(path, "", WatchEvent::Deleted);
                }
                deletedItems.erase(inode);
                it = pendingEvents.erase(it);
                continue;
              }
            }
          } else {
            if (callback) {
              callback(path, "", WatchEvent::Deleted);
            }
            it = pendingEvents.erase(it);
            continue;
          }
          ++it;
          continue;
        }
      }
    }
  }
  void pushEvent(const std::string &path, const std::string &oldPath,
                 WatchEvent event) {
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
      bool wasEmpty = pendingEvents.empty();
      pendingEvents[path] = PendingEvent{
          event,   mtime, now + pollInterval, now, SettleState::Polling,
          oldPath, false};
      if (wasEmpty) {
        pendingCount = 1;
        cv.notify_one();
      } else {
        ++pendingCount;
      }
    } catch (...) {
      bool wasEmpty = pendingEvents.empty();
      pendingEvents[path] =
          PendingEvent{event, (fs::file_time_type::min)(), now + pollInterval,
                       now,   SettleState::Polling,        oldPath,
                       false};
      if (wasEmpty) {
        pendingCount = 1;
      } else {
        ++pendingCount;
      }
    }
  }

  // Implement FileWatchListener
  void handleFileAction(efsw::WatchID watchid, const std::string &dir,
                        const std::string &filename, efsw::Action action,
                        std::string oldFilename) override {
    std::string fullPath = dir + filename;
    std::string fullOldPath = dir + oldFilename;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
      fullPath = dir + "/" + filename;
    fullPath =
        std::filesystem::path(fullPath).lexically_normal().generic_string();

    WatchEvent type;

    auto now = std::chrono::steady_clock::now();

    if (action == efsw::Actions::Add) {
      type = WatchEvent::Added;
    } else if (action == efsw::Actions::Delete) {
      type = WatchEvent::Deleted;
    } else if (action == efsw::Actions::Modified) {
      type = WatchEvent::Modified;
    } else if (action == efsw::Actions::Moved) {
      if (!oldFilename.empty()) {
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
          fullOldPath = dir + "/" + oldFilename;
        fullOldPath = std::filesystem::path(fullOldPath)
                          .lexically_normal()
                          .generic_string();
        type = WatchEvent::Renamed;
      }
    } else
      return;
    pushEvent(fullPath, fullOldPath, type);
  }
};

FilesystemWatcher::FilesystemWatcher(
    const std::string &path,
    std::unordered_map<std::string, std::string> &inodesCache,
    Callback callback)
    : m_path(path), m_inodesCache(inodesCache), m_callback(callback),
      m_impl(std::make_unique<Impl>(inodesCache, callback)) {}
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

} // namespace sync_app
