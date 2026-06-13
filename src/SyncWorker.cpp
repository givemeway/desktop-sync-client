#include "SyncWorker.hpp"
#include "ActivityStore.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "SyncTree.hpp"
#include "ThreadPool.hpp"
#include "Utility.hpp"
#include "UuidUtils.hpp"
#include "types.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <winerror.h>
#ifdef _WIN32
#include <windows.h>

#include <cfapi.h>
// #include <fileapi.h>
// #include <minwindef.h>
// #include <winnt.h>
#endif

#ifdef DELETE
#undef DELETE
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef UNKNOWN
#undef UNKNOWN
#endif
#ifdef DUPLICATE
#undef DUPLICATE
#endif
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
#ifdef VOID
#undef VOID
#endif

namespace fs = std::filesystem;

namespace sync_app {

struct SyncEvent {
  WatchEvent type;
  std::string path;
  std::string oldPath;
};

struct SyncWorker::Impl {
  DatabaseManager &m_dbManager;
  FileSystemScanner &m_scanner;
  ActivityStore &m_activityStore;
  SyncTree &m_syncTree;
  std::string m_syncPath;

  std::queue<SyncEvent> m_eventQueue;

  std::priority_queue<FileQueueEntry, std::vector<FileQueueEntry>,
                      Utility::PriorityComparator<FileQueueEntry>>
      m_filePriorityQ;

  std::priority_queue<DirectoryQueueEntry, std::vector<DirectoryQueueEntry>,
                      Utility::PriorityComparator<DirectoryQueueEntry>>
      m_dirPriorityQ;

  ThreadPool &m_threadPool;

  std::map<std::string, std::vector<SyncEvent>> m_eventMap;
  std::map<std::string, WatchEvent> m_ignoreMap;
  std::vector<std::thread> m_workerThreads;

  std::atomic<bool> m_running{false};
  std::mutex m_queueMtx;
  std::mutex m_upSyncMutex;
  std::mutex m_ignoreMtx;

  std::condition_variable m_queueCV;
  std::condition_variable m_upSyncCV;

  Impl(DatabaseManager &dbManager, FileSystemScanner &scanner,
       ActivityStore &activityStore, ThreadPool &threadPool, SyncTree &syncTree,
       const std::string &syncPath)
      : m_dbManager(dbManager), m_activityStore(activityStore),
        m_scanner(scanner), m_threadPool(threadPool), m_syncTree(syncTree),
        m_syncPath(syncPath) {}

  template <typename T> void pop(T &t) { t.pop(); }
  template <typename T1, typename T2> void push(T1 &t, T2 &t2) { t.push(t2); }
  template <typename T> auto top(T &t) { return t.top(); }
  template <typename T> bool empty(T &t) {
    if (t.empty())
      return true;
    return false;
  }

  bool isHighPriorityLocalTaskQueued() {
    auto fileEntry = m_filePriorityQ.top();
    auto dirEntry = m_dirPriorityQ.top();
    if (*fileEntry.priority <= 6 || *dirEntry.priority <= 5)
      return true;
    return false;
  }

  bool deHydrateFile(const std::string &absPath) {
    HANDLE hFile = CreateFileA(
        absPath.c_str(), WRITE_DAC,
        FILE_SHARE_WRITE | FILE_SHARE_DELETE | FILE_SHARE_READ, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
      std::cerr << "[SyncWorker] dehydrateFile: cannot open file :" << absPath
                << std::endl;
      return false;
    }
    LARGE_INTEGER startOffset, length = {};
    startOffset.QuadPart = 0;
    length.QuadPart = -1;

    HRESULT hr = CfDehydratePlaceholder(hFile, startOffset, length,
                                        CF_DEHYDRATE_FLAG_BACKGROUND, nullptr);
    if (FAILED(hr)) {
      std::cerr
          << "[SyncWorker] dehydrateFile: CfDeHydratePlaceHolder failed: 0x"
          << std::hex << hr << std::endl;
      CloseHandle(hFile);
      return false;
    }

    hr = CfSetInSyncState(hFile, CF_IN_SYNC_STATE_IN_SYNC,
                          CF_SET_IN_SYNC_FLAG_NONE, nullptr);
    if (FAILED(hr)) {
      std::cerr << "[SyncWorker] dehydrateFile: CfSetInSyncState failed: 0x"
                << std::hex << hr << std::endl;
      CloseHandle(hFile);
      return false;
    }

    hr = CfSetPinState(hFile, CF_PIN_STATE_UNPINNED, CF_SET_PIN_FLAG_NONE,
                       nullptr);
    CloseHandle(hFile);
    if (FAILED(hr)) {
      std::cerr << "[SyncWorker] dehydrateFile: CfSetPinState failed: 0x"
                << std::hex << hr << std::endl;
      return false;
    }

    std::cout << "[Syncworker] dehydrateFile: CfDehydratePlaceholder & "
                 "CfSetPinState & CfSetInSyncState SUCCESS "
              << hr << std::endl;
    return true;
  }

