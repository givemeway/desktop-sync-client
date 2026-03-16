#include "ReconciliationService.hpp"
#include "FileSystemScanner.hpp"
#include "Utility.hpp"
#include "UuidUtils.hpp"
#include "types.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sqlite_orm/sqlite_orm.h>
#include <sstream>

namespace fs = std::filesystem;

namespace sync_app {

ReconciliationService::ReconciliationService(DatabaseManager &dbManager,
                                             FileSystemScanner &scanner,
                                             ThreadPool &threadpool,
                                             const std::string &syncPath)
    : m_dbManager(dbManager), m_syncPath(syncPath), m_scanner(scanner),
      m_threadPool(threadpool) {}

std::vector<std::string>
ReconciliationService::splitDbPath(const std::string &p) {
  std::vector<std::string> segments;
  std::stringstream ss(p);
  std::string item;
  while (std::getline(ss, item, '/')) {
    if (!item.empty())
      segments.push_back(item);
  }
  return segments;
}

std::string ReconciliationService::getUniqueKey(const std::string &dir,
                                                const std::string &filename) {
  std::string normalizedDir = dir;
  if (!normalizedDir.empty() && normalizedDir.back() != '/') {
    normalizedDir += '/';
  }
  return normalizedDir + filename;
}

DirectoryMetadata ReconciliationService::createDirectoryMetadata(
    const std::string &path, bool isNewDir, const std::string &uuid) {
  DirectoryMetadata d;
  pathParts part = m_dbManager.getFolderDevice(fs::path(path));
  d.path = path;
  d.absPath = path == "/" ? m_syncPath : m_syncPath + path;
  d.device = part.device;
  d.folder = part.folder;
  d.uuid = isNewDir ? UuidUtils::generate() : uuid;
  try {
    d.inode = m_scanner.getInode(d.absPath);
    fs::path dir{d.absPath};
    auto ftime = fs::directory_entry(dir).last_write_time();
    d.created_at = std::to_string(m_scanner.getUnixTimeStamp(ftime));
  } catch (const fs::filesystem_error &e) {
    d.created_at = "";
    d.inode = "";
    std::cerr << "Error: " << e.what() << std::endl;
  }
  return d;
}

std::vector<std::string>
ReconciliationService::getPathComponents(const std::string &path) {
  if (path == "/" || path.empty()) {
    return {"/"};
  }

  std::vector<std::string> pathTree;
  std::string currentPath = "";
  std::stringstream ss(path);
  std::string token;
  while (std::getline(ss, token, '/')) {
    if (token.empty())
      continue;
    currentPath += "/" + token;
    pathTree.push_back(currentPath);
  }

  if (pathTree.empty()) {
    return {"/"};
  }
  return pathTree;
}

ReconciliationResult ReconciliationService::reconcile(
    const std::vector<CloudFileMetadata> &cloudFiles,
    const std::vector<CloudFolderMetadata> &cloudDirs,
    const std::vector<FileMetadata> &dbFiles,
    const std::vector<DirectoryMetadata> &dbDirs) {
  std::cout << "[Reconcile] Starting reconciliation loop..." << std::endl;
  ReconciliationResult result;

  // 1. Indexing Cloud State.
  std::map<std::string, CloudFileMetadata> cloudByOrigin;
  std::map<std::string, std::vector<CloudFileMetadata>> cloudByUuid;
  std::map<std::string, CloudFileMetadata> cloudPathMap;

  for (const auto &f : cloudFiles) {
    cloudByOrigin[f.origin] = f;
    cloudByUuid[f.uuid].push_back(f);
    cloudPathMap[getUniqueKey(f.path, f.filename)] = f;
  }

  // 2. Indexing DB State
  std::map<std::string, FileMetadata> dbByOrigin;
  std::map<std::string, std::vector<FileMetadata>> dbByUuid;
  std::map<std::string, FileMetadata> dbPathMap;

  for (const auto &f : dbFiles) {
    dbByOrigin[f.origin] = f;
    dbByUuid[f.uuid].push_back(f);
    dbPathMap[getUniqueKey(f.path, f.filename)] = f;
  }

  // 3. Load Local Queue
  std::optional<std::vector<FileQueueEntry>> localFileQueue;
  std::optional<std::vector<DirectoryQueueEntry>> localDirQueue;
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    localFileQueue = m_dbManager.getFileQueue();
    localDirQueue = m_dbManager.getDirectoryQueue();
  }
  std::map<std::string, FileQueueEntry> localQueueByOrigin;
  std::map<std::string, std::vector<FileQueueEntry>> localQueueByUuid;
  std::map<std::string, FileQueueEntry> localQueueByPath;

