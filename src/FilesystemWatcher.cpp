#include "FilesystemWatcher.hpp"
#include "types.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <efsw/efsw.hpp>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <sqlite_orm/sqlite_orm.h>
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
  bool isAccesible = true;
  std::string cachedInode = "";
};

struct RawEvent {
  WatchEvent type;
  std::string path;
  std::string oldPath;
  std::chrono::steady_clock::time_point arrivalTime;
};

struct DeletedItem {
  std::string inode;
  std::string path;
  std::chrono::steady_clock::time_point time;
  bool isDir = false;
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
  std::unordered_map<std::string, InodeCacheInfo> &m_inodesCache;
  FilesystemWatcher::Callback callback;

  // buffer to store the incoming event
  std::vector<RawEvent> incomingEvents;

  // Configurable intervals
  std::chrono::milliseconds pollInterval{100};
  std::chrono::milliseconds settleTime{2000};

  Impl(std::unordered_map<std::string, InodeCacheInfo> &inodesCache,
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
    std::vector<RawEvent> localQueue;
    while (workerRunning) {
      if (incomingEvents.empty() && pendingEvents.empty()) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock,
                [&]() { return !incomingEvents.empty() || !workerRunning; });
      }
      if (!workerRunning)
        break;
      {
        std::lock_guard<std::mutex> lock(mtx);
        if (!incomingEvents.empty())
          localQueue.swap(incomingEvents);
      }

      auto now = std::chrono::steady_clock::now();

      for (auto raw : localQueue) {
        bool isModifiedEvent = raw.type == WatchEvent::Modified;
        auto it = pendingEvents.find(raw.path);
        bool pathExists = it != pendingEvents.end();
        bool isAddedEvent =
            pathExists ? it->second.type == WatchEvent::Added : false;
        if (isModifiedEvent && isAddedEvent)
          continue;
        auto mtime = (fs::file_time_type::min)();
        auto nextCheck = now + pollInterval;
        auto deletedTime = now;
        auto fileState = SettleState::Polling;
        bool isAccessible = true;
        bool isMoved = false;
        auto eventType = raw.type;
        const std::string path = raw.path;
        std::string oldPath = raw.oldPath;
        try {
          mtime = fs::last_write_time(path);
        } catch (const std::exception &e) {
          isAccessible = false;
        }
        pendingEvents[path] =
            PendingEvent({eventType, mtime, nextCheck, deletedTime, fileState,
                          oldPath, isMoved, isAccessible, ""});
      }

      {
        std::lock_guard<std::mutex> lock(mtx);
        localQueue.clear();
      }

      for (auto it = pendingEvents.begin(); it != pendingEvents.end();) {
        const std::string path = it->first;
        auto &ev = it->second;
        if (ev.type == WatchEvent::Renamed) {
          std::string inode = "";
          bool isDir = false;
          try {
            inode = getInode(path);
            isDir = fs::is_directory(path);
          } catch (...) {
          }
          m_inodesCache.erase(ev.oldPath);
          m_inodesCache[path] = InodeCacheInfo({inode, isDir});
          if (callback)
            callback(path, ev.oldPath, WatchEvent::Renamed);
          it = pendingEvents.erase(it);
          continue;
        }

        if (now < ev.nextCheck) {
          ++it;
          continue;
        }

        if (ev.type == WatchEvent::Deleted) {
          auto cacheIt = m_inodesCache.find(path);
          if (cacheIt != m_inodesCache.end()) {
            // found the deleted file in cache.
            std::string inode = cacheIt->second.inode;
            auto delIt = deletedItems.find(inode);
            bool isDir = cacheIt->second.isDir;
            if (delIt == deletedItems.end()) {
              deletedItems[inode] = {inode, path, now, isDir};
              ev.deletedTime = now;
              ev.nextCheck = now + pollInterval;
              ++it;
              continue;
            } else {
              auto elapsed =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - delIt->second.time);
              bool isDir = delIt->second.isDir;
              if (isDir && elapsed >= pollInterval) {
                if (callback)
                  callback(path, "", WatchEvent::Deleted);
                m_inodesCache.erase(path);
                deletedItems.erase(inode);
                it = pendingEvents.erase(it);
                continue;
              }
              if (isDir && elapsed <= pollInterval) {
                ev.nextCheck = now + pollInterval;
                ++it;
                continue;
              }
              if (!isDir && elapsed >= settleTime) {
                if (callback) {
                  callback(path, "", WatchEvent::Deleted);
                }
                m_inodesCache.erase(path);
                deletedItems.erase(inode);
                it = pendingEvents.erase(it);
                continue;
              }
              if (!isDir && elapsed <= settleTime) {
                ev.nextCheck = now + pollInterval;
                ++it;
                continue;
              }
            }
          } else {
            if (callback) {
              callback(path, "", WatchEvent::Deleted);
            }
            m_inodesCache.erase(path);
            it = pendingEvents.erase(it);
            continue;
          }
        }