  void hydrateFile(const std::string &absPath) {
    HANDLE hFile =
        CreateFileA(absPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
      std::cerr << "[SyncWorker] fetchData: cannot open file :" << absPath
                << std::endl;
      return;
    }
    char buf;
    DWORD bytesRead = 0;
    ReadFile(hFile, &buf, 1, &bytesRead, nullptr);
    CloseHandle(hFile);
  }
};

SyncWorker::SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
                       ActivityStore &activityStore, ThreadPool &threadPool,
                       SyncTree &syncTree, const std::string &syncPath)
    : m_impl(std::make_unique<Impl>(dbManager, scanner, activityStore,
                                    threadPool, syncTree, syncPath)) {}

// SyncWorker::~SyncWorker() = default;
SyncWorker::~SyncWorker() { stop(); }

void SyncWorker::addActivity(const std::string &key, const SyncItem &item) {
  m_impl->m_activityStore.addActivity(key, item);
  emit activityAdded(key, item);
}

std::mutex &SyncWorker::getUpSyncMutex() { return m_impl->m_upSyncMutex; };
std::condition_variable &SyncWorker::getUpSyncCV() {
  return m_impl->m_upSyncCV;
};
// std::atomic<size_t> &SyncWorker::getUpSyncTasks() {
//  return m_impl->m_upSyncTasks;
//};

void SyncWorker::start() {
  if (m_impl->m_running)
    return;
  m_impl->m_running = true;
  int numThreads = 1;
  for (int i = 0; i < numThreads; ++i) {
    m_impl->m_workerThreads.push_back(
        std::thread(&SyncWorker::workerLoop, this));
  }
  std::cout << "[SyncWorker] " << numThreads << " worker threads started."
            << std::endl;
}

void SyncWorker::stop() {
  if (!m_impl->m_running)
    return;
  m_impl->m_running = false;
  m_impl->m_queueCV.notify_all();
  for (auto &t : m_impl->m_workerThreads) {
    if (t.joinable()) {
      t.join();
    }
  }
  m_impl->m_workerThreads.clear();
  std::cout << "[SyncWorker] All worker threads stopped." << std::endl;
}

bool SyncWorker::isHighPriorityLocalTaskQueued() const {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  return m_impl->isHighPriorityLocalTaskQueued();
};

void SyncWorker::pushDirEntry(const DirectoryQueueEntry &dq) {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  m_impl->m_dirPriorityQ.push(dq);
}

void SyncWorker::pushFileEntry(const FileQueueEntry &fq) {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  m_impl->m_filePriorityQ.push(fq);
}

std::optional<FileQueueEntry> SyncWorker::popNextFileEntry() {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  if (!m_impl->m_filePriorityQ.empty()) {
    auto file = m_impl->top(m_impl->m_filePriorityQ);
    m_impl->pop(m_impl->m_filePriorityQ);
    return file;
  } else {
    return std::nullopt;
  }
}

std::optional<DirectoryQueueEntry> SyncWorker::popNextDirEntry() {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  if (!m_impl->m_dirPriorityQ.empty()) {
    auto dir = m_impl->top(m_impl->m_dirPriorityQ);
    m_impl->pop(m_impl->m_dirPriorityQ);
    return dir;
  } else {
    return std::nullopt;
  }
}
bool SyncWorker::fileIsEmpty() {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  return m_impl->empty(m_impl->m_filePriorityQ);
};

bool SyncWorker::dirIsEmpty() {
  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());
  return m_impl->empty(m_impl->m_dirPriorityQ);
};

void SyncWorker::addIgnoreEvent(const std::string &path, WatchEvent event) {
  std::lock_guard<std::mutex> lock(m_impl->m_ignoreMtx);
  m_impl->m_ignoreMap[path] = event;
}

void SyncWorker::removeIgnoreEvent(const std::string &path, WatchEvent event) {
  std::lock_guard<std::mutex> lock(m_impl->m_ignoreMtx);
  auto it = m_impl->m_ignoreMap.find(path);
  if (it != m_impl->m_ignoreMap.end() && it->second == event) {
    m_impl->m_ignoreMap.erase(it);
  }
}

