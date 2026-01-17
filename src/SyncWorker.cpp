#include "SyncWorker.hpp"
#include "FilesystemWatcher.hpp"
#include "UuidUtils.hpp"
#include "picosha2.h"
#include "types.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
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
  std::string m_syncPath;

  std::queue<SyncEvent> m_eventQueue;
  std::mutex m_queueMtx;
  std::condition_variable m_queueCV;
  std::vector<std::thread> m_workerThreads;
  std::atomic<bool> m_running{false};

  std::map<std::string, WatchEvent> m_ignoreMap;
  std::mutex m_ignoreMtx;

  Impl(DatabaseManager &dbManager, FileSystemScanner &scanner,
       const std::string &syncPath)
      : m_dbManager(dbManager), m_scanner(scanner), m_syncPath(syncPath) {}
};

SyncWorker::SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
                       const std::string &syncPath)
    : m_impl(std::make_unique<Impl>(dbManager, scanner, syncPath)) {}

SyncWorker::~SyncWorker() { stop(); }

void SyncWorker::start() {
  if (m_impl->m_running)
    return;
  m_impl->m_running = true;
  int numThreads = 4;
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

void SyncWorker::addIgnoreEvent(const std::string &path, WatchEvent event) {
  std::lock_guard<std::mutex> lock(m_impl->m_ignoreMtx);
  m_impl->m_ignoreMap[path] = event;
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
    m_impl->m_eventQueue.push({event, path, oldPath});
  }
  m_impl->m_queueCV.notify_one();
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
      std::cout << "[syncworker] Added: " << event.path << std::endl;
      handleAdded(event.path);
      break;
    case WatchEvent::Deleted:
      std::cout << "[syncworker] Deleted: " << event.path << std::endl;
      handleDeleted(event.path);
      break;
    case WatchEvent::Modified:
      std::cout << "[syncworker] Modified: " << event.path << std::endl;
      handleModified(event.path);
      break;
    case WatchEvent::Moved:
      std::cout << "[syncworker] Moved: " << event.path
                << " oldPath: " << event.oldPath << std::endl;
      handleRenamed(event.path, event.oldPath);
      break;
    }
  }
}
void SyncWorker::handleAdded(const std::string &path) {
  if (!fs::exists(path))
    return;

  bool isDir = fs::is_directory(path);
  std::string relPath = m_impl->m_scanner.toRelativePath(path);

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
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(fi, hash.begin(), hash.end());
    std::string hashStr =
        picosha2::bytes_to_hex_string(hash.begin(), hash.end());
    std::int64_t unixTimeStamp =
        m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path));
    std::uintmax_t fileSize = fs::file_size(path);
    auto inode = m_impl->m_scanner.getInode(path);

    // 2. Database Work (LOCK)
    std::cout << "[SyncWorker] handleAdded: waiting for lock for " << path
              << std::endl;
    std::lock_guard<std::recursive_mutex> lock(
        m_impl->m_dbManager.getSyncMutex());

    std::optional<FileMetadata> file =
        m_impl->m_dbManager.getFileByPath(relPath, filename);

    if (!file.has_value()) {
      FileMetadata f;
      FileQueueEntry fq;
      f.origin = f.uuid = UuidUtils::generate();
      f.path = relPath;
      f.filename = filename;
      f.last_modified = std::to_string(unixTimeStamp);
      f.hashvalue = hashStr;
      f.size = fileSize;
      f.inode = inode;
      f.absPath = path;
      f.versions = 1;
      f.lastSyncedHashValue = f.hashvalue;

      pathParts part = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
      auto dir = m_impl->m_dbManager.getDirectoryByPath(part.device,
                                                        part.folder, f.path);
      if (dir.has_value()) {
        f.dirID = dir->uuid;
      } else {
        DirectoryMetadata d;
        DirectoryQueueEntry dq;
        d.absPath = p.parent_path().generic_string();
        d.path = f.path;
        d.created_at = std::to_string(unixTimeStamp);
        d.device = part.device;
        d.folder = part.folder;
        d.uuid = UuidUtils::generate();
        d.inode = m_impl->m_scanner.getInode(path);
        dq = DirectoryMetadata(d);
        dq.old_path = d.path;
        dq.sync_status = syncStatusToString(SyncStatus::FILE_LINKED);
        m_impl->m_dbManager.insertDirectory(d, dq);
        f.dirID = d.uuid;
      }
      fq = FileMetadata(f);
      fq.old_filename = f.filename;
      fq.old_path = f.path;
      fq.sync_status = syncStatusToString(SyncStatus::NEW);
      f.conflictId = "";
      m_impl->m_dbManager.insertFile(f, fq);
    } else {
      std::cout << "[syncworker] File Already Exists, Skipping: " << path
                << std::endl;
    }
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
      dq = DirectoryMetadata(d);
      dq.sync_status = syncStatusToString(SyncStatus::NEW);
      dq.old_path = d.path;
      m_impl->m_dbManager.insertDirectory(d, dq);
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

  std::lock_guard<std::recursive_mutex> lock(
      m_impl->m_dbManager.getSyncMutex());

  pathParts p = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
  auto existingDir =
      m_impl->m_dbManager.getDirectoryByPath(p.device, p.folder, relPath);
  if (existingDir.has_value()) {
    DirectoryQueueEntry dq(*existingDir);
    dq.sync_status = syncStatusToString(SyncStatus::DELETE);
    dq.old_path = dq.path;
    m_impl->m_dbManager.deleteFolderWithTransaction(relPath, dq);
    return;
  }
  // 2. check if the deleted event is file
  std::string filePath = fs::path(relPath).parent_path().generic_string();
  std::string filename = fs::path(relPath).filename().generic_string();
  auto existingFile = m_impl->m_dbManager.getFileByPath(filePath, filename);
  if (existingFile.has_value()) {
    FileQueueEntry fq(*existingFile);
    fq.old_path = fq.path;
    fq.old_filename = fq.filename;
    fq.sync_status = syncStatusToString(SyncStatus::DELETE);
    m_impl->m_dbManager.deleteFile(existingFile->path, existingFile->filename,
                                   fq);
    return;
  }
  // Delete event triggered by the cloudsyncworker deleting local files;
  std::cout << "[syncworker] skipping the delete event" << std::endl;
};