  std::map<std::string, DirectoryQueueEntry> localDirQByUuid;
  std::map<std::string, DirectoryQueueEntry> localDirQByPath;

  for (const auto &q : *localFileQueue) {
    if (!q.origin.empty())
      localQueueByOrigin[q.origin] = q;
    localQueueByUuid[q.uuid].push_back(q);
    localQueueByPath[getUniqueKey(q.path, q.filename)] = q;
  }

  for (const auto &dq : *localDirQueue) {
    localDirQByUuid[dq.uuid] = dq;
    localDirQByPath[dq.path] = dq;
  }

  // 4. Process Cloud Files
  for (const auto &cloudFile : cloudFiles) {
    std::string pathKey = getUniqueKey(cloudFile.path, cloudFile.filename);

    auto itOrigin = dbByOrigin.find(cloudFile.origin);
    auto itPath = dbPathMap.find(pathKey);

    bool isLocalModified = false;
    bool isLocalRenamed = false;
    bool isLocalFileMoved = false;

    bool isCloudRenamed = false;
    bool isCloudModified = false;
    bool isCloudFileNew = false;
    bool isCloudFileMoved = false;

    FileMetadata *localFileByOrigin =
        (itOrigin != dbByOrigin.end()) ? &itOrigin->second : nullptr;

    FileMetadata *localFileByPath =
        (itPath != dbPathMap.end()) ? &itPath->second : nullptr;

    auto localInQueue = localInQueueByAnyPath(
        cloudFile, localQueueByOrigin, localQueueByUuid, localQueueByPath);

    auto itLocalQ = localQueueByPath.find(pathKey);

    if (itLocalQ != localQueueByPath.end()) {
      isLocalModified = (itLocalQ->second.sync_status ==
                         syncStatusToString(SyncStatus::MODIFIED));
    }

    auto itLocalOR = localQueueByOrigin.find(cloudFile.origin);

    if (itLocalOR != localQueueByOrigin.end()) {
      isLocalRenamed = (itLocalOR->second.sync_status ==
                        syncStatusToString(SyncStatus::RENAME));

      isLocalFileMoved = (itLocalOR->second.sync_status ==
                          syncStatusToString(SyncStatus::MOVED));
    }

    if (!isLocalModified) {
      isCloudModified = localFileByPath
                            ? cloudFile.hashvalue != localFileByPath->hashvalue
                            : false;
    } else {
      isCloudModified =
          localFileByPath
              ? (cloudFile.hashvalue != localFileByPath->lastSyncedHashValue)
              : false;
    }

    if (isLocalRenamed) {
      auto qEntry = localQueueByOrigin[cloudFile.origin];
      isCloudRenamed =
          qEntry.old_filename && (*qEntry.old_filename != cloudFile.filename);
    } else {
      isCloudRenamed = localFileByOrigin
                           ? (localFileByOrigin->filename != cloudFile.filename)
                           : false;
    }

    if (!localFileByOrigin)
      isCloudFileNew = true;

    if (localFileByOrigin && localFileByOrigin->path != cloudFile.path &&
        !isLocalFileMoved) {
      isCloudFileMoved = true;
    }

    // New file in cloud
    if (!localFileByPath) {
      if (!localInQueue) {
        auto itDirQ = localDirQByUuid.find(cloudFile.dirID);

        if (itDirQ == localDirQByUuid.end()) {
          result.filesToDownload.push_back(cloudFile);

          continue;
        }
      }
    }

    // Existing file - handle updates / renames / conflicts
    if (localFileByOrigin) {
      if (isCloudModified && !isCloudRenamed && !isLocalModified &&
          !isLocalRenamed && !isLocalFileMoved && !isCloudFileMoved) {
        result.filesToUpdate.push_back(cloudFile);
      }
      // Conflict detection
      if (isCloudModified && !isCloudRenamed && isLocalModified &&
          !isLocalRenamed && !isLocalFileMoved && !isCloudFileMoved) {
        result.filesInConflict.push_back(cloudFile);
      }
    }

    // Resurrection protection: skip cloud version if local work is pending
    if (localInQueue && !isCloudModified) {
      continue;
    }
  }

  // 5. Deletions (Cloud -> Local)
  std::map<std::string, FileMetadata> filesToDeleteMap;