void SyncWorker::removeEventMap(const std::string &inode,
                                const std::string &path) {
  std::lock_guard<std::mutex> lock(m_impl->m_queueMtx);
  auto it = m_impl->m_eventMap.find(inode);
  if (it != m_impl->m_eventMap.end()) {
    if (!path.empty()) {
      auto &vec = it->second;
      vec.erase(std::remove_if(vec.begin(), vec.end(),
                               [&](const SyncEvent &e) {
                                 return e.path == path || e.oldPath == path;
                               }),
                vec.end());
      if (vec.empty()) {
        m_impl->m_eventMap.erase(it);
      }
    } else {
      m_impl->m_eventMap.erase(it);
    }
  }
}

void SyncWorker::enqueueEvent(WatchEvent event, const std::string &path,
                              const std::string &oldPath) {
  {
    std::lock_guard<std::mutex> lock(m_impl->m_ignoreMtx);

    auto it = m_impl->m_ignoreMap.find(path);
    if (it != m_impl->m_ignoreMap.end() && it->second == event) {
      m_impl->m_ignoreMap.erase(it);
      return;
    }
  }
  {
    std::lock_guard<std::mutex> lock(m_impl->m_queueMtx);
    SyncEvent syncEvent{event, path, oldPath};
    m_impl->m_eventQueue.push(syncEvent);
  }
  m_impl->m_queueCV.notify_all();
}

void SyncWorker::workerLoop() {
  while (m_impl->m_running) {
    SyncEvent event;
    {
      std::unique_lock<std::mutex> lock(m_impl->m_queueMtx);
      m_impl->m_queueCV.wait(lock,

                             [&]() {
                               return !m_impl->m_eventQueue.empty() ||
                                      !m_impl->m_running;
                             });
      if (!m_impl->m_running && m_impl->m_eventQueue.empty())
        break;
      event = m_impl->m_eventQueue.front();
      m_impl->m_eventQueue.pop();
    }
    // Now we call the handlers on the parent
    switch (event.type) {
    case WatchEvent::Added:
      m_impl->m_threadPool.enqueue(
          [this, path = event.path]() { handleAdded(path); });
      break;
    case WatchEvent::Deleted:
      std::cout << "[syncworker] Enqueueing Deleted: " << event.path
                << std::endl;
      m_impl->m_threadPool.enqueue(
          [this, path = event.path]() { handleDeleted(path); });
      break;
    case WatchEvent::Modified:
      std::cout << "[syncworker] Enqueueing Modified: " << event.path
                << std::endl;
      m_impl->m_threadPool.enqueue(
          [this, path = event.path]() { handleModified(path); });
      break;
    case WatchEvent::Moved:
      std::cout << "[syncworker] Enqueueing MOVED: " << event.path
                << " OLD: " << event.oldPath << std::endl;
      m_impl->m_threadPool.enqueue(
          [this, path = event.path, oldPath = event.oldPath]() {
            handleRenamed(path, oldPath, true);
          });
      break;
    case WatchEvent::Renamed:
      std::cout << "[syncworker] Enqueueing RENAME: " << event.path
                << " OLD: " << event.oldPath << std::endl;
      m_impl->m_threadPool.enqueue(
          [this, path = event.path, oldPath = event.oldPath]() {
            handleRenamed(path, oldPath);
          });
      break;
    }
  }
}

FileMetadata SyncWorker::constructFileMetadata(const ScannedFile &scannedFile,
                                               const FileQueueEntry &file,
                                               const std::string &dirID) {
  FileMetadata f;
  f.filename = scannedFile.filename;
  f.absPath = scannedFile.absPath;
  f.path = scannedFile.path;
  f.hashvalue = scannedFile.hash;
  f.inode = scannedFile.inode;
  f.lastSyncedHashValue = file.lastSyncedHashValue.empty()
                              ? scannedFile.hash
                              : file.lastSyncedHashValue;
  f.lastSynced = file.lastSynced;
  f.dirID = file.dirID.empty() ? dirID : file.dirID;
  //  f.conflictId = file.conflictId;
  f.last_modified = std::to_string(scannedFile.mtime);
  f.size = scannedFile.size;
  f.uuid = file.uuid.empty() ? UuidUtils::generate() : file.uuid;
  f.origin = file.origin.empty() ? f.uuid : file.origin;
  f.versions = file.versions == 0 ? 1 : file.versions;
  return f;
}

DirectoryMetadata
SyncWorker::constructDirectoryMetadata(const ScannedDirectory &scannedDir,
                                       const DirectoryQueueEntry &dir) {
  DirectoryMetadata d;
  d.uuid = dir.uuid.empty() ? UuidUtils::generate() : dir.uuid;
  d.created_at = std::to_string(scannedDir.mtime);
  d.inode = scannedDir.inode;
  d.path = scannedDir.path;
  d.absPath = scannedDir.absPath;
  pathParts p = m_impl->m_dbManager.getFolderDevice(fs::path(d.path));
  d.folder = p.folder;
  d.device = p.device;
  d.lastSynced = dir.lastSynced;
  return d;
}

