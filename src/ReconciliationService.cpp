#include "ReconciliationService.hpp"
#include "UuidUtils.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace sync {

ReconciliationService::ReconciliationService(DatabaseManager &dbManager,
                                             const std::string &syncPath)
    : m_dbManager(dbManager), m_syncPath(syncPath), m_scanner(syncPath) {}

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

  // 1. Indexing Cloud State
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

  // 3. Load Local Queue (Mocking prisma retrieval via dbManager)
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

    FileMetadata *localFileByOrigin =
        (itOrigin != dbByOrigin.end()) ? &itOrigin->second : nullptr;
    FileMetadata *localFileByPath =
        (itPath != dbPathMap.end()) ? &itPath->second : nullptr;

    auto localInQueue = localInQueueByAnyPath(
        cloudFile, localQueueByOrigin, localQueueByUuid, localQueueByPath);

    bool isLocalModified = false;
    auto itLocalQ = localQueueByPath.find(pathKey);
    if (itLocalQ != localQueueByPath.end()) {
      isLocalModified = (itLocalQ->second.sync_status == "modified");
    }

    bool isLocalRenamed = false;
    auto itLocalOR = localQueueByOrigin.find(cloudFile.origin);
    if (itLocalOR != localQueueByOrigin.end()) {
      isLocalRenamed = (itLocalOR->second.sync_status == "rename");
    }

    bool isCloudModified =
        localFileByPath
            ? (cloudFile.hashvalue != localFileByPath->lastSyncedHashValue)
            : false;
    bool isCloudRenamed = false;

    if (isLocalRenamed) {
      auto qEntry = localQueueByOrigin[cloudFile.origin];
      isCloudRenamed =
          qEntry.old_filename && (*qEntry.old_filename != cloudFile.filename);
    } else {
      isCloudRenamed = localFileByOrigin
                           ? (localFileByOrigin->filename != cloudFile.filename)
                           : false;
    }

    // New file in cloud
    if (!localFileByPath && !localFileByOrigin) {
      if (!localInQueue) {
        result.filesToDownload.push_back(cloudFile);
        continue;
      }
    }

    // Existing file - handle updates / renames / conflicts
    if (localFileByOrigin) {
      if (isCloudModified && !isCloudRenamed && !isLocalModified &&
          !isLocalRenamed) {
        result.filesToUpdate.push_back(cloudFile);
      }
      if (!isCloudModified && isCloudRenamed && !isLocalModified &&
          !isLocalRenamed) {
        result.filesToRename.push_back({*localFileByOrigin, cloudFile});
      }
      // Conflict detection
      if (isCloudModified && !isCloudRenamed && isLocalModified &&
          !isLocalRenamed) {
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
    std::string key = getUniqueKey(dbFile.path, dbFile.filename);
    if (cloudPathMap.find(key) == cloudPathMap.end()) {
      auto itLQ = localQueueByOrigin.find(dbFile.origin);
      if (itLQ != localQueueByOrigin.end()) {
        const auto &status = itLQ->second.sync_status;
        if (status == "modified" || status == "rename" || status == "new") {
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
  for (const auto &d : cloudDirs) {
    if (d.path != "/")
      cloudDirMap[d.path] = d;
  }

  std::map<std::string, DirectoryMetadata> dbDirMap;
  for (const auto &d : dbDirs) {
    if (d.path != "/")
      dbDirMap[d.path] = d;
  }

  for (const auto &[path, cloudDir] : cloudDirMap) {
    if (dbDirMap.find(path) == dbDirMap.end()) {
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
            std::filesystem::path(m_syncPath).append(cloudDir.path).string();
        createMeta.path = cloudDir.path;
        createMeta.folder = cloudDir.folder;
        createMeta.uuid = cloudDir.uuid;
        createMeta.device = cloudDir.device;
        createMeta.created_at = cloudDir.created_at;
        result.foldersToCreateLocal.push_back(createMeta);
      }
    }
  }

  for (const auto &[path, dbDir] : dbDirMap) {
    if (cloudDirMap.find(path) == cloudDirMap.end()) {
      auto dirsInQ = m_dbManager.getDirectoryQueue();
      bool alreadyInQ = std::any_of(
          dirsInQ->begin(), dirsInQ->end(),
          [&](const DirectoryQueueEntry &e) { return e.path == path; });

      if (!alreadyInQ) {
        result.foldersToDeleteLocal.push_back(
            {dbDir.absPath, dbDir.path, dbDir.folder});
      }
    }
  }

  // 8. Handle Directory Renames (using inodes from local queue)
  std::vector<RenameInfo> renames = detectDirRenames(*localDirQueue);
  std::vector<RenameInfo> collapsed = collapseDirRenames(renames);
  reconcileDirRenamedCandidates(collapsed);

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
      if (e.sync_status == "delete")
        deletes.push_back(e);
      else if (e.sync_status == "new")
        news.push_back(e);
    }

    if (deletes.empty() || news.empty())
      continue;

    // Pick top-most (shortest path) for comparison
    auto oldEntry = *std::min_element(
        deletes.begin(), deletes.end(),
        [](const DirectoryQueueEntry &a, const DirectoryQueueEntry &b) {
          return a.path.length() < b.path.length();
        });
    auto newEntry = *std::min_element(
        news.begin(), news.end(),
        [](const DirectoryQueueEntry &a, const DirectoryQueueEntry &b) {
          return a.path.length() < b.path.length();
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
    // Since we don't have prisma-style transactions yet, we'll perform these
    // cleanup operations sequentially via dbManager This would involve deleting
    // redundant delete/new queue entries and upserting a single 'rename' entry
    // For now, these are placeholder routines as we'd need more specific delete
    // methods in DatabaseManager
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
    dq.sync_status = "rename";
    dq.absPath = std::filesystem::path(m_syncPath).append(dir.newPath).string();

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

  auto itU = localQueueByUuid.find(cloudFile.uuid);
  if (itU != localQueueByUuid.end() && !itU->second.empty())
    return itU->second[0];

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

  // 2. Identify File Changes

  // Check for NEW or MODIFIED files
  for (const auto &[key, sFile] : scanFilesMap) {
    if (dbFilesPathMap.find(key) == dbFilesPathMap.end()) {
      // New File
      std::cout << "[Reconcile] Offline ADD detected: " << key << std::endl;
      FileMetadata f;
      FileQueueEntry fq;

      f.uuid = UuidUtils::generate();
      f.path = sFile.path;
      f.filename = sFile.filename;
      f.last_modified = std::to_string(sFile.mtime);
      f.hashvalue = sFile.hash;
      f.size = sFile.size;
      f.inode = sFile.inode;
      f.absPath = sFile.absPath;
      f.versions = 1;
      f.origin = f.uuid;
      f.lastSyncedHashValue = sFile.hash;

      fq = FileMetadata(f);
      fq.sync_status = "new";
      fq.old_filename = sFile.filename;
      fq.old_path = sFile.path;

      pathParts part = m_dbManager.getFolderDevice(f.path);
      std::cout << "[Reconcile] " << "device : " << part.device
                << " folder: " << part.folder << " path: " << f.path
                << std::endl;
      auto dir =
          m_dbManager.getDirectoryByPath(part.device, part.folder, f.path);
      std::cout << "[Reconcile] dir value exists : " << dir.has_value()
                << std::endl;
      if (dir.has_value()) {
        fq.dirID = dir->uuid;
        f.dirID = dir->uuid;
      } else {
        // create the directory path
        DirectoryQueueEntry dq;
        DirectoryMetadata d;
        d.path = f.path;
        d.device = part.device;
        d.folder = part.folder;
        d.uuid = UuidUtils::generate();
        if (d.path != "/")
          d.absPath = m_syncPath + "/" + d.path;
        else
          d.absPath = m_syncPath;
        try {
          d.inode = m_scanner.getInode(d.absPath);
          std::filesystem::path dir{d.absPath};
          auto ftime = std::filesystem::directory_entry(dir).last_write_time();
          d.created_at = std::to_string(m_scanner.getUnixTimeStamp(ftime));
        } catch (const std::filesystem::filesystem_error &e) {
          d.created_at = "";
          d.inode = "";
          std::cerr << "Error: " << e.what() << std::endl;
        }
        // Create DirectoryQueueEntry from DirectoryMetadata
        dq = DirectoryQueueEntry(d);
        dq.sync_status = "FILE_LINKED";
        dq.old_path = d.path;
        m_dbManager.insertDirectoryQueue(dq);
        m_dbManager.insertDirectory(d);
        fq.dirID = d.uuid;
        f.dirID = d.uuid;
      }
      m_dbManager.upsertFileQueue(fq);
      m_dbManager.upsertFile(f);
    } else {

      const auto &dbFile = dbFilesPathMap[key];
      if (dbFile.hashvalue != sFile.hash) {
        // Modified File
        std::cout << "[Reconcile] Offline MODIFY detected: " << key
                  << std::endl;
        FileQueueEntry fq;
        FileMetadata f;
        f.path = sFile.path;
        f.dirID = dbFile.dirID;
        f.filename = sFile.filename;
        f.absPath = sFile.absPath;
        f.inode = sFile.inode;
        f.hashvalue = sFile.hash;
        f.lastSyncedHashValue = dbFile.lastSyncedHashValue;
        f.size = sFile.size;
        f.last_modified = std::to_string(sFile.mtime);
        f.uuid = UuidUtils::generate();
        f.origin = dbFile.origin;
        f.versions = dbFile.versions + 1;
        fq = FileMetadata(f);
        fq.sync_status = "modified";
        m_dbManager.upsertFile(f);
        m_dbManager.upsertFileQueue(fq);
      }
    }
  }
  // Check for DELETED files
  for (const auto &[key, dbFile] : dbFilesPathMap) {
    if (scanFilesMap.find(key) == scanFilesMap.end()) {
      std::cout << "[Reconcile] Offline DELETE detected: " << key << std::endl;

      // Create FileQueueEntry from FileMetadata
      FileQueueEntry q(dbFile);
      q.sync_status = "delete";
      m_dbManager.deleteFile(dbFile.origin);
      m_dbManager.upsertFileQueue(q);
    }
  }

  // 3. Identify Directory Changes

  // Check for NEW directories
  for (const auto &[path, sDir] : scanDirsMap) {
    if (dbDirsPathMap.find(path) == dbDirsPathMap.end()) {
      std::cout << "[Reconcile] Offline DIR ADD detected: " << path
                << std::endl;
      DirectoryMetadata q;
      DirectoryQueueEntry qd;
      q.path = sDir.path;
      q.folder = sDir.name;
      q.absPath = sDir.absPath;
      q.inode = sDir.inode;
      q.created_at = std::to_string(sDir.mtime);
      pathParts part = m_dbManager.getFolderDevice(sDir.path);
      auto existingDir =
          m_dbManager.getDirectoryByPath(part.device, sDir.name, sDir.path);
      if (existingDir.has_value()) {
        q.uuid = existingDir->uuid;
      } else {
        q.uuid = UuidUtils::generate();
      }
      q.device = part.device;
      qd = DirectoryMetadata(q);
      qd.sync_status = "new";
      m_dbManager.upsertDirectory(q);
      m_dbManager.upsertDirectoryQueue(qd);
    }
  }

  // Check for DELETED directories
  for (const auto &[path, dbDir] : dbDirsPathMap) {
    if (scanDirsMap.find(path) == scanDirsMap.end()) {
      std::cout << "[Reconcile] Offline DIR DELETE detected: " << path
                << std::endl;
      DirectoryQueueEntry q(dbDir);
      q.sync_status = "delete";
      m_dbManager.deleteDirectory(dbDir.uuid);
      m_dbManager.upsertDirectoryQueue(q);
    }
  }
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
      if (file.sync_status == "new")
        added = file;
      if (file.sync_status == "delete")
        deleted = file;
    }
    if (!deleted.sync_status.empty() && !added.sync_status.empty() &&
        deleted.hashvalue == added.hashvalue) {
      FileQueueEntry q(added);
      added.sync_status = "rename";
      added.old_filename = deleted.filename;
      m_dbManager.deleteFileQueue(deleted.origin);
      m_dbManager.updateFileQueue(added);
    }
  }
}

} // namespace sync
