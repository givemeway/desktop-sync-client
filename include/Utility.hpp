#pragma once
#include "DatabaseManager.hpp"
#include "types.hpp"
#include <filesystem>
namespace sync_app {

class Utility {
public:
  FileQueueEntry static constructFileQueueEntry(
      const FileMetadata &f, SyncStatus status = SyncStatus::NEW,
      std::string &&old_path = "", std::string &&old_filename = "");

  DirectoryQueueEntry static constructDirectoryQueueEntry(
      const DirectoryMetadata &d, SyncStatus status = SyncStatus::NEW,
      std::string &&old_path = "");

  FileMetadata static constructFileMetadata(const FileQueueEntry &fq);

  DirectoryMetadata static constructDirectoryMetadata(
      const DirectoryQueueEntry &dq);

  std::int64_t static getUnixTimeStamp(
      const std::filesystem::file_time_type &ftime);

  std::string static getInode(const std::string &absPath);

  DirectoryMetadata static createDirectoryMetadata(const std::string &path,
                                                   const std::string &syncPath);

  pathParts static getFolderDevice(const std::filesystem::path &path);

  ReconciliationResult static detectDirRenames(
      const ReconciliationResult &result);

  template <typename Target, typename Source, typename... Args>

  Target static convert(const Source &src, Args... args) {
    Target dst;

    if constexpr (std::is_same_v<Target, CloudFileMetadata> ||
                  std::is_same_v<Target, FileMetadata> ||
                  std::is_same_v<Target, FileQueueEntry>) {

      dst.filename = src.filename;
      dst.path = src.path;
      dst.size = src.size;
      dst.last_modified = src.last_modified;
      dst.hashvalue = src.hashvalue;
      dst.lastSyncedHashValue = src.lastSyncedHashValue;
      dst.versions = src.versions;
      dst.origin = src.origin;
      dst.dirID = src.dirID;
      dst.uuid = src.uuid;

      // Convert from FileQueueEntry => CloudFileMetadata
      if constexpr (std::is_same_v<Target, CloudFileMetadata>) {

        if constexpr (requires { src.dirIDs; }) {

          if (src.dirIDs.has_value()) {
            dst.dirIDs = src.dirIDs;
          }
        }

        if constexpr (requires { src.conflictId; }) {

          if (src.conflictId.has_value()) {
            dst.conflictId = src.conflictId;
          }
        }

        if constexpr (requires { src.last_updated; }) {

          if (src.last_updated.has_value()) {
            dst.last_updated = src.last_updated;
          }
        }

        return dst;
      }
      if constexpr (requires {
                      src.absPath;
                      dst.absPath;
                    }) {

        dst.absPath = src.absPath;
      }
      if constexpr (requires {
                      src.inode;
                      dst.inode;
                    }) {

        dst.inode = src.inode;
      }

      // Convert from FileQueueEntry => FileMetadata
      if constexpr (std::is_same_v<Target, FileMetadata>)
        return dst;

      if constexpr (std::is_same_v<Target, FileQueueEntry>) {
        // Convert from FileMetadata => FileQueueEntry
        if constexpr (requires { src.dirIDs; }) {

          if (src.dirIDs.has_value()) {
            dst.dirIDs = src.dirIDs;
          }
        }
        (
            [&]<typename T>(T &a) {
              using ArgType = std::decay_t<decltype(a)>;

              if constexpr (std::is_same_v<ArgType, SyncStatus>) {
                dst.sync_status = syncStatusToString(a);

              } else if constexpr (std::is_same_v<ArgType, std::string>) {

                if constexpr (requires {
                                dst.old_path;
                                dst.old_filename;
                              }) {
                  dst.old_path = a;
                  dst.old_filename = a;
                }
              }
            }(args),
            ...);
        return dst;
      }
    } else if constexpr (std::is_same_v<Target, DirectoryMetadata> ||
                         std::is_same_v<Target, DirectoryQueueEntry> ||
                         std::is_same_v<Target, LocalFolderCreateMetadata> ||
                         std::is_same_v<Target, LocalFolderDeleteMetadata>

    ) {

      dst.absPath = src.absPath;
      dst.path = src.path;
      dst.folder = src.folder;
      dst.uuid = src.uuid;
      dst.device = src.device;
      dst.created_at = src.created_at;

      if constexpr (std::is_same_v<Target, LocalFolderDeleteMetadata>)
        return dst;

      if constexpr (std::is_same_v<Target, LocalFolderCreateMetadata>) {

        if constexpr (requires { src.dirIDs; }) {

          if (src.dirIDs.has_value()) {

            dst.dirIDs = src.dirIDs;
          }
        }
        return dst;
      }
      if constexpr (requires { src.inode; }) {

        if constexpr (requires { dst.inode; }) {

          dst.inode = src.inode;
        }

      } else if constexpr (requires { dst.inode; }) {

        dst.inode = "";
      }

      if constexpr (requires { src.lastSynced; }) {

        if constexpr (requires { dst.lastSynced; }) {

          dst.lastSynced = src.lastSynced;
        }

      } else if constexpr (requires { dst.lastSynced; }) {

        dst.lastSynced = "";
      }

      if constexpr (std::is_same_v<Target, DirectoryMetadata>)
        return dst;

      if constexpr (std::is_same_v<Target, DirectoryQueueEntry>) {

        if constexpr (requires { src.dirIDs; }) {

          if (src.dirIDs.has_value()) {

            dst.dirIDs = src.dirIDs;
          }
        }
        (
            [&]<typename T>(T &a) {
              using ArgType = std::decay_t<decltype(a)>;

              if constexpr (std::is_same_v<ArgType, SyncStatus>) {

                dst.sync_status = syncStatusToString(a);
              } else if constexpr (std::is_same_v<ArgType, std::string>) {

                if constexpr (requires { dst.old_path; }) {
                  dst.old_path = a;
                }
              }
            }(args),
            ...);
        return dst;
      }
    }
  }