void SyncWorker::handleAdded(const std::string &path) {
  if (!fs::exists(path))
    return;
  bool isDir = fs::is_directory(path);
  std::string relPath = m_impl->m_scanner.toRelativePath(path);
  std::cout << "[syncworker] " << relPath << " isDir: " << isDir << std::endl;

  if (!isDir) {
    std::filesystem::path p(path);
    std::string filename = p.filename().generic_string();

    // 1. Heavy Hashing (NO LOCK)
    std::ifstream fi(path, std::ios::binary);
    if (!fi.is_open()) {
      std::cout << "[syncworker] unable to read the file during hashing: "
                << filename << std::endl;
      return;
    }

    std::string hashStr = m_impl->m_scanner.calculateHash(path);

    std::int64_t unixTimeStamp =
        m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path));
    std::uintmax_t fileSize = fs::file_size(path);
    auto inode = m_impl->m_scanner.getInode(path);

    // 2. Database Work (LOCK)
    std::lock_guard<std::recursive_mutex> lock(
        m_impl->m_dbManager.getSyncMutex());

    std::optional<FileMetadata> file =
        m_impl->m_dbManager.getFileByPath(relPath, filename);

    if (!file.has_value()) {

      FileMetadata f;
      FileQueueEntry fq;

      f.filename = filename;
      f.absPath = path;
      f.path = relPath;
      f.inode = inode;
      f.hashvalue = hashStr;
      f.size = fileSize;
      f.last_modified = std::to_string(unixTimeStamp);
      f.lastSyncedHashValue = f.hashvalue;
      f.uuid = f.origin = UuidUtils::generate();
      f.lastSynced = "";
      f.versions = 1;

      pathParts part = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
      auto dir = m_impl->m_dbManager.getDirectoryByPath(part.device,
                                                        part.folder, f.path);
      if (dir.has_value()) {
        f.dirID = dir->uuid;
        fq.dirID = dir->uuid;
      } else {
        DirectoryMetadata d;
        DirectoryQueueEntry dq;
        d.absPath = p.parent_path().generic_string();
        d.path = f.path;
        d.created_at = std::to_string(unixTimeStamp);
        d.device = part.device;
        d.folder = part.folder;
        d.uuid = UuidUtils::generate();
        d.inode = m_impl->m_scanner.getInode(
            fs::path(path).parent_path().generic_string());
        std::string old_path = d.path;
        dq = Utility::constructDirectoryQueueEntry(d, SyncStatus::FILE_LINKED,
                                                   std::move(old_path));
        dq.priority = 6;
        m_impl->m_dbManager.insertDirectory(d, dq);
        f.dirID = d.uuid;
        fq.dirID = d.uuid;
      }
      f.conflictId = "";
      //      std::string sync_status = syncStatusToString(SyncStatus::NEW);
      std::string old_path = f.path;
      std::string old_filename = f.filename;

      fq = Utility::constructFileQueueEntry(
          f, SyncStatus::NEW, std::move(old_path), std::move(old_filename));
      fq.priority = qPriorityToInt(QPriority::FILE_UPLOAD);

      pushFileEntry(fq);
      m_impl->m_upSyncCV.notify_all();

      bool isFileInserted = m_impl->m_dbManager.insertFile(f, fq);

      if (!isFileInserted)
        std::cout << "[syncworker] file added into db FAILED" << f.absPath
                  << std::endl;
      else {
        auto syncItem = Utility::convertToActivity<FileQueueEntry>(
            fq, ActivityStatus::UPLOAD);
        addActivity(fq.uuid, syncItem);
      }
    }
    if (file.has_value())
      std::cout << "[syncworker] File Already Exists, Skipping: " << path
                << std::endl;
  } else {
    // 3. Folder Logic (LOCK)
    std::lock_guard<std::recursive_mutex> lock(
        m_impl->m_dbManager.getSyncMutex());

    DirectoryMetadata d;
    DirectoryQueueEntry dq;
    d.path = relPath;
    pathParts part = m_impl->m_dbManager.getFolderDevice(fs::path(d.path));
    d.device = part.device;
    d.folder = part.folder;
    d.absPath = path;
    auto existingDir = m_impl->m_dbManager.getDirectoryByPath(
        part.device, part.folder, d.path);
    d.inode = m_impl->m_scanner.getInode(path);
    auto ftime = fs::last_write_time(path);
    d.created_at = std::to_string(m_impl->m_scanner.getUnixTimeStamp(ftime));

    if (!existingDir.has_value()) {

      d.uuid = UuidUtils::generate();
      std::string old_path = d.path;
      dq = Utility::constructDirectoryQueueEntry(d, SyncStatus::NEW,
                                                 std::move(old_path));
      dq.priority = qPriorityToInt(QPriority::FOLDER_CREATE);

      pushDirEntry(dq);
      m_impl->m_upSyncCV.notify_all();

      m_impl->m_dbManager.insertDirectory(d, dq);

      auto syncItem = Utility::convertToActivity<DirectoryQueueEntry>(
          dq, ActivityStatus::CLOUD_FOLDER_CREATE);
      addActivity(dq.uuid, syncItem);

    } else {
      std::cout << "[syncworker] Dir Already Exists, Skipping: " << path
                << std::endl;
    }
  }
}
void SyncWorker::handleDeleted(const std::string &path) {
  // Determine if the deleted event is file or directory
  // 1. Check if the deleted event is folder
  fs::path fp(path);
  fs::path base(m_impl->m_syncPath);
  std::string relPath = "/" + fs::relative(fp, base).generic_string();
  relPath = m_impl->m_scanner.normalizePathSeparators(relPath);

  // Use the recursive DB mutex for the actual work
  std::lock_guard<std::recursive_mutex> dbLock(
      m_impl->m_dbManager.getSyncMutex());
  pathParts p = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
  auto existingDir =
      m_impl->m_dbManager.getDirectoryByPath(p.device, p.folder, relPath);
  if (existingDir.has_value()) {
    DirectoryQueueEntry dq{Utility::constructDirectoryQueueEntry(*existingDir)};
    dq.sync_status = syncStatusToString(SyncStatus::DELETE);
    dq.old_path = dq.path;
    dq.priority = qPriorityToInt(QPriority::FOLDER_DELETE);

    pushDirEntry(dq);
    m_impl->m_upSyncCV.notify_all();

    m_impl->m_dbManager.deleteFolderWithTransaction(relPath, dq);

    auto syncItem = Utility::convertToActivity<DirectoryQueueEntry>(
        dq, ActivityStatus::CLOUD_DELETE);
    addActivity(dq.uuid, syncItem);

    return;
  }
  // 2. check if the deleted event is file
  std::string filePath = fs::path(relPath).parent_path().generic_string();
  std::string filename = fs::path(relPath).filename().generic_string();
  auto existingFile = m_impl->m_dbManager.getFileByPath(filePath, filename);
  if (existingFile.has_value()) {
    FileQueueEntry fq;
    std::string old_path = fq.path;
    std::string old_filename = fq.filename;
    //    std::string sync_status = syncStatusToString(SyncStatus::DELETE);
    fq = Utility::constructFileQueueEntry(*existingFile, SyncStatus::DELETE,
                                          std::move(old_path),
                                          std::move(old_filename));
    fq.priority = qPriorityToInt(QPriority::FILE_DELETE);

    pushFileEntry(fq);
    m_impl->m_upSyncCV.notify_all();

    m_impl->m_dbManager.deleteFile(existingFile->path, existingFile->filename,
                                   fq);

    auto syncItem = Utility::convertToActivity<FileQueueEntry>(
        fq, ActivityStatus::CLOUD_DELETE);
    addActivity(fq.uuid, syncItem);

    return;
  }
};