void SyncWorker::handleRenamed(const std::string &path,
                               const std::string &oldPath) {
  std::cout << "[syncworker] renamed event => path: " << path
            << " oldpath: " << oldPath << std::endl;
  if (oldPath.empty()) {
    return;
  }
  if (fs::is_directory(path)) {
    fs::path fp(oldPath);
    fs::path base(m_impl->m_syncPath);
    std::string oldRelPath = "/" + fs::relative(fp, base).generic_string();
    oldRelPath = m_impl->m_scanner.normalizePathSeparators(oldRelPath);
    std::string relPath = m_impl->m_scanner.toRelativePath(path);
    pathParts o = m_impl->m_dbManager.getFolderDevice(fs::path(oldRelPath));
    pathParts n = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
    auto existingDir =
        m_impl->m_dbManager.getDirectoryByPath(o.device, o.folder, oldRelPath);
    if (existingDir.has_value()) {
      DirectoryQueueEntry dq(*existingDir);
      dq.sync_status = syncStatusToString(SyncStatus::RENAME);
      dq.old_path = oldRelPath;
      dq.path = relPath;
      dq.absPath = path;
      dq.device = n.device;
      dq.folder = n.folder;
      m_impl->m_dbManager.moveDirectory(relPath, oldRelPath, dq);
    } else {
      std::cout << "[syncworker] old folder name not found in DB. It has to be "
                   "added as new folder"
                << std::endl;
      handleAdded(path);
    }
  } else {
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
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(fi, hash.begin(), hash.end());
    std::string hashStr =
        picosha2::bytes_to_hex_string(hash.begin(), hash.end());
    std::int64_t unixTimeStamp =
        m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path));
    std::uintmax_t fileSize = fs::file_size(path);
    auto inode = m_impl->m_scanner.getInode(path);

    // 2. Database Work (LOCK)
    std::cout << "[SyncWorker] handleRenamed: waiting for lock for " << path
              << std::endl;
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
      f.hashvalue = hashStr;
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
        dq = DirectoryMetadata(d);
        dq.old_path = oldRelPath;
        dq.sync_status = syncStatusToString(SyncStatus::FILE_LINKED);
        auto dirCreateResult = m_impl->m_dbManager.insertDirectory(d, dq);
        if (dirCreateResult)
          f.dirID = d.uuid;
        else
          return;
      }
      fq = FileMetadata(f);
      fq.old_filename = oldFileName;
      fq.old_path = oldRelPath;
      fq.sync_status = syncStatusToString(SyncStatus::RENAME);
      f.conflictId = "";
      m_impl->m_dbManager.insertFile(f, fq);
    } else {
      std::cout << "[syncworker] old file not found in DB during rename, "
                   "adding as new: "
                << path << std::endl;
      // Note: We are already inside a lock, but handleAdded will try to
      // re-lock. This is fine because the SyncMutex is a recursive_mutex.
      handleAdded(path);
    }
  }
}

void SyncWorker::handleModified(const std::string &path) {
  if (!fs::exists(path))
    return;

  if (!fs::is_directory(path)) {
    // 1. Heavy Hashing (NO LOCK)
    std::ifstream fi(path, std::ios::binary);
    if (!fi.is_open()) {
      std::cerr << "[syncworker] Error reading file during modify-hash: "
                << path << std::endl;
      return;
    }
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(fi, hash.begin(), hash.end());
    std::string hashStr =
        picosha2::bytes_to_hex_string(hash.begin(), hash.end());
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
      auto now = std::chrono::system_clock::now().time_since_epoch();
      auto timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(now).count();
      f.lastSynced = std::to_string(timestamp);
      fq = FileMetadata(f);
      fq.sync_status = syncStatusToString(SyncStatus::MODIFIED);
      fq.old_path = f.path;
      fq.old_filename = f.filename;
      f.conflictId = "";
      m_impl->m_dbManager.insertFile(f, fq);
    } else {
      std::cout
          << "[syncworker] file not found in DB during modify, adding as new: "
          << path << std::endl;
      handleAdded(path);
    }
  }
}

} // namespace sync_app
