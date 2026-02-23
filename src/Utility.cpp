#include "Utility.hpp"
#include "UuidUtils.hpp"
#include "types.hpp"

#ifdef _WIN32
#include <windows.h>
#undef DELETE
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace sync_app {

FileQueueEntry Utility::constructFileQueueEntry(const FileMetadata &f,
                                                SyncStatus status,
                                                std::string &&old_path,
                                                std::string &&old_filename) {
  FileQueueEntry fq;
  fq.filename = f.filename;
  fq.path = f.path;
  fq.inode = f.inode;
  fq.size = f.size;
  fq.last_modified = f.last_modified;
  fq.hashvalue = f.hashvalue;
  fq.lastSyncedHashValue = f.lastSyncedHashValue;
  fq.versions = f.versions;
  fq.origin = f.origin;
  fq.dirID = f.dirID;
  fq.uuid = f.uuid;
  fq.absPath = f.absPath;
  fq.old_path = std::move(old_path);
  fq.old_filename = std::move(old_filename);
  fq.sync_status = syncStatusToString(status);
  return fq;
}

DirectoryQueueEntry Utility::constructDirectoryQueueEntry(
    const DirectoryMetadata &d, SyncStatus status, std::string &&old_path) {

  DirectoryQueueEntry dq;
  dq.uuid = d.uuid;
  dq.device = d.device;
  dq.folder = d.folder;
  dq.path = d.path;
  dq.absPath = d.absPath;
  dq.created_at = d.created_at;
  dq.inode = d.inode;
  dq.lastSynced = d.lastSynced;
  dq.old_path = std::move(old_path);
  dq.sync_status = syncStatusToString(status);
  return dq;
}
FileMetadata Utility::constructFileMetadata(const FileQueueEntry &f) {

  FileMetadata fq;
  fq.filename = f.filename;
  fq.path = f.path;
  fq.inode = f.inode;
  fq.size = f.size;
  fq.last_modified = f.last_modified;
  fq.hashvalue = f.hashvalue;
  fq.lastSyncedHashValue = f.lastSyncedHashValue;
  fq.versions = f.versions;
  fq.origin = f.origin;
  fq.dirID = f.dirID;
  fq.uuid = f.uuid;
  fq.absPath = f.absPath;
  return fq;
}
DirectoryMetadata
Utility::constructDirectoryMetadata(const DirectoryQueueEntry &d) {

  DirectoryMetadata dq;
  dq.uuid = d.uuid;
  dq.device = d.device;
  dq.folder = d.folder;
  dq.path = d.path;
  dq.absPath = d.absPath;
  dq.created_at = d.created_at;
  dq.inode = d.inode;
  dq.lastSynced = d.lastSynced;
  return dq;
}
std::int64_t Utility::getUnixTimeStamp(const fs::file_time_type &ftime) {
  auto now_file = fs::file_time_type::clock::now();
  auto now_sys = std::chrono::system_clock::now();
  auto file_duration = ftime - now_file;
  auto sys_time =
      now_sys + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    file_duration);
  return std::chrono::duration_cast<std::chrono::seconds>(
             sys_time.time_since_epoch())
      .count();
}