void SyncWorker::handleRenamed(const std::string &path,
                               const std::string &oldPath, bool isMoved) {
  if (oldPath.empty()) {
    return;
  }
  if (!isMoved) {
    if (fs::is_directory(path)) {
      std::lock_guard<std::recursive_mutex> lock(
          m_impl->m_dbManager.getSyncMutex());

      fs::path fp(oldPath);
      fs::path base(m_impl->m_syncPath);
      std::string oldRelPath = "/" + fs::relative(fp, base).generic_string();
      oldRelPath = m_impl->m_scanner.normalizePathSeparators(oldRelPath);
      std::string relPath = m_impl->m_scanner.toRelativePath(path);
      pathParts o = m_impl->m_dbManager.getFolderDevice(fs::path(oldRelPath));
      pathParts n = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));

      auto existingDir = m_impl->m_dbManager.getDirectoryByPath(
          o.device, o.folder, oldRelPath);
      if (existingDir.has_value()) {
        DirectoryQueueEntry dq{
            Utility::constructDirectoryQueueEntry(*existingDir)};
        dq.sync_status = syncStatusToString(SyncStatus::RENAME);
        dq.old_path = oldRelPath;
        dq.path = relPath;
        dq.absPath = path;
        dq.device = n.device;
        dq.folder = n.folder;
        dq.priority = qPriorityToInt(QPriority::FOLDER_RENAME);

        pushDirEntry(dq);
        m_impl->m_upSyncCV.notify_all();

        m_impl->m_dbManager.moveDirectory(relPath, oldRelPath, dq);
        std::cout << "[syncworker] Folder Renamed Successfully!" << std::endl;
        auto syncItem = Utility::convertToActivity<DirectoryQueueEntry>(
            dq, ActivityStatus::CLOUD_RENAME);
        addActivity(dq.uuid, syncItem);
      }
      if (!existingDir.has_value()) {
        std::cout
            << "[syncworker] old folder name not found in DB. It has to be "
               "added as new folder"
            << std::endl;
        handleAdded(path);
        return;
      }
    }
    if (!fs::is_directory(path)) {
      // file renamed
      fs::path p(path);
      fs::path op(oldPath);
      std::string relPath = m_impl->m_scanner.toRelativePath(path);
      std::string oldRelPath = "/" + fs::relative(oldPath, m_impl->m_syncPath)
                                         .parent_path()
                                         .generic_string();

      oldRelPath = m_impl->m_scanner.normalizePathSeparators(oldRelPath);
      std::string filename = p.filename().generic_string();
      std::string oldFileName = op.filename().generic_string();

      // 1. Heavy Hashing (NO LOCK)
      std::ifstream fi(path, std::ios::binary);
      if (!fi.is_open()) {
        std::cout << "[syncworker] unable to read the file during rename-hash: "
                  << path << std::endl;
        return;
      }
      std::int64_t unixTimeStamp;
      std::uintmax_t fileSize;
      std::string hashStr = "";
      bool isPlaceholder = m_impl->m_scanner.isCloudPlaceholder(path);
      if (isPlaceholder) {
        auto meta = m_impl->m_scanner.getPlaceholderMeta(path);
        unixTimeStamp = meta.mtime;
        fileSize = meta.size;
      } else {
        hashStr = m_impl->m_scanner.calculateHash(path);
        unixTimeStamp =
            m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path));
        fileSize = fs::file_size(path);
      }
      auto inode = m_impl->m_scanner.getInode(path);
      // 2. Database Work (LOCK)
      std::lock_guard<std::recursive_mutex> lock(
          m_impl->m_dbManager.getSyncMutex());

      std::optional<FileMetadata> file =
          m_impl->m_dbManager.getFileByPath(oldRelPath, oldFileName);

      if (file.has_value()) {
        FileMetadata f;
        FileQueueEntry fq;
        f.origin = file->origin;
        f.uuid = file->uuid;
        f.path = relPath;
        f.filename = filename;
        f.last_modified = std::to_string(unixTimeStamp);
        f.hashvalue = hashStr == "" ? file->hashvalue : hashStr;
        f.size = fileSize;
        f.inode = inode;
        f.absPath = path;
        f.versions = file->versions;
        f.lastSyncedHashValue = file->lastSyncedHashValue;

        pathParts part = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
        auto dir = m_impl->m_dbManager.getDirectoryByPath(part.device,
                                                          part.folder, relPath);
        if (dir.has_value()) {
          f.dirID = dir->uuid;
        } else {
          DirectoryMetadata d;
          DirectoryQueueEntry dq;
          d.absPath = p.parent_path().generic_string();
          d.path = relPath;
          d.created_at = std::to_string(unixTimeStamp);
          d.device = part.device;
          d.folder = part.folder;
          d.uuid = UuidUtils::generate();
          d.inode = m_impl->m_scanner.getInode(path);
          std::string old_path = oldRelPath;
          dq = Utility::constructDirectoryQueueEntry(d, SyncStatus::FILE_LINKED,
                                                     std::move(old_path));
          auto dirCreateResult = m_impl->m_dbManager.insertDirectory(d, dq);
          if (dirCreateResult)
            f.dirID = d.uuid;
          else
            return;
        }
        std::string old_filename = oldFileName;
        std::string old_path = oldRelPath;

        fq = Utility::constructFileQueueEntry(f, SyncStatus::RENAME,
                                              std::move(old_path),
                                              std::move(old_filename));
        fq.priority = qPriorityToInt(QPriority::FILE_RENAME);
        f.conflictId = "";

        pushFileEntry(fq);
        m_impl->m_upSyncCV.notify_all();

        bool isRenamed = m_impl->m_dbManager.renameFile(f, fq);

        if (isRenamed)
          std::cout << "[syncworker] file renamed successfully" << std::endl;
        else
          std::cout << "[syncworker] file renamed FAILED" << std::endl;

        auto syncItem = Utility::convertToActivity<FileQueueEntry>(
            fq, ActivityStatus::CLOUD_RENAME);
        addActivity(fq.uuid, syncItem);
      }
      if (!file.has_value()) {
        std::cout << "[syncworker] old file not found in DB during rename, "
                     "adding as new: "
                  << path << std::endl;
        // Note: We are already inside a lock, but handleAdded will try to
        // re-lock. This is fine because the SyncMutex is a recursive_mutex.
        handleAdded(path);
      }
    }
  }
  if (isMoved) {
    if (fs::is_directory(path)) {
      std::string relPath = m_impl->m_scanner.toRelativePath(path);
      fs::path fp(oldPath);
      fs::path base(m_impl->m_syncPath);
      std::string rel = fs::relative(fp, base).generic_string();
      std::string oldRelPath = rel == "." ? "/" : "/" + rel;
      DirectoryQueueEntry dq;
      dq.sync_status = syncStatusToString(SyncStatus::MOVED);
      dq.old_path = oldRelPath;
      dq.path = relPath;
      pathParts p = m_impl->m_dbManager.getFolderDevice(relPath);
      dq.device = p.device;
      dq.folder = p.folder;
      dq.created_at = std::to_string(
          m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path)));
      dq.absPath = path;
      dq.inode = m_impl->m_scanner.getInode(
          fs::path(path).parent_path().generic_string());
      dq.uuid = "";
      dq.lastSynced = "";
      dq.priority = qPriorityToInt(QPriority::FOLDER_MOVED);
      pathParts op = m_impl->m_dbManager.getFolderDevice(oldRelPath);
      std::lock_guard<std::recursive_mutex> lock(
          m_impl->m_dbManager.getSyncMutex());
      auto existingDir = m_impl->m_dbManager.getDirectoryByPath(
          op.device, op.folder, oldRelPath);

      if (existingDir.has_value()) {
        dq.uuid = existingDir->uuid;
      }
      if (!existingDir.has_value()) {
        std::cout << "[syncworker] move failed as dir does not exist"
                  << std::endl;
        return;
      }

      pushDirEntry(dq);
      m_impl->m_upSyncCV.notify_all();

      bool isFolderMoved =
          m_impl->m_dbManager.moveDirectory(relPath, oldRelPath, dq);

      if (isFolderMoved) {
        std::cout << "[syncworker] " << oldRelPath << " MOVED TO =>" << relPath
                  << std::endl;
        auto syncItem = Utility::convertToActivity<DirectoryQueueEntry>(
            dq, ActivityStatus::CLOUD_MOVE);
        addActivity(dq.uuid, syncItem);

        return;
      }
      if (!isFolderMoved)
        std::cout << "[syncworker] move failed" << std::endl;
    }

    if (!fs::is_directory(path)) {
      std::string newRelPath = m_impl->m_scanner.toRelativePath(path);
      fs::path oldFileAbsPath(oldPath);
      fs::path base(m_impl->m_syncPath);
      std::string baseRelPath =
          fs::relative(oldFileAbsPath, base).parent_path().generic_string();
      std::string oldRelPath = baseRelPath == "." ? "/" : "/" + baseRelPath;
      std::string oldFilename = oldFileAbsPath.filename().generic_string();
      std::string newFilename = fs::path(path).filename().generic_string();
      std::lock_guard<std::recursive_mutex> lock(
          m_impl->m_dbManager.getSyncMutex());

      auto file = m_impl->m_dbManager.getFileByPath(oldRelPath, oldFilename);

      if (file.has_value()) {
        FileMetadata f{*file};
        FileQueueEntry fq;

        f.filename = newFilename;
        f.path = newRelPath;
        f.absPath = path;

        auto part = m_impl->m_dbManager.getFolderDevice(fs::path(newRelPath));

        auto dir = m_impl->m_dbManager.getDirectoryByPath(
            part.device, part.folder, newRelPath);

        if (dir.has_value()) {
          f.dirID = dir->uuid;
        } else {

          DirectoryMetadata d;
          DirectoryQueueEntry dq;

          d.absPath = fs::path(path).parent_path().generic_string();
          d.path = newRelPath;
          auto unixTimeStamp = m_impl->m_scanner.getUnixTimeStamp(
              fs::last_write_time(d.absPath));
          d.created_at = std::to_string(unixTimeStamp);
          d.device = part.device;
          d.folder = part.folder;
          d.uuid = UuidUtils::generate();
          d.inode = m_impl->m_scanner.getInode(d.absPath);

          std::string old_path = oldRelPath;

          dq = Utility::constructDirectoryQueueEntry(d, SyncStatus::FILE_LINKED,
                                                     std::move(old_path));

          auto dirCreateResult = m_impl->m_dbManager.insertDirectory(d, dq);

          if (dirCreateResult)
            f.dirID = d.uuid;
          else
            return;
        }
        std::string old_path = oldRelPath;
        std::string old_filename = oldFilename;

        fq = Utility::constructFileQueueEntry(
            f, SyncStatus::MOVED, std::move(old_path), std::move(old_filename));
        fq.priority = qPriorityToInt(QPriority::FILE_MOVED);

        pushFileEntry(fq);
        m_impl->m_upSyncCV.notify_all();

        bool isFileMoved = m_impl->m_dbManager.renameFile(f, fq);

        if (!isFileMoved) {
          std::cerr << "[syncworker] unable to move the file from => "
                    << oldRelPath << " to =>" << newRelPath << std::endl;
        } else {
          std::cerr << "[syncworker] file moved succeded => " << oldRelPath
                    << " to =>" << newRelPath << std::endl;
          auto syncItem = Utility::convertToActivity<FileQueueEntry>(
              fq, ActivityStatus::CLOUD_MOVE);
          addActivity(fq.uuid, syncItem);
        }
      }
      if (!file.has_value())
        std::cout << "[syncworker] file not found. It has to be treated as Add "
                     "& remove event.. Implementation pending.."
                  << std::endl;
    }
  }
}

