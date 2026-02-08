#include "SyncWorker.hpp"
#include "FilesystemWatcher.hpp"
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
#include <picosha2.h>
#include <set>
#include <string>
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
  std::map<std::string, std::vector<SyncEvent>> m_eventMap;
  std::mutex m_queueMtx;
  std::condition_variable m_queueCV;
  std::vector<std::thread> m_workerThreads;
  std::atomic<bool> m_running{false};
  ThreadPool &m_threadPool;

  std::map<std::string, WatchEvent> m_ignoreMap;
  std::mutex m_moveMtx;
  std::condition_variable m_moveCV;
  std::set<std::string> m_activeInodes; // Track inodes currently being deleted
  std::mutex m_ignoreMtx;

  Impl(DatabaseManager &dbManager, FileSystemScanner &scanner,
       ThreadPool &threadPool, const std::string &syncPath)
      : m_dbManager(dbManager), m_scanner(scanner), m_threadPool(threadPool),
        m_syncPath(syncPath) {}
};

SyncWorker::SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
                       ThreadPool &threadPool, const std::string &syncPath)
    : m_impl(std::make_unique<Impl>(dbManager, scanner, threadPool, syncPath)) {
}

// SyncWorker::~SyncWorker() = default;
SyncWorker::~SyncWorker() { stop(); }
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
      std::cout << "[syncworker] Enqueueing Moved: " << event.path
                << " oldPath: " << event.oldPath << std::endl;
      m_impl->m_threadPool.enqueue(
          [this, path = event.path, oldPath = event.oldPath]() {
            handleRenamed(path, oldPath, true);
          });
      break;
    case WatchEvent::Renamed:
      std::cout << "[syncworker] Enqueueing Rename: " << event.path
                << "oldPath: " << event.oldPath << std::endl;
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
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(fi, hash.begin(), hash.end());
    std::string hashStr =
        picosha2::bytes_to_hex_string(hash.begin(), hash.end());
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
      bool isFileInserted = m_impl->m_dbManager.insertFile(f, fq);
      if (!isFileInserted)
        std::cout << "[syncworker] file added into db FAILED" << f.absPath
                  << std::endl;
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
      std::string old_path = d.path;
      dq = Utility::constructDirectoryQueueEntry(d, SyncStatus::NEW,
                                                 std::move(old_path));
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
    m_impl->m_dbManager.deleteFolderWithTransaction(relPath, dq);
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
    m_impl->m_dbManager.deleteFile(existingFile->path, existingFile->filename,
                                   fq);
    return;
  }
  // Delete event triggered by the cloudsyncworker deleting local files;
  std::cout << "[syncworker] skipping delete: " << path << std::endl;
  std::cout << "[syncworker] skipping delete RelPath: " << relPath << std::endl;
  std::cout << "[syncworker] skipping delete device: " << p.device
            << " folder: " << p.folder << std::endl;
  std::cout << "[syncworker] skipping delete filePath: " << filePath
            << " filename: " << filename << std::endl;
};