  template <typename R, typename T1, typename T2, typename T3, typename T4>
  R static detectRenames(T1 &fa, T2 &fd, T3 &da, T4 &dd) {

    std::map<std::string, std::vector<FileQueueEntry>> movedFileCandidates;
    std::map<std::string, std::vector<DirectoryQueueEntry>> movedDirCandidates;
    std::map<std::string, DirectoryQueueEntry> movedDirsMap;

    T1 filesToAdd{fa};
    T2 filesToDelete{fd};
    T3 dirsToAdd{da};
    T4 dirsToDelete{dd};

    std::vector<FileQueueEntry> movedFiles{};
    std::vector<DirectoryQueueEntry> movedDirs{};

    for (auto &f : filesToAdd) {
      FileQueueEntry fq;
      fq = convert<FileQueueEntry>(f, SyncStatus::NEW, f.path, f.filename);

      if constexpr (std::is_same_v<typename T1::value_type, FileMetadata>) {
        movedFileCandidates[f.inode].push_back(fq);
      }

      if constexpr (std::is_same_v<typename T1::value_type,
                                   CloudFileMetadata>) {
        movedFileCandidates[f.origin].push_back(fq);
      }
    }

    for (auto &f : filesToDelete) {
      FileQueueEntry fq;
      fq = convert<FileQueueEntry>(f, SyncStatus::DELETE, f.path, f.filename);

      if constexpr (std::is_same_v<typename T2::value_type, FileQueueEntry>) {
        movedFileCandidates[f.inode].push_back(fq);
      }

      if constexpr (std::is_same_v<typename T2::value_type, FileMetadata>) {
        movedFileCandidates[f.origin].push_back(fq);
      }
    }

    for (auto &d : dirsToAdd) {
      DirectoryQueueEntry dq;
      dq = convert<DirectoryQueueEntry>(d, SyncStatus::NEW, d.path);

      if constexpr (std::is_same_v<typename T3::value_type,
                                   DirectoryMetadata>) {
        movedDirCandidates[d.inode].push_back(dq);
      }

      if constexpr (std::is_same_v<typename T3::value_type,
                                   LocalFolderCreateMetadata>) {
        movedDirCandidates[d.uuid].push_back(dq);
      }
    }

    for (auto &d : dirsToDelete) {
      DirectoryQueueEntry dq;
      dq = convert<DirectoryQueueEntry>(d, SyncStatus::DELETE, d.path);

      if constexpr (std::is_same_v<typename T4::value_type,
                                   DirectoryQueueEntry>) {
        movedDirCandidates[d.inode].push_back(dq);
      }

      if constexpr (std::is_same_v<typename T4::value_type,
                                   LocalFolderDeleteMetadata>) {
        movedDirCandidates[d.uuid].push_back(dq);
      }
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
          typename T3::value_type fc;

          fc = convert<typename T3::value_type>(d);

          dirsToAdd.push_back(fc);
        }

        if (d.sync_status == syncStatusToString(SyncStatus::DELETE)) {
          typename T4::value_type fd;

          if constexpr (std::is_same_v<typename T4::value_type,
                                       DirectoryQueueEntry>) {
            fd = convert<typename T4::value_type>(d, SyncStatus::DELETE,
                                                  d.old_path);
          } else {

            fd = convert<typename T4::value_type>(d);
          }

          dirsToDelete.push_back(fd);
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
          typename T1::value_type cf;
          cf = convert<typename T1::value_type>(f);
          filesToAdd.push_back(cf);
        }

        if (f.sync_status == syncStatusToString(SyncStatus::DELETE)) {
          typename T2::value_type ff;
          if constexpr (std::is_same_v<typename T2::value_type,
                                       FileQueueEntry>) {
            ff = convert<typename T2::value_type>(f, SyncStatus::DELETE,
                                                  f.old_path, f.old_filename);
          } else {
            ff = convert<typename T2::value_type>(f);
          }
          filesToDelete.push_back(ff);
        }
      }
    }

    movedDirs.reserve(movedDirsMap.size());

    std::transform(movedDirsMap.begin(), movedDirsMap.end(),
                   std::back_inserter(movedDirs),
                   [&](const auto &pair) { return pair.second; });

    R result;
    result.filesToDeleteLocal = filesToDelete;
    result.filesToDownload = filesToAdd;
    result.foldersToCreateLocal = dirsToAdd;
    result.foldersToDeleteLocal = dirsToDelete;
    result.filesToMove = movedFiles;
    result.dirsToMove = movedDirs;
    return result;
  };

  template <typename T, typename T2, typename T3>
  T static removeRedundantMovedFiles(T2 &movedDirs, T3 &movedFiles) {

    std::map<std::string, std::vector<typename T3::value_type>>
        movedFilesPathMap;
    std::map<std::string, typename T2::value_type> movedDirsMap;

    for (auto &d : movedDirs) {
      movedDirsMap[d.path] = d;
    }

    T filesTomove;

    for (auto &f : movedFiles) {
      movedFilesPathMap[f.path].push_back(f);
    }

    for (auto &[path, d] : movedDirsMap) {
      auto itNewPath = movedFilesPathMap.find(path);

      if (itNewPath != movedFilesPathMap.end()) {
        for (auto &f : itNewPath->second) {

          if (d.old_path != *f.old_path) {
            filesTomove.push_back(f);
          }
        }
        movedFilesPathMap.erase(path);
      }
    }

    for (auto &[path, files] : movedFilesPathMap) {
      for (auto &f : files) {
        filesTomove.push_back(f);
      }
    }
    return filesTomove;
  }
};

} // namespace sync_app