void SyncWorker::handleModified(const std::string &path) {
  if (!fs::exists(path))
    return;
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path.c_str());

  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_UNPINNED)

  ) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
      std::cout << "[SyncWorker] DIRECTORY ATTRIBUTE CHANGED TO UNPINNED "
                << path << std::endl;
    } else {
      std::cout << "[SyncWorker] FILE ATTRIBUTE CHANGED TO UNPINNED " << path
                << std::endl;
      m_impl->deHydrateFile(path);
    }
    return;
  }
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_PINNED) &&
      (attr & FILE_ATTRIBUTE_OFFLINE)) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
      std::cout << "[SyncWorker] DIRECTORY ATTRIBUTE PINNED " << path
                << std::endl;
    } else {
      std::cout << "[SyncWorker] FILE ATTRIBUTE PINNED: FETCH_DATA: " << path
                << std::endl;
      std::string absPath = fs::path(path).make_preferred().string();
      m_impl->hydrateFile(absPath);
    }
    return;
  }

#endif
  if (!fs::is_directory(path)) {
    // 1. Heavy Hashing (NO LOCK)
    std::ifstream fi(path, std::ios::binary);
    if (!fi.is_open()) {
      std::cerr << "[syncworker] Error reading file during modify-hash: "
                << path << std::endl;
      return;
    }

    std::string hashStr = m_impl->m_scanner.calculateHash(path);
    std::int64_t unixTimeStamp =
        m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path));

    std::uintmax_t fileSize = fs::file_size(path);
    auto inode = m_impl->m_scanner.getInode(path);

    std::string filename = fs::path(path).filename().generic_string();
    std::string relPath = m_impl->m_scanner.toRelativePath(path);

    // 2. Database Work (LOCK)
    std::cout << "[SyncWorker] handleModified: waiting for lock for " << path
              << std::endl;
    std::lock_guard<std::recursive_mutex> lock(
        m_impl->m_dbManager.getSyncMutex());

    auto existingFile = m_impl->m_dbManager.getFileByPath(relPath, filename);
    if (existingFile.has_value()) {

      FileMetadata f;
      FileQueueEntry fq;

      f.filename = filename;
      f.path = relPath;
      f.absPath = path;
      f.inode = inode;
      f.hashvalue = hashStr;
      f.lastSyncedHashValue = existingFile->lastSyncedHashValue;
      f.origin = existingFile->origin;
      f.uuid = UuidUtils::generate();
      f.last_modified = std::to_string(unixTimeStamp);
      f.versions = existingFile->versions + 1;
      f.size = fileSize;
      f.dirID = existingFile->dirID;
#ifdef _WIN32
      if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_PINNED) &&
          !(attr & FILE_ATTRIBUTE_OFFLINE)) {
        if (hashStr == existingFile->hashvalue) {
          std::cout << "[SyncWorker] FILE ATTRIBUTE PINNED: ALWAYS ON DISK : "
                    << path << std::endl;
          return;
        }
      }
#endif
      auto now = std::chrono::system_clock::now().time_since_epoch();
      auto timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(now).count();

      f.lastSynced = std::to_string(timestamp);

      std::string old_path = f.path;
      std::string old_filename = f.filename;

      fq = Utility::constructFileQueueEntry(f, SyncStatus::MODIFIED,
                                            std::move(old_path),
                                            std::move(old_filename));

      fq.priority = qPriorityToInt(QPriority::FILE_MODIFIED);

      f.conflictId = "";

      pushFileEntry(fq);
      m_impl->m_upSyncCV.notify_all();

      m_impl->m_dbManager.insertFile(f, fq);

      auto syncItem = Utility::convertToActivity<FileQueueEntry>(
          fq, ActivityStatus::EDITED);
      addActivity(fq.uuid, syncItem);

      // triggerUpload();
    } else {
      std::cout
          << "[syncworker] file not found in DB during modify, adding as new: "
          << path << std::endl;
      handleAdded(path);
    }
  }
}

} // namespace sync_app