std::string Utility::getInode(const std::string &absPath) {

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
pathParts Utility::getFolderDevice(const fs::path &path) {
  pathParts parts{"/", "/"};
  if (path.empty())
    return parts;
  parts.device = path.root_name().generic_string();
  parts.folder = path.filename().generic_string();
  if (parts.folder.empty())
    parts.folder = "/";
  auto root_dir = path.root_directory();
  auto rel = path.relative_path();
  if (!rel.empty()) {
    auto first = rel.begin();
    if (first != rel.end()) {
      parts.device = first->string();
    }
  }
  if (parts.device.empty()) {
    parts.device = "/";
  }
  return parts;
}

DirectoryMetadata
Utility::createDirectoryMetadata(const std::string &path,
                                 const std::string &syncPath) {
  DirectoryMetadata d;
  pathParts p = getFolderDevice(fs::path(path));
  d.device = p.device;
  d.path = path;
  d.folder = p.folder;
  d.absPath = d.path == "/" ? syncPath : syncPath + d.path;
  d.uuid = UuidUtils::generate();
  try {
    d.inode = getInode(d.absPath);
    d.created_at =
        std::to_string(getUnixTimeStamp(fs::last_write_time(d.absPath)));

  } catch (const std::exception &) {
    d.inode = "";
    d.created_at = "";
  }
  return d;
}
/*
ReconciliationResult
Utility::detectDirRenames(const ReconciliationResult &result) {
  ReconciliationResult updatedResults;
  std::map<std::string, std::vector<FileQueueEntry>> movedFileCandidates;
  std::map<std::string, DirectoryQueueEntry> movedDirsMap;
  std::map<std::string, std::vector<DirectoryQueueEntry>> movedDirCandidates;

  std::vector<CloudFileMetadata> filesToAdd{result.filesToDownload};
  std::vector<FileMetadata> filesToDelete{result.filesToDeleteLocal};
  std::vector<LocalFolderCreateMetadata> dirsToAdd{result.foldersToCreateLocal};
  std::vector<LocalFolderDeleteMetadata> dirsToDelete{
      result.foldersToDeleteLocal};
  std::vector<FileQueueEntry> movedFiles{};
  std::vector<DirectoryQueueEntry> movedDirs{};

  for (auto &f : filesToAdd) {
    FileQueueEntry fq;
    fq = Utility::convert<FileQueueEntry>(f, SyncStatus::NEW, f.path,
                                          f.filename);
    movedFileCandidates[f.origin].push_back(fq);
  }

  for (auto &f : filesToDelete) {
    FileQueueEntry fq;
    fq = Utility::convert<FileQueueEntry>(f, SyncStatus::DELETE, f.path,
                                          f.filename);
    movedFileCandidates[f.inode].push_back(fq);
  }

  for (auto &d : dirsToAdd) {
    DirectoryQueueEntry dq;
    dq = Utility::convert<DirectoryQueueEntry>(d, SyncStatus::NEW, d.path);
    movedDirCandidates[d.uuid].push_back(dq);
  }

  for (auto &d : dirsToDelete) {
    DirectoryQueueEntry dq;
    dq = Utility::convert<DirectoryQueueEntry>(d, SyncStatus::NEW, d.path);
    movedDirCandidates[d.uuid].push_back(dq);
  }

  filesToAdd.clear();
  filesToDelete.clear();
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
        LocalFolderCreateMetadata fc;
        fc = Utility::convert<LocalFolderCreateMetadata>(d);
        updatedResults.foldersToCreateLocal.push_back(fc);
      }

      if (d.sync_status == syncStatusToString(SyncStatus::DELETE)) {
        LocalFolderDeleteMetadata fd;
        fd = Utility::convert<LocalFolderDeleteMetadata>(d);
        updatedResults.foldersToDeleteLocal.push_back(fd);
      }
    }
  }

  for (const auto &[origin, group] : movedFileCandidates) {

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
        CloudFileMetadata cf;
        cf = Utility::convert<CloudFileMetadata>(f);
        updatedResults.filesToDownload.push_back(cf);
      }

      if (f.sync_status == syncStatusToString(SyncStatus::DELETE)) {
        FileMetadata ff;
        ff = Utility::convert<FileMetadata>(f);
        updatedResults.filesToDeleteLocal.push_back(ff);
      }
    }
  }

  std::transform(movedDirsMap.begin(), movedDirsMap.end(),
                 std::back_inserter(updatedResults.dirsToMove),
                 [&](const auto &pair) { return pair.second; });

  movedFiles = Utility::removeRedundantMovedFiles<std::vector<FileQueueEntry>>(
      updatedResults.dirsToMove, updatedResults.filesToMove);

  updatedResults.filesToMove = movedFiles;

  return updatedResults;
}
*/
} // namespace sync_app