  for (const auto &dbFile : dbFiles) {
    auto itLQ = localQueueByOrigin.find(dbFile.origin);
    auto itDirQ = localDirQByUuid.find(dbFile.dirID);

    if (itLQ != localQueueByOrigin.end()) {
      const auto &status = itLQ->second.sync_status;
      if (status == syncStatusToString(SyncStatus::MODIFIED) ||
          status == syncStatusToString(SyncStatus::RENAME) ||
          status == syncStatusToString(SyncStatus::MOVED) ||
          status == syncStatusToString(SyncStatus::NEW)) {
        continue; // Skip if local work pending
      }
    }
    if (itLQ == localQueueByOrigin.end() && itDirQ != localDirQByUuid.end() &&
        itDirQ->second.sync_status !=
            syncStatusToString(SyncStatus::FILE_LINKED))
      continue;
    std::string key = getUniqueKey(dbFile.path, dbFile.filename);
    if (cloudPathMap.find(key) == cloudPathMap.end())
      result.filesToDeleteLocal.push_back(dbFile);
  }

  // 7. Directory Reconciliation (Paths are authoritative)
  std::map<std::string, CloudFolderMetadata> cloudDirMap;
  std::map<std::string, CloudFolderMetadata> cloudDirMapByUuid;

  for (const auto &d : cloudDirs) {
    if (d.path != "/")
      cloudDirMap[d.path] = d;
    cloudDirMapByUuid[d.uuid] = d;
  }

  std::map<std::string, DirectoryMetadata> dbDirMap;
  std::map<std::string, DirectoryMetadata> dbDirMapByUuid;

  for (const auto &d : dbDirs) {
    if (d.path != "/")
      dbDirMap[d.path] = d;
    dbDirMapByUuid[d.uuid] = d;
  }

  // identify new dirs to be added in local

  for (const auto &[path, cloudDir] : cloudDirMap) {
    auto itPath = dbDirMap.find(path);
    auto itUuidQ = localDirQByUuid.find(cloudDir.uuid);

    if (itPath == dbDirMap.end()) {
      // Check if already in queue
      if (itUuidQ != localDirQByUuid.end())
        continue;

      LocalFolderCreateMetadata createMeta;

      createMeta.absPath =
          cloudDir.path == "/" ? m_syncPath : m_syncPath + cloudDir.path;
      createMeta.path = cloudDir.path;
      createMeta.folder = cloudDir.folder;
      createMeta.uuid = cloudDir.uuid;
      createMeta.device = cloudDir.device;
      createMeta.created_at = cloudDir.created_at;

      auto dirPaths = getPathComponents(cloudDir.path);

      for (auto &path : dirPaths) {
        auto it = cloudDirMap.find(path);
        if (it != cloudDirMap.end()) {
          if (!createMeta.dirIDs) {
            createMeta.dirIDs = std::map<std::string, std::string>();
          }
          (*createMeta.dirIDs)[path] = it->second.uuid;
        }
      }
      result.foldersToCreateLocal.push_back(createMeta);
    }
  }

  // identify dirs for deletion in local
  for (const auto &[path, dbDir] : dbDirMap) {

    auto itPath = cloudDirMap.find(path);
    auto itUuidQ = localDirQByUuid.find(dbDir.uuid);

    if (itPath == cloudDirMap.end()) {
      if (itUuidQ != localDirQByUuid.end())
        continue;
      LocalFolderDeleteMetadata dq;
      dq.absPath = dbDir.absPath;
      dq.inode = dbDir.inode;
      dq.folder = dbDir.folder;
      dq.device = dbDir.device;
      dq.path = dbDir.path;
      dq.uuid = dbDir.uuid;
      dq.created_at = dbDir.created_at;
      result.foldersToDeleteLocal.push_back(dq);
    }
  }

  ReconciliationResult updatedResults =
      Utility::detectRenames<ReconciliationResult>(
          result.filesToDownload, result.filesToDeleteLocal,
          result.foldersToCreateLocal, result.foldersToDeleteLocal, m_syncPath);

  auto movedFiles =
      Utility::removeRedundantMovedFiles<std::vector<FileQueueEntry>>(
          updatedResults.dirsToMove, updatedResults.filesToMove);

  result.filesToMove = movedFiles;
  result.filesToDeleteLocal = updatedResults.filesToDeleteLocal;
  result.filesToDownload = updatedResults.filesToDownload;
  result.dirsToMove = updatedResults.dirsToMove;
  result.foldersToCreateLocal = updatedResults.foldersToCreateLocal;
  result.foldersToDeleteLocal = updatedResults.foldersToDeleteLocal;
  result.dirsToMove =
      Utility::reduceDirs<std::vector<DirectoryQueueEntry>>(result.dirsToMove);

