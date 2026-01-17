#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sync_app {

enum class SyncStatus { NEW, RENAME, MODIFIED, DELETE, FILE_LINKED, UNKNOWN };

struct ScannedFile {
  std::string path; // Relative path from sync root (e.g. "/foo/bar.txt")
  std::string filename;
  std::string absPath;
  std::string inode;
  std::string hash;
  int64_t size;
  int64_t mtime; // UTC timestamp
};

struct ScannedDirectory {
  std::string path; // Relative path (e.g. "/foo")
  std::string name;
  std::string absPath;
  std::string inode;
  int64_t mtime;
};

struct ScanResult {
  std::vector<ScannedFile> files;
  std::vector<ScannedDirectory> directories;
};

struct FileMetadata {
  std::string uuid;
  std::string path;
  std::string filename;
  std::string last_modified; // Store as string for SQLite compatibility
  std::string hashvalue;
  int64_t size;
  std::string dirID;
  std::string inode;
  std::string absPath;
  int32_t versions;
  std::string origin;
  std::string lastSyncedHashValue;
  std::optional<std::string> conflictId;
  std::string lastSynced;
};

struct DirectoryMetadata {
  std::string uuid;
  std::string device;
  std::string folder;
  std::string path;
  std::string created_at;
  std::string absPath;
  std::string inode;
  std::string lastSynced;
};

struct FileQueueEntry : public FileMetadata {
  std::string sync_status;
  std::optional<std::string> old_path;
  std::optional<std::string> old_filename;
  // Shadowed fields to give unique memory pointer addresses for sqlite_orm
  std::string uuid;
  std::string path;
  std::string dirID;
  std::string filename;
  std::string origin;

  // Default constructor
  FileQueueEntry() = default;

  // Constructor from FileMetadata (ensure shadowed fields are copied)
  FileQueueEntry(const FileMetadata &f)
      : FileMetadata(f), uuid(f.uuid), path(f.path), dirID(f.dirID),
        filename(f.filename), origin(f.origin) {}
};

struct DirectoryQueueEntry : public DirectoryMetadata {
  std::string sync_status;
  std::optional<std::string> old_path;

  // Shadowed fields to give unique memory pointer addresses for sqlite_orm
  std::string uuid;
  std::string path;
  std::string folder;
  std::string device;
  // Default constructor
  DirectoryQueueEntry() = default;

  // Constructor from DirectoryMetadata (ensure shadowed fields are copied)
  DirectoryQueueEntry(const DirectoryMetadata &d)
      : DirectoryMetadata(d), uuid(d.uuid), path(d.path), device(d.device),
        folder(d.folder) {}
};

struct CloudFileMetadata {
  std::string uuid;
  std::string path;
  std::string filename;
  std::string last_modified;
  std::string hashvalue;
  std::string dirID;
  int64_t size;
  std::string origin;
  std::string lastSyncedHashValue;
  int32_t versions;
  std::optional<std::string> conflictId;
  std::optional<std::map<std::string, std::string>> dirIDs;
};

struct CloudFolderMetadata {
  std::string uuid;
  std::string device;
  std::string folder;
  std::string path;
  std::string created_at;
  std::optional<std::map<std::string, std::string>> dirIDs;
};

struct CloudMetadataResult {
  bool success;
  std::vector<CloudFileMetadata> files;
  std::vector<CloudFolderMetadata> directories;
};

struct LocalFolderCreateMetadata {
  std::string absPath;
  std::string path;
  std::string folder;
  std::string uuid;
  std::string device;
  std::string created_at;
  std::optional<std::map<std::string, std::string>> dirIDs;
};

struct LocalFolderDeleteMetadata {
  std::string absPath;
  std::string path;
  std::string folder;
};

struct LocalFileRenameMetadata {
  FileMetadata oldFile;
  CloudFileMetadata newFile;
};

struct ReconciliationResult {
  std::vector<CloudFileMetadata> filesToDownload;
  std::vector<FileMetadata> filesToDeleteLocal;
  std::vector<LocalFolderCreateMetadata> foldersToCreateLocal;
  std::vector<LocalFolderDeleteMetadata> foldersToDeleteLocal;
  std::vector<CloudFileMetadata> filesInConflict;
  std::vector<CloudFileMetadata> filesToUpdate;
  std::vector<LocalFileRenameMetadata> filesToRename;
};

struct FileUploadMetadata {
  std::string mtime;
  int64_t size;
  std::string hashvalue;
};

inline void from_json(const nlohmann::json &j, CloudFileMetadata &f) {
  f.uuid = j.value("uuid", "");
  f.dirID = j.value("dirID", "");
  f.path = j.value("path", "/");
  f.filename = j.value("filename", "");
  f.last_modified = j.value("last_modified", "");
  f.hashvalue = j.value("hashvalue", "");
  f.lastSyncedHashValue = f.hashvalue;
  f.size = j.value("size", 0LL);
  f.origin = j.value("origin", "");
  f.versions = j.value("versions", 1);
  f.conflictId = j.value("conflictId", "");
}

inline void from_json(const nlohmann::json &j, CloudFolderMetadata &f) {
  f.uuid = j.value("uuid", "");
  f.device = j.value("device", "");
  f.folder = j.value("folder", "");
  f.path = j.value("path", "/");
  f.created_at = j.value("created_at", "");
}

inline void to_json(nlohmann::json &j, const DirectoryMetadata &d) {
  j = nlohmann::json{{"uuid", d.uuid},
                     {"device", d.device},
                     {"folder", d.folder},
                     {"path", d.path},
                     {"created_at", d.created_at}};
}

inline std::string syncStatusToString(SyncStatus status) {
  switch (status) {
  case SyncStatus::NEW:
    return "new";
  case SyncStatus::DELETE:
    return "delete";
  case SyncStatus::MODIFIED:
    return "modified";
  case SyncStatus::FILE_LINKED:
    return "FILE_LINKED";
  case SyncStatus::RENAME:
    return "rename";
  default:
    return "unknown";
  }
}

inline SyncStatus stringToSyncStatus(const std::string &syncStatus) {
  if (syncStatus == "new")
    return SyncStatus::NEW;
  if (syncStatus == "delete")
    return SyncStatus::DELETE;
  if (syncStatus == "modified")
    return SyncStatus::MODIFIED;
  if (syncStatus == "rename")
    return SyncStatus::RENAME;
  if (syncStatus == "FILE_LINKED")
    return SyncStatus::FILE_LINKED;
  return SyncStatus::UNKNOWN;
}
} // namespace sync_app