void SyncWorker::handleRenamed(const std::string &path,
                               const std::string &oldPath, bool isMoved) {
  if (oldPath.empty()) {
    return;
  }
  if (!isMoved) {
    if (fs::is_directory(path)) {
      fs::path fp(oldPath);
      fs::path base(m_impl->m_syncPath);
      std::string oldRelPath = "/" + fs::relative(fp, base).generic_string();
      oldRelPath = m_impl->m_scanner.normalizePathSeparators(oldRelPath);
      std::string relPath = m_impl->m_scanner.toRelativePath(path);
      pathParts o = m_impl->m_dbManager.getFolderDevice(fs::path(oldRelPath));
      pathParts n = m_impl->m_dbManager.getFolderDevice(fs::path(relPath));

      std::lock_guard<std::recursive_mutex> lock(
          m_impl->m_dbManager.getSyncMutex());

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
        m_impl->m_dbManager.moveDirectory(relPath, oldRelPath, dq);
      } else {
        std::cout
            << "[syncworker] old folder name not found in DB. It has to be "
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
        //        fq.sync_status = syncStatusToString(SyncStatus::RENAME);

        fq = Utility::constructFileQueueEntry(f, SyncStatus::RENAME,
                                              std::move(old_path),
                                              std::move(old_filename));
        f.conflictId = "";
        m_impl->m_dbManager.insertFile(f, fq);
        // triggerUpload();
      } else {
        std::cout << "[syncworker] old file not found in DB during rename, "
                     "adding as new: "
                  << path << std::endl;
        // Note: We are already inside a lock, but handleAdded will try to
        // re-lock. This is fine because the SyncMutex is a recursive_mutex.
        handleAdded(path);
      }
    }
  } else {
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
      pathParts op = m_impl->m_dbManager.getFolderDevice(oldRelPath);
      std::lock_guard<std::recursive_mutex> lock(
          m_impl->m_dbManager.getSyncMutex());
      auto existingDir = m_impl->m_dbManager.getDirectoryByPath(
          op.device, op.folder, oldRelPath);
      if (existingDir.has_value()) {
        dq.uuid = existingDir->uuid;
      } else {
        std::cout << "[syncworker] move failed as dir does not exist"
                  << std::endl;
        return;
      }
      bool isFolderMoved =
          m_impl->m_dbManager.moveDirectory(relPath, oldRelPath, dq);
      if (isFolderMoved) {
        std::cout << "[syncworker] folder " << oldRelPath << " moved to=>"
                  << relPath << std::endl;
        return;
      }
      std::cout << "[syncworker] move failed" << std::endl;
    } else {
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
        std::string old_path = oldRelPath;
        std::string old_filename = oldFilename;
        //        fq.sync_status = syncStatusToString(SyncStatus::MOVED);
        fq = Utility::constructFileQueueEntry(*file, SyncStatus::MOVED,
                                              std::move(old_path),
                                              std::move(old_filename));
        bool isFileMoved = m_impl->m_dbManager.moveFile(f, fq);
        if (!isFileMoved) {
          std::cerr << "[syncworker] unable to move the file from => "
                    << oldRelPath << " to =>" << newRelPath << std::endl;
        }
      } else {
        std::cout << "[syncworker] file not found. It has to be treated as Add "
                     "& remove event.. Implementation pending.."
                  << std::endl;
      }
    }

    /*if (isMoved) {

      std::unique_lock<std::mutex> lock(m_impl->m_moveMtx);
      m_impl->m_moveCV.wait(
          lock, [&]() { return !m_impl->m_activeInodes.contains(inode); });
      auto scannedItems = m_impl->m_scanner.scanSyncPath(path);
      ScannedDirectory parent;
      parent.absPath = path;
      parent.path = relPath;
      parent.inode = m_impl->m_scanner.getInode(path);
      parent.mtime =
          m_impl->m_scanner.getUnixTimeStamp(fs::last_write_time(path));
      parent.name =
          m_impl->m_dbManager.getFolderDevice(fs::path(relPath)).folder;
      scannedItems.directories.push_back(parent);
      removeEventMap(inode, path);
      {
        std::lock_guard<std::recursive_mutex> lock(
            m_impl->m_dbManager.getSyncMutex());
        auto queueItems = m_impl->m_dbManager.getDirQueueByInode(inode);
        if (queueItems.has_value()) {
          auto [filesQ, dirsQ] = *queueItems;
          std::map<std::string, std::vector<FileQueueEntry>> filesMap{};
          std::map<std::string, DirectoryQueueEntry> dirsMap{};
          std::vector<FileMetadata> files;
          std::vector<DirectoryMetadata> dirs;
          for (auto &f : filesQ) {
            filesMap[f.inode].push_back(f);
          }
          for (auto &d : dirsQ) {
            dirsMap[d.inode] = d;
          }

          for (auto &f : scannedItems.files) {
            auto it = filesMap.find(f.inode);
            if (it != filesMap.end()) {
              for (auto &i : it->second) {
                auto file = constructFileMetadata(f, i);
                files.push_back(file);
              }
            } else {
              pathParts p =
                  m_impl->m_dbManager.getFolderDevice(fs::path(f.path));
              auto d = m_impl->m_dbManager.getDirectoryByPath(p.device,
                                                               p.folder,
    f.path); FileMetadata file{}; if (d.has_value()) file =
    constructFileMetadata(f, {}, d->uuid); else {
                // create a new directory path;
              }
              files.push_back(file);
            }
          }

          for (auto &d : scannedItems.directories) {
            auto it = dirsMap.find(d.inode);
            if (it != dirsMap.end()) {
              auto dir = constructDirectoryMetadata(d, it->second);
              dirs.push_back(dir);
            } else {
              auto dir = constructDirectoryMetadata(d, {});
              dirs.push_back(dir);
            }
          }

          auto isInserted = m_impl->m_dbManager.insertFilesAndDirectories(
              files, dirs, relPath, oldRelPath);
          if (!isInserted) {
            std::cout
                << "[syncworker] unable to insert items from moved directory
                = > "
                        << path << std::endl;
          }
        } else {
          // if the queue entries are empty
        }
      }
      return;
    } else if (isMoved && !isDir && !inode.empty()) {
      // handle file move
      fs::path fp(oldPath);
      fs::path base(m_impl->m_syncPath);
      std::string rel = fs::relative(fp, base).string();
      std::string oldRelPath = rel == "." ? "/" : "/" + rel;
      std::unique_lock<std::mutex> lock(m_impl->m_moveMtx);
      std::string filename = fs::path(oldPath).filename().string();
      m_impl->m_moveCV.wait(
          lock, [&]() { return !m_impl->m_activeInodes.contains(inode); });

      {
        std::lock_guard<std::recursive_mutex> lk(
            m_impl->m_dbManager.getSyncMutex());
        auto qFile =
            m_impl->m_dbManager.getFileQueueByPath(oldRelPath, filename);
        if (qFile.has_value()) {
          FileQueueEntry fq;
          FileMetadata f;
          if (qFile->sync_status == syncStatusToString(SyncStatus::DELETE)) {
            f.filename = qFile->filename;
            f.path = relPath; // new path f.absPath =
            path;
            f.inode = inode;
            f.hashvalue = qFile->hashvalue;
            f.lastSyncedHashValue = qFile->hashvalue;
            f.lastSynced = ""; // since the
            move it not yet moved to
                // sync keep it empty
                f.last_modified = qFile->last_modified;
            f.origin = qFile->origin;
            f.uuid = qFile->uuid;
            f.size = qFile->size;
            pathParts p =
                m_impl->m_dbManager.getFolderDevice(fs::path(relPath));
            auto dir = m_impl->m_dbManager.getDirectoryByPath(
                p.device, p.folder, relPath);
            if (!dir.has_value()) {
              // create path in the main dir table;
              f.dirID = ""; // get the new dirID from where the DB belongs to
                            // after creating it
            } else {
              f.dirID = dir->uuid;
            }
            f.versions = qFile->versions;
            fq = FileMetadata(f);
            fq.sync_status = syncStatusToString(SyncStatus::MOVED);
            fq.old_path = oldRelPath;
            fq.old_filename = qFile->old_filename;
            bool isMoveUpdated = m_impl->m_dbManager.updateMovedFile(fq, f);
            if (!isMoveUpdated) {
              std::cout << "[syncworker] file move failed : " << std::endl;
            } else {
              std::cout << "[syncworker] file move successful : from ->"
                        << oldRelPath << " ==> " << relPath << std::endl;
            }
          }
        } else {
          // deleted file not found in Queue so treat it as a normal file
          addition
        }
      }
      return;
    }
    */
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
      //      fq.sync_status = syncStatusToString(SyncStatus::MODIFIED);
      std::string old_path = f.path;
      std::string old_filename = f.filename;
      fq = Utility::constructFileQueueEntry(f, SyncStatus::MODIFIED,
                                            std::move(old_path),
                                            std::move(old_filename));
      f.conflictId = "";
      m_impl->m_dbManager.insertFile(f, fq);
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