  // 8. Handle Directory Renames (using inodes from local queue)

  return result;
}

std::optional<FileQueueEntry> ReconciliationService::localInQueueByAnyPath(
    const CloudFileMetadata &cloudFile,
    const std::map<std::string, FileQueueEntry> &localQueueByOrigin,
    const std::map<std::string, std::vector<FileQueueEntry>> &localQueueByUuid,
    const std::map<std::string, FileQueueEntry> &localQueueByPath) {
  auto itO = localQueueByOrigin.find(cloudFile.origin);
  if (itO != localQueueByOrigin.end())
    return itO->second;

  auto itP =
      localQueueByPath.find(getUniqueKey(cloudFile.path, cloudFile.filename));
  if (itP != localQueueByPath.end())
    return itP->second;

  return std::nullopt;
}

bool ReconciliationService::reconcileLocalState(
    const std::vector<ScannedFile> &scannedFiles,
    const std::vector<ScannedDirectory> &scannedDirs) {
  std::cout << "[Reconcile] Reconciling local filesystem with database..."
            << std::endl;

  // 1. Fetch current DB state
  auto dbFiles = m_dbManager.getAllFiles();
  auto dbDirs = m_dbManager.getAllDirectories();

  // Index DB State
  std::map<std::string, FileMetadata> dbFilesPathMap;
  for (const auto &f : *dbFiles) {
    dbFilesPathMap[getUniqueKey(f.path, f.filename)] = f;
  }

  std::map<std::string, DirectoryMetadata> dbDirsPathMap;
  for (const auto &d : *dbDirs) {
    std::string path = d.path;
    if (path.length() > 1 && path.back() == '/')
      path.pop_back(); // Normalize
    if (path != "/")
      dbDirsPathMap[path] = d;
  }

  // Index Scan State
  std::map<std::string, ScannedFile> scanFilesMap;
  for (const auto &f : scannedFiles) {
    scanFilesMap[getUniqueKey(f.path, f.filename)] = f;
  }

  std::map<std::string, ScannedDirectory> scanDirsMap;
  for (const auto &d : scannedDirs) {
    scanDirsMap[d.path] = d;
  }

  std::map<std::string, std::string> dirIDsMap;
  std::map<std::string, DirectoryMetadata> dirsToAddMap;

  std::vector<FileQueueEntry> filesToDelete;
  std::vector<FileMetadata> filesToAdd;
  std::vector<FileMetadata> filesToModify;

  std::vector<DirectoryMetadata> dirsToAdd;
  std::vector<DirectoryQueueEntry> dirsToDelete;

  // Check for NEW directories
  for (const auto &[path, sDir] : scanDirsMap) {
    auto itPath = dbDirsPathMap.find(path);

    if (itPath == dbDirsPathMap.end()) {
      std::cout << "[Reconcile] Offline DIR ADD detected: " << path
                << std::endl;
      DirectoryMetadata q;
      pathParts p = m_dbManager.getFolderDevice(fs::path(sDir.path));
      q.path = sDir.path;
      q.device = p.device;
      q.folder = sDir.name;
      q.absPath = sDir.absPath;
      q.inode = sDir.inode;
      q.created_at = std::to_string(sDir.mtime);
      q.uuid = UuidUtils::generate();
      dirsToAddMap[q.path] = q;
    }
  }

  // Check for DELETED directories
  for (const auto &[path, dbDir] : dbDirsPathMap) {
    auto itPath = scanDirsMap.find(path);

    if (itPath == scanDirsMap.end()) {
      std::cout << "[Reconcile] Offline DIR DELETE detected: " << path
                << std::endl;
      auto old_path = dbDir.path;
      DirectoryQueueEntry q{Utility::constructDirectoryQueueEntry(
          dbDir, SyncStatus::DELETE, std::move(old_path))};
      dirsToDelete.push_back(q);
    }
  }

  // Check for NEW or MODIFIED files
  for (const auto &[key, sFile] : scanFilesMap) {
    auto itPath = dbFilesPathMap.find(key);
    if (itPath != dbFilesPathMap.end()) {
      const auto &dbFile = itPath->second;
      if (dbFile.hashvalue != sFile.hash) {
        // Modified File
        std::cout << "[Reconcile] Offline MODIFY detected: " << key
                  << std::endl;
        FileMetadata f;
        f.uuid = UuidUtils::generate();
        f.path = sFile.path;
        f.filename = sFile.filename;
        f.last_modified = std::to_string(sFile.mtime);
        f.hashvalue = sFile.hash;
        f.size = sFile.size;
        f.inode = sFile.inode;
        f.absPath = sFile.absPath;
        f.dirID = dbFile.dirID;
        f.versions = dbFile.versions + 1;
        f.origin = dbFile.origin;
        f.lastSyncedHashValue = dbFile.hashvalue;
        filesToModify.push_back(f);
      }
      continue;
    }
    if (itPath == dbFilesPathMap.end()) {
      std::cout << "[Reconcile] Offline ADD detected: " << key << std::endl;
      FileMetadata f;
      f.uuid = f.origin = UuidUtils::generate();
      f.path = sFile.path;
      f.filename = sFile.filename;
      f.last_modified = std::to_string(sFile.mtime);
      f.hashvalue = sFile.hash;
      f.size = sFile.size;
      f.inode = sFile.inode;
      f.absPath = sFile.absPath;
      DirectoryMetadata d;
      d = createDirectoryMetadata(f.path);
      auto itDirID = dirIDsMap.find(f.path);
      if (itDirID != dirIDsMap.end()) {
        f.dirID = itDirID->second;
      } else {
        auto existingDir =
            m_dbManager.getDirectoryByPath(d.device, d.folder, d.path);
        if (existingDir.has_value()) {
          f.dirID = existingDir->uuid;
          dirIDsMap[d.path] = existingDir->uuid;
        } else {
          auto itDir = dirsToAddMap.find(d.path);
          if (itDir != dirsToAddMap.end()) {
            f.dirID = itDir->second.uuid;
            dirIDsMap[d.path] = itDir->second.uuid;
          } else {
            dirsToAddMap[d.path] = d;
          }
        }
      }
      filesToAdd.push_back(f);
    }
  }

  // Check for DELETED files

  for (const auto &[key, dbFile] : dbFilesPathMap) {
    auto itPath = scanFilesMap.find(key);

    if (itPath == scanFilesMap.end()) {
      std::cout << "[Reconcile] Offline DELETE detected: " << key << std::endl;
      FileQueueEntry fq;
      auto old_path = dbFile.path;
      auto old_filename = dbFile.filename;
      fq = Utility::constructFileQueueEntry(dbFile, SyncStatus::DELETE,
                                            std::move(old_path),
                                            std::move(old_filename));
      filesToDelete.push_back(fq);
    }
  }

  std::transform(dirsToAddMap.begin(), dirsToAddMap.end(),
                 std::back_inserter(dirsToAdd),
                 [&](const auto &pair) { return pair.second; });

  OfflineReconResult updated = Utility::detectRenames<OfflineReconResult>(
      filesToAdd, filesToDelete, dirsToAdd, dirsToDelete, m_syncPath);

  auto movedFiles =
      Utility::removeRedundantMovedFiles<std::vector<FileQueueEntry>>(
          updated.dirsToMove, updated.filesToMove);
  auto deletedFiles =
      Utility::removeRedundantMovedFiles<std::vector<FileQueueEntry>>(
          updated.foldersToDeleteLocal, updated.filesToDeleteLocal);
  auto reducedDeletedDirs = Utility::reduceDirs(updated.foldersToDeleteLocal);

  updated.filesToMove = movedFiles;
  updated.filesToDeleteLocal = deletedFiles;
  updated.foldersToDeleteLocal = reducedDeletedDirs;

  std::cout << "[offline reconcile] filesToDownload : "
            << updated.filesToDownload.size() << std::endl;

  std::cout << "[offline reconcile] filesToDelete : "
            << updated.filesToDeleteLocal.size() << std::endl;

  std::cout << "[offline reconcile] filesToMove : "
            << updated.filesToMove.size() << std::endl;

  std::cout << "[offline reconcile] dirsToMove : " << updated.dirsToMove.size()
            << std::endl;

  std::cout << "[offline reconcile] dirsToAdd : "
            << updated.foldersToCreateLocal.size() << std::endl;

  std::cout << "[offline reconcile] dirsToDelete : "
            << updated.foldersToDeleteLocal.size() << std::endl;

  std::cout << "[offline reconcile] filesToModify : " << filesToModify.size()
            << std::endl;

  bool isLocalDBUpdated = m_dbManager.reconcileLocalState(
      updated.filesToDownload, updated.filesToDeleteLocal,
      updated.foldersToCreateLocal, updated.foldersToDeleteLocal, filesToModify,
      updated.filesToMove, updated.dirsToMove);
  std::cout << "[reconcile] isLocalDBUPdated : " << isLocalDBUpdated
            << std::endl;
  return isLocalDBUpdated;
}

} // namespace sync_app