        else if (ev.type == WatchEvent::Added ||
                 ev.type == WatchEvent::Modified) {
          try {
            if (!fs::exists(path)) {
              it = pendingEvents.erase(it);
              continue;
            }
            bool isDir = fs::is_directory(path);
            if (ev.cachedInode.empty())
              ev.cachedInode = getInode(path);
            auto inode = ev.cachedInode;
            if (isDir && ev.type == WatchEvent::Modified) {
              it = pendingEvents.erase(it);
              continue;
            }
            auto delIt = deletedItems.find(inode);
            if (isDir) {
              // folder add event
              m_inodesCache[path] = InodeCacheInfo({inode, true});
              if (delIt != deletedItems.end()) {
                // folder move detected dispatch moved event
                std::string oldPath = delIt->second.path;
                if (callback)
                  callback(path, oldPath, WatchEvent::Moved);
                m_inodesCache.erase(oldPath);
                deletedItems.erase(inode);
                pendingEvents.erase(oldPath);
              } else {
                // its a new event. dispatch add event
                if (callback)
                  callback(path, "", WatchEvent::Added);
              }
              it = pendingEvents.erase(it);
              continue;
            }
            // file add or modified event
            if (delIt != deletedItems.end()) {
              // file move detected wait for file to stabilize
              std::string oldPath = delIt->second.path;
              ev.oldPath = oldPath;
              ev.isMoved = true;
            }

            auto currentMTime = fs::last_write_time(path);

            if (currentMTime != it->second.lastMTime) {
              // File is changing! Reset to polling state
              ev.lastMTime = currentMTime;
              ev.nextCheck = now + pollInterval;
              ev.state = SettleState::Polling;
              ++it;
              continue;
            } else if (it->second.state == SettleState::Polling) {
              // MTime is stable for 'pollInterval', move to 'settleTime'
              ev.state = SettleState::Settling;
              ev.nextCheck = now + settleTime;
              ++it;
              continue;
            } else if (it->second.state == SettleState::Settling) {
              // MTime has been stable for full 'settleTime'.
              // Final Check: Is it locked by another process?
              if (isFileAccessible(path)) {
                m_inodesCache[path] = InodeCacheInfo({inode, false});
                if (ev.isMoved) {
                  if (callback)
                    callback(path, ev.oldPath, WatchEvent::Moved);
                  m_inodesCache.erase(ev.oldPath);
                  pendingEvents.erase(ev.oldPath);
                  deletedItems.erase(inode);
                } else {
                  if (callback)
                    callback(path, "", ev.type);
                }
                it = pendingEvents.erase(it);
                continue;
              } else {
                // Still locked! Stay in Settling state and check again soon
                ev.nextCheck = now + pollInterval;
                ++it;
                continue;
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
  }
  void pushEvent(const std::string &path, const std::string &oldPath,
                 WatchEvent event) {
    {
      std::lock_guard<std::mutex> lock(mtx);
      incomingEvents.push_back(
          {event, path, oldPath, std::chrono::steady_clock::now()});
    }
    cv.notify_one();
  }

  /*void pushEvent(const std::string &path, const std::string &oldPath,
                 WatchEvent event) {
    std::lock_guard<std::mutex> lock(mtx);
    // Requirement: ignore Modified if Add is already pending
    if (pendingEvents.count(path))
      return;
    if (event == WatchEvent::Modified) {
      auto existing = pendingEvents.find(path);
      if (existing != pendingEvents.end() &&
          existing->second.type == WatchEvent::Added) {
        return;
      }
    }
    auto now = std::chrono::steady_clock::now();
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
  */

  // Implement FileWatchListener
  void handleFileAction(efsw::WatchID watchid, const std::string &dir,
                        const std::string &filename, efsw::Action action,
                        std::string oldFilename) override {
    std::string fullPath = dir + filename;
    std::string fullOldPath = dir + oldFilename;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
      fullPath = dir + "/" + filename;

    WatchEvent type;

    if (action == efsw::Actions::Add) {
      type = WatchEvent::Added;
      fullOldPath = "";
    } else if (action == efsw::Actions::Delete) {
      type = WatchEvent::Deleted;
      fullOldPath = "";
    } else if (action == efsw::Actions::Modified) {
      type = WatchEvent::Modified;
      fullOldPath = "";
    } else if (action == efsw::Actions::Moved) {
      if (!oldFilename.empty()) {
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
          fullOldPath = dir + "/" + oldFilename;
        type = WatchEvent::Renamed;
      }
    } else
      return;
    fullPath = fs::path(fullPath).lexically_normal().generic_string();
    fullOldPath = fs::path(fullOldPath).lexically_normal().generic_string();
    pushEvent(fullPath, fullOldPath, type);
  }
};

FilesystemWatcher::FilesystemWatcher(
    const std::string &path,
    std::unordered_map<std::string, InodeCacheInfo> &inodesCache,
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
