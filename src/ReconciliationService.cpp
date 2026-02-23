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

FileQueueEntry ReconciliationService::createFileMetadata(
    const FileMetadata &file, const ScannedFile &sFile, bool isNewFile,
    const std::string &dirID) {
  FileMetadata f;
  f.uuid = isNewFile ? UuidUtils::generate() : file.uuid;
  f.path = sFile.path;
  f.filename = sFile.filename;
  f.last_modified = std::to_string(sFile.mtime);
  f.hashvalue = sFile.hash;
  f.size = sFile.size;
  f.inode = sFile.inode;
  f.absPath = sFile.absPath;
  f.versions = isNewFile ? 1 : file.versions;
  f.origin = isNewFile ? f.uuid : file.origin;
  f.lastSyncedHashValue = isNewFile ? sFile.hash : file.lastSyncedHashValue;
  f.dirID = !isNewFile ? dirID : "";
  FileQueueEntry fq;
  fq = Utility::constructFileQueueEntry(f);
  return fq;
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

std::optional<PathDiff>
ReconciliationService::findRenameDepthFromPath(const std::string &oldPath,
                                               const std::string &newPath) {
  auto oldSegs = splitDbPath(oldPath);
  auto newSegs = splitDbPath(newPath);

  size_t len = std::min(oldSegs.size(), newSegs.size());
  size_t idx = 0;

  while (idx < len && oldSegs[idx] == newSegs[idx]) {
    idx++;
  }

  if (idx == len && oldSegs.size() == newSegs.size()) {
    return std::nullopt;
  }

  PathDiff diff;
  diff.depth = static_cast<int32_t>(idx);
  if (idx < oldSegs.size())
    diff.oldSegment = oldSegs[idx];
  if (idx < newSegs.size())
    diff.newSegment = newSegs[idx];
  return diff;
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
  auto localFileQueue = m_dbManager.getFileQueue();
  auto localDirQueue = m_dbManager.getDirectoryQueue();

  std::map<std::string, FileQueueEntry> localQueueByOrigin;
  std::map<std::string, std::vector<FileQueueEntry>> localQueueByUuid;
  std::map<std::string, FileQueueEntry> localQueueByPath;

  for (const auto &q : *localFileQueue) {
    if (!q.origin.empty())
      localQueueByOrigin[q.origin] = q;
    localQueueByUuid[q.uuid].push_back(q);
    localQueueByPath[getUniqueKey(q.path, q.filename)] = q;
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
        result.filesToDownload.push_back(cloudFile);
        continue;
      }
    }

    // Existing file - handle updates / renames / conflicts
    if (localFileByOrigin) {
      if (isCloudModified && !isCloudRenamed && !isLocalModified &&
          !isLocalRenamed && !isLocalFileMoved && !isCloudFileMoved) {
        result.filesToUpdate.push_back(cloudFile);
      }
      if (!isCloudModified && isCloudRenamed && !isLocalModified &&
          !isLocalRenamed && !isLocalFileMoved && !isCloudFileMoved) {
        // result.filesToRename.push_back({*localFileByOrigin, cloudFile});
      }
      // Conflict detection
      if (isCloudModified && !isCloudRenamed && isLocalModified &&
          !isLocalRenamed && !isLocalFileMoved && !isCloudFileMoved) {
        result.filesInConflict.push_back(cloudFile);
      }
      if (!isCloudModified && !isCloudRenamed && !isLocalModified &&
          !isLocalRenamed && !isLocalFileMoved && isCloudFileMoved) {
        //        result.filesToMove.push_back({*localFileByOrigin, cloudFile});
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
    std::string key = getUniqueKey(dbFile.path, dbFile.filename);
    if (cloudPathMap.find(key) == cloudPathMap.end()) {
      auto itLQ = localQueueByOrigin.find(dbFile.origin);
      if (itLQ != localQueueByOrigin.end()) {
        const auto &status = itLQ->second.sync_status;
        if (status == syncStatusToString(SyncStatus::MODIFIED) ||
            status == syncStatusToString(SyncStatus::RENAME) ||
            status == syncStatusToString(SyncStatus::MOVED) ||
            status == syncStatusToString(SyncStatus::NEW)) {
          continue; // Skip if local work pending
        }
      }
      filesToDeleteMap[key] = dbFile;
    }
  }

  // 6. Safety Filter: remove from delete if it was actually a rename
  for (const auto &rename : result.filesToRename) {
    std::string oldKey =
        getUniqueKey(rename.oldFile.path, rename.oldFile.filename);
    filesToDeleteMap.erase(oldKey);
  }

  for (auto const &[key, val] : filesToDeleteMap) {
    result.filesToDeleteLocal.push_back(val);
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
  std::map<std::string, std::vector<LocalDirRenameMetadata>>
      renameDirCandidates{};

  for (const auto &[path, cloudDir] : cloudDirMap) {

    auto itUuid = dbDirMapByUuid.find(cloudDir.uuid);

    auto itPath = dbDirMap.find(path);

    if (itUuid != dbDirMapByUuid.end() && itPath != dbDirMap.end()) {
      renameDirCandidates[cloudDir.uuid].push_back({itUuid->second, cloudDir});
    }

    if (itPath == dbDirMap.end()) {
      // Check if already in queue
      auto dirsInQ = m_dbManager.getDirectoryQueue();
      bool alreadyInQ = std::any_of(
          dirsInQ->begin(), dirsInQ->end(), [&](const DirectoryQueueEntry &e) {
            return e.path == path && e.device == cloudDir.device &&
                   e.folder == cloudDir.folder;
          });

      if (!alreadyInQ) {
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
  }

  for (const auto &[path, dbDir] : dbDirMap) {

    auto itUuid = cloudDirMapByUuid.find(dbDir.uuid);

    auto itPath = cloudDirMap.find(path);

    if (itUuid != cloudDirMapByUuid.end() && itPath == cloudDirMap.end()) {
      renameDirCandidates[dbDir.uuid].push_back({dbDir, itUuid->second});
    }

    if (itPath == cloudDirMap.end()) {
      auto dirsInQ = m_dbManager.getDirectoryQueue();
      bool alreadyInQ = std::any_of(
          dirsInQ->begin(), dirsInQ->end(),
          [&](const DirectoryQueueEntry &e) { return e.path == path; });

      if (!alreadyInQ) {
        LocalFolderDeleteMetadata dq;
        dq.absPath = dbDir.absPath;
        dq.folder = dbDir.folder;
        dq.device = dbDir.device;
        dq.path = dbDir.path;
        dq.uuid = dbDir.uuid;
        dq.created_at = dbDir.created_at;
        result.foldersToDeleteLocal.push_back(dq);
      }
    }
  }
  ReconciliationResult updatedResults =
      Utility::detectRenames<ReconciliationResult>(
          result.filesToDownload, result.filesToDeleteLocal,
          result.foldersToCreateLocal, result.foldersToDeleteLocal);

  auto movedFiles =
      Utility::removeRedundantMovedFiles<std::vector<FileQueueEntry>>(
          updatedResults.dirsToMove, updatedResults.filesToMove);

  result.filesToMove = movedFiles;
  result.filesToDeleteLocal = updatedResults.filesToDeleteLocal;
  result.filesToDownload = updatedResults.filesToDownload;
  result.dirsToMove = updatedResults.dirsToMove;
  result.foldersToCreateLocal = updatedResults.foldersToCreateLocal;
  result.foldersToDeleteLocal = updatedResults.foldersToDeleteLocal;

  // 8. Handle Directory Renames (using inodes from local queue)
  /*
  std::vector<RenameInfo> renames = detectDirRenames(*localDirQueue);
  std::vector<RenameInfo> collapsed = collapseDirRenames(renames);
  reconcileDirRenamedCandidates(collapsed);
   */
  return result;
}

std::vector<RenameInfo> ReconciliationService::detectDirRenames(
    const std::vector<DirectoryQueueEntry> &entries) {
  std::map<std::string, std::vector<DirectoryQueueEntry>> byInode;
  for (const auto &e : entries) {
    byInode[e.inode].push_back(e);
  }

  std::vector<RenameInfo> renames;
  for (auto const &[inode, group] : byInode) {
    std::vector<DirectoryQueueEntry> deletes;
    std::vector<DirectoryQueueEntry> news;

    for (const auto &e : group) {
      if (e.sync_status == syncStatusToString(SyncStatus::DELETE))
        deletes.push_back(e);
      else if (e.sync_status == syncStatusToString(SyncStatus::NEW))
        news.push_back(e);
    }

    if (deletes.empty() || news.empty())
      continue;

    // Pick top-most (shortest path) for comparison
    auto oldEntry = *std::min_element(
        deletes.begin(), deletes.end(),
        [](const DirectoryQueueEntry &a, const DirectoryQueueEntry &b) {
          std::stringstream ss_1{a.path};
          std::string token_1;
          std::vector<std::string> seg_1;

          while (std::getline(ss_1, token_1, '/')) {
            if (!token_1.empty())
              seg_1.push_back(token_1);
          }

          std::stringstream ss_2{b.path};
          std::string token_2;
          std::vector<std::string> seg_2;

          while (std::getline(ss_2, token_2, '/')) {
            if (!token_2.empty())
              seg_2.push_back(token_2);
          }

          return seg_1.size() < seg_2.size();
        });
    auto newEntry = *std::min_element(
        news.begin(), news.end(),
        [](const DirectoryQueueEntry &a, const DirectoryQueueEntry &b) {
          std::stringstream ss_1{a.path};
          std::string token_1;
          std::vector<std::string> seg_1;

          while (std::getline(ss_1, token_1, '/')) {
            if (!token_1.empty())
              seg_1.push_back(token_1);
          }

          std::stringstream ss_2{b.path};
          std::string token_2;
          std::vector<std::string> seg_2;

          while (std::getline(ss_2, token_2, '/')) {
            if (!token_2.empty())
              seg_2.push_back(token_2);
          }

          return seg_1.size() < seg_2.size();
        });

    auto diff = findRenameDepthFromPath(oldEntry.path, newEntry.path);
    if (!diff)
      continue;

    RenameInfo info;
    info.inode = inode;
    info.uuid = newEntry.uuid;
    info.device = newEntry.device;
    info.folder = newEntry.folder;
    info.created_at = newEntry.created_at;
    info.depth = diff->depth;
    info.oldSegment = diff->oldSegment;
    info.newSegment = diff->newSegment;
    info.oldPath = oldEntry.path;
    info.newPath = newEntry.path;
    renames.push_back(info);
  }
  return renames;
}

std::vector<RenameInfo> ReconciliationService::collapseDirRenames(
    const std::vector<RenameInfo> &renames) {
  std::map<std::string, RenameInfo> bySegmentChange;
  for (const auto &r : renames) {
    std::string key = (r.oldSegment ? *r.oldSegment : "") + "=>" +
                      (r.newSegment ? *r.newSegment : "");

    auto it = bySegmentChange.find(key);
    if (it == bySegmentChange.end() ||
        r.oldPath.length() < it->second.oldPath.length()) {
      bySegmentChange[key] = r;
    }
  }

  std::vector<RenameInfo> result;
  for (auto const &[key, val] : bySegmentChange) {
    result.push_back(val);
  }
  return result;
}

void ReconciliationService::reconcileDirRenamedCandidates(
    const std::vector<RenameInfo> &localFoldersRenamed) {
  for (const auto &dir : localFoldersRenamed) {
    std::cout << "[Reconcile] Detected Directory Rename: " << dir.oldPath
              << " -> " << dir.newPath << std::endl;

    DirectoryQueueEntry dq;
    dq.uuid = dir.uuid;
    dq.created_at = dir.created_at;
    dq.inode = dir.inode;
    dq.device = dir.device;
    dq.folder = dir.newSegment ? *dir.newSegment : dir.folder;
    dq.path = dir.newPath;
    dq.old_path = dir.oldPath;
    dq.sync_status = syncStatusToString(SyncStatus::RENAME);
    std::string abspath =
        dir.newPath == "/" ? m_syncPath : m_syncPath + dir.newPath;
    dq.absPath = abspath;

    bool isQDeleted =
        m_dbManager.deleteOrphanItemsInQueue(dq.path, *dq.old_path);
    if (isQDeleted)
      m_dbManager.upsertDirectoryQueue(dq);
  }
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

void ReconciliationService::reconcileLocalState(
    const std::vector<ScannedFile> &scannedFiles,
    const std::vector<ScannedDirectory> &scannedDirs) {
  std::cout << "[Reconcile] Reconciling local filesystem with database..."
            << std::endl;

  // 1. Fetch current DB state
  auto dbFiles = m_dbManager.getAllFiles();
  auto dbDirs = m_dbManager.getAllDirectories();
  //  bool isFiQDeleted = m_dbManager.deleteAllFilesInQueue();
  // bool isDiQDeleted = m_dbManager.deleteAllDirsInQueue();

  // if (!isDiQDeleted || !isDiQDeleted)
  //  return;

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

  std::map<std::string, std::vector<ScannedFile>> scanFilesMapByInode;
  for (const auto &f : scannedFiles) {
    scanFilesMapByInode[f.inode].push_back(f);
  }

  std::map<std::string, ScannedDirectory> scanDirsMapByInode;
  for (const auto &d : scannedDirs) {
    scanDirsMapByInode[d.inode] = d;
  }

  std::map<std::string, std::vector<FileMetadata>> dbFilesByInode;
  for (const auto &f : *dbFiles) {
    dbFilesByInode[f.inode].push_back(f);
  }

  std::map<std::string, DirectoryMetadata> dbDirsByInode;
  for (const auto &d : *dbDirs) {
    dbDirsByInode[d.inode] = d;
  }

  std::vector<FileQueueEntry> movedFiles;
  std::vector<DirectoryQueueEntry> movedDirs;

  std::map<std::string, DirectoryQueueEntry> movedDirsMap;

  std::map<std::string, std::string> dirIDsMap;
  std::map<std::string, DirectoryMetadata> dirsToAddMap;

  std::vector<FileQueueEntry> filesToDelete;
  std::vector<FileMetadata> filesToAdd;
  std::vector<FileMetadata> filesToModify;

  std::vector<DirectoryMetadata> dirsToAdd;
  std::vector<DirectoryQueueEntry> dirsToDelete;
  // 2. Identify Directory Changes

  // Check for NEW directories
  for (const auto &[path, sDir] : scanDirsMap) {
    // auto itUuid = dbDirsByInode.find(sDir.inode);
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
      DirectoryQueueEntry q{Utility::constructDirectoryQueueEntry(dbDir)};
      q.sync_status = syncStatusToString(SyncStatus::DELETE);
      dirsToDelete.push_back(q);
    }
  }
  // 2. Identify File Changes

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
      fq = Utility::constructFileQueueEntry(dbFile, SyncStatus::DELETE);
      filesToDelete.push_back(fq);
    }
  }
  /*
    std::map<std::string, std::vector<FileQueueEntry>> movedFileCandidates;
    std::map<std::string, std::vector<DirectoryQueueEntry>> movedDirCandidates;

    for (auto &f : filesToAdd) {
      movedFileCandidates[f.inode].push_back(
          Utility::constructFileQueueEntry(f, SyncStatus::NEW));
    }

    for (auto &f : filesToDelete) {
      movedFileCandidates[f.inode].push_back(f);
    }

    for (auto &[path, d] : dirsToAddMap) {
      movedDirCandidates[d.inode].push_back(
          Utility::constructDirectoryQueueEntry(d, SyncStatus::NEW));
    }

    for (auto &dq : dirsToDelete) {
      movedDirCandidates[dq.inode].push_back(dq);
    }

    filesToAdd.clear();
    filesToDelete.clear();
    dirsToAddMap.clear();
    dirsToDelete.clear();
    dirsToAdd.clear();

    for (const auto &[uuid, group] : movedDirCandidates) {

      if (group.size() == 2) {
        bool isMoved = false;
        DirectoryQueueEntry newDir, oldDir;

        if (group[0].sync_status == syncStatusToString(SyncStatus::NEW) &&
            group[1].sync_status == syncStatusToString(SyncStatus::DELETE)) {
          isMoved = true;
          newDir = group[0];
          oldDir = group[1];
        }

        if (group[0].sync_status == syncStatusToString(SyncStatus::DELETE) &&
            group[1].sync_status == syncStatusToString(SyncStatus::NEW)) {
          isMoved = true;
          newDir = group[1];
          oldDir = group[0];
        }

        if (isMoved) {
          DirectoryQueueEntry d;
          d.folder = newDir.folder;
          d.path = newDir.path;
          d.device = newDir.device;
          d.created_at = newDir.created_at;
          d.absPath = newDir.absPath;
          d.old_path = oldDir.path;
          d.sync_status = syncStatusToString(SyncStatus::MOVED);
          d.inode = newDir.inode;
          d.lastSynced = "";
          d.uuid = oldDir.uuid;
          movedDirsMap[d.path] = d;
          continue;
        }
      }

      for (auto &d : group) {

        if (d.sync_status == syncStatusToString(SyncStatus::NEW)) {
          dirsToAdd.push_back(Utility::constructDirectoryMetadata(d));
        }

        if (d.sync_status == syncStatusToString(SyncStatus::DELETE)) {
          dirsToDelete.push_back(d);
        }
      }
    }

    for (const auto &[inode, group] : movedFileCandidates) {
      if (group.size() == 2) {
        bool isMoved = false;
        FileQueueEntry newFile, oldFile;

        if (group[0].sync_status == syncStatusToString(SyncStatus::NEW) &&
            group[1].sync_status == syncStatusToString(SyncStatus::DELETE)) {
          isMoved = true;
          newFile = group[0];
          oldFile = group[1];
        }

        if (group[0].sync_status == syncStatusToString(SyncStatus::DELETE) &&
            group[1].sync_status == syncStatusToString(SyncStatus::NEW)) {
          isMoved = true;
          newFile = group[1];
          oldFile = group[0];
        }

        if (isMoved) {
          FileQueueEntry f;
          f.filename = newFile.filename;
          f.path = newFile.path;
          f.absPath = newFile.absPath;
          f.inode = newFile.inode;
          f.versions = newFile.versions;
          f.dirID = newFile.dirID;
          f.last_modified = newFile.last_modified;
          f.hashvalue = newFile.hashvalue;
          f.lastSyncedHashValue = newFile.lastSyncedHashValue;
          f.size = newFile.size;
          f.lastSynced = "";
          f.origin = oldFile.origin;
          f.uuid = oldFile.uuid;
          f.old_filename = oldFile.filename;
          f.old_path = oldFile.path;
          f.sync_status = syncStatusToString(SyncStatus::MOVED);
          auto itMv = movedDirsMap.find(f.path);
          if (itMv != movedDirsMap.end()) {
            f.dirID = itMv->second.uuid;
          }
          movedFiles.push_back(f);
          continue;
        }
      }
      for (auto &f : group) {
        if (f.sync_status == syncStatusToString(SyncStatus::NEW)) {
          filesToAdd.push_back(Utility::constructFileMetadata(f));
        }
        if (f.sync_status == syncStatusToString(SyncStatus::DELETE)) {
          filesToDelete.push_back(f);
        }
      }
    }

    movedDirs.reserve(movedDirsMap.size());

    std::transform(movedDirsMap.begin(), movedDirsMap.end(),
                   std::back_inserter(movedDirs),
                   [&](const auto &pair) { return pair.second; });
  */
  std::transform(dirsToAddMap.begin(), dirsToAddMap.end(),
                 std::back_inserter(dirsToAdd),
                 [&](const auto &pair) { return pair.second; });

  OfflineReconResult updated = Utility::detectRenames<OfflineReconResult>(
      filesToAdd, filesToDelete, dirsToAdd, dirsToDelete);

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
  /*
     auto filesInQueue = m_dbManager.getAllQueueFiles();
     std::map<std::string, std::vector<FileQueueEntry>> inodeGroups;
     std::map<std::string, std::vector<FileQueueEntry>> renamedCandidates;
     for (const FileQueueEntry &f : *filesInQueue) {
       inodeGroups[f.inode].push_back(f);
     }
     for (const auto &[inode, files] : inodeGroups) {
       if (files.size() == 2) {
         renamedCandidates[inode] = files;
       }
     }
     for (const auto &[inode, files] : renamedCandidates) {
       FileQueueEntry deleted{};
       FileQueueEntry added{};
       for (const auto &file : files) {
         if (file.sync_status == syncStatusToString(SyncStatus::NEW))
           added = file;
         if (file.sync_status == syncStatusToString(SyncStatus::DELETE))
           deleted = file;
       }
       if (!deleted.sync_status.empty() && !added.sync_status.empty() &&
           deleted.hashvalue == added.hashvalue) {
         FileQueueEntry q{added};
         added.sync_status = syncStatusToString(SyncStatus::RENAME);
         added.old_filename = deleted.filename;
         m_dbManager.deleteFileQueue(deleted.path, deleted.filename);
         m_dbManager.updateFileQueue(added);
       }
     }
     */
  /*  std::optional<std::vector<DirectoryQueueEntry>> qDirs{};
  qDirs = m_dbManager.getAllQueueDirectories();
  std::vector<RenameInfo> renames = detectDirRenames(*qDirs);
  std::vector<RenameInfo> collapsed = collapseDirRenames(renames);
  reconcileDirRenamedCandidates(collapsed);
  */
}

} // namespace sync_app
