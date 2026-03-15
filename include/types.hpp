#pragma once

#include <QString>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sync_app {

enum class SyncStatus {
  NEW,
  RENAME,
  MODIFIED,
  DELETE,
  FILE_LINKED,
  UNKNOWN,
  MOVED,
  MOVE_CANDIDATE
};

enum class QPriority {
  FOLDER_CREATE,
  FOLDER_RENAME,
  FOLDER_MOVED,
  FOLDER_DELETE,
  FILE_MODIFIED,
  FILE_DELETE,
  FILE_UPLOAD,
  FILE_RENAME,
  FILE_MOVED,
  UNKNOWN
};

enum class ActivityStatus {
  QUEUED,
  DONE,
  ERROR,
  SYNCING,
  UPLOAD,
  DOWNLOAD,
  LOCAL_DELETE,
  CLOUD_DELETE,
  LOCAL_RENAME,
  CLOUD_RENAME,
  LOCAL_MOVE,
  CLOUD_MOVE,
  LOCAL_FOLDER_CREATE,
  CLOUD_FOLDER_CREATE,
  UNKNOWN
};

struct ScannedFile {
  std::string path; // Relative path from sync root (e.g. "/foo/bar.txt")
  std::string filename;
  std::string absPath;
  std::string inode;
  std::string hash;
  int64_t size;
  int64_t mtime; // UTC timestamp
};

struct InodeCacheInfo {
  std::string inode;
  bool isDir;
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
  std::string uuid = "";
  std::string path = "";
  std::string filename = "";
  std::string last_modified; // Store as string for SQLite compatibility
  std::string hashvalue = "";
  int64_t size = 0;
  std::string dirID = "";
  std::string inode = "";
  std::string absPath = "";
  int32_t versions = 1;
  std::string origin = "";
  std::string lastSyncedHashValue = "";
  std::optional<std::string> conflictId = "";
  std::string lastSynced = "";
};

struct DirectoryMetadata {
  std::string uuid;
  std::string device;
  std::string folder;
  std::string path;
  std::string created_at;
  std::string absPath;
  std::string inode;
  std::string lastSynced = "";
};

struct FileQueueEntry {
  std::string uuid = "";
  std::string path = "";
  std::string filename = "";
  std::string last_modified; // Store as string for SQLite compatibility
  std::string hashvalue = "";
  int64_t size = 0;
  std::string dirID = "";
  std::string inode = "";
  std::string absPath = "";
  int32_t versions = 1;
  std::string origin = "";
  std::string lastSyncedHashValue = "";
  std::string lastSynced = "";

  std::string sync_status;
  std::optional<size_t> priority;
  std::optional<std::string> old_path;
  std::optional<std::string> old_filename;
  std::optional<std::map<std::string, std::string>> dirIDs;
};

struct DirectoryQueueEntry {
  std::string uuid;
  std::string device;
  std::string folder;
  std::string path;
  std::string created_at;
  std::string absPath;
  std::string inode;
  std::string lastSynced;
  std::string sync_status;
  std::optional<std::string> old_path;
  std::optional<size_t> priority;
  std::optional<std::map<std::string, std::string>> dirIDs;
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
  std::optional<std::string> last_updated;
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
  std::string inode;
  std::string path;
  std::string uuid;
  std::string device;
  std::string created_at;
  std::string folder;
};

struct LocalFileRenameMetadata {
  FileMetadata oldFile;
  CloudFileMetadata newFile;
};

struct OfflineFileMoveMetadata {
  FileQueueEntry oldFile;
  FileQueueEntry newFile;
};

struct OfflineDirMoveMetadata {
  DirectoryQueueEntry oldFile;
  DirectoryQueueEntry newFile;
};

struct LocalDirRenameMetadata {
  DirectoryMetadata localDir;
  CloudFolderMetadata cloudDir;
};

struct OfflineReconResult {

  std::vector<FileMetadata> filesToDownload;
  std::vector<FileQueueEntry> filesToDeleteLocal;
  std::vector<DirectoryMetadata> foldersToCreateLocal;
  std::vector<DirectoryQueueEntry> foldersToDeleteLocal;
  std::vector<FileQueueEntry> filesToMove;
  std::vector<DirectoryQueueEntry> dirsToMove;
};

struct ReconciliationResult {
  std::vector<CloudFileMetadata> filesToDownload;
  std::vector<FileMetadata> filesToDeleteLocal;
  std::vector<LocalFolderCreateMetadata> foldersToCreateLocal;
  std::vector<LocalFolderDeleteMetadata> foldersToDeleteLocal;
  std::vector<CloudFileMetadata> filesInConflict;
  std::vector<CloudFileMetadata> filesToUpdate;
  std::vector<LocalFileRenameMetadata> filesToRename;
  std::vector<FileQueueEntry> filesToMove;
  std::vector<DirectoryQueueEntry> dirsToMove;
};

struct FileUploadMetadata {
  std::string mtime;
  int64_t size;
  std::string hashvalue;
};

struct DownloadProgress {
  double progress = 0.0;
  int64_t size = 0;
  std::string fname;
  std::string fpath;
  bool isDownloading = false;
  bool inQueue = true;
  std::string meta;
  bool isDownloaded = false;
};

struct UploadProgress {
  double progress = 0.0;
  int64_t size = 0;
  std::string fname;
  std::string fpath;
  bool isUploading = false;
  bool inQueue = true;
  std::string meta;
  bool isUploaded = false;
};

struct SyncItem {
  std::string id;
  std::string name;
  std::string path;
  std::string meta;
  std::string type; // "upload" or "download"
  double progress = 0.0;
  int64_t size = 0;
  bool inQueue = true;
  bool isActive = false; // replaces isDownloading/isUploading
  bool isDone = false;   // replaces isDownloaded/isUploaded
  bool isError = false;  //
};

struct ActivityItem {
  QString id; // unique id
  QString name;
  QString percentage = "0%";
  QString path;
  QString meta;   // pdf doc folder
  QString type;   // upload or download or delete or rename
  QString status; // queued or done or error or syncing
  double progress = 0.0;
  QString size = "0B";
};

inline void from_json(const nlohmann::json &j, CloudFileMetadata &f) {
  f.uuid = j.value("uuid", "");
  f.dirID = j.value("dirID", "");
  f.path = j.value("path", "/");
  f.filename = j.value("filename", "");
  f.last_modified = j.value("last_modified", "");
  f.last_updated = j.value("last_updated", "");
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

inline std::string activityToString(ActivityStatus activity) {
  switch (activity) {
  case ActivityStatus::DONE:
    return "done";
  case ActivityStatus::ERROR:
    return "error";
  case ActivityStatus::SYNCING:
    return "syncing";
  case ActivityStatus::QUEUED:
    return "queued";
  case ActivityStatus::UPLOAD:
    return "upload";
  case ActivityStatus::DOWNLOAD:
    return "download";
  case ActivityStatus::LOCAL_FOLDER_CREATE:
    return "local_folder_create";
  case ActivityStatus::CLOUD_FOLDER_CREATE:
    return "cloud_folder_create";
  case ActivityStatus::LOCAL_DELETE:
    return "local_delete";
  case ActivityStatus::CLOUD_DELETE:
    return "cloud_delete";
  case ActivityStatus::LOCAL_MOVE:
    return "local_move";
  case ActivityStatus::CLOUD_MOVE:
    return "cloud_move";
  case ActivityStatus::CLOUD_RENAME:
    return "cloud_rename";
  case ActivityStatus::LOCAL_RENAME:
    return "local_rename";
  case ActivityStatus::UNKNOWN:
    return "unknown";
  }
}

inline size_t qPriorityToInt(QPriority priority) {
  switch (priority) {
  case QPriority::FOLDER_CREATE:
    return 7;
  case QPriority::FOLDER_DELETE:
    return 1;
  case QPriority::FOLDER_MOVED:
    return 2;
  case QPriority::FOLDER_RENAME:
    return 3;
  case QPriority::FILE_RENAME:
    return 5;
  case QPriority::FILE_DELETE:
    return 4;
  case QPriority::FILE_MODIFIED:
    return 9;
  case QPriority::FILE_MOVED:
    return 6;
  case QPriority::FILE_UPLOAD:
    return 8;
  default:
    return 10;
  }
}

inline QPriority intToQPriority(size_t priority) {
  switch (priority) {
  case 1:
    return QPriority::FOLDER_DELETE;
  case 2:
    return QPriority::FOLDER_MOVED;
  case 3:
    return QPriority::FOLDER_RENAME;
  case 4:
    return QPriority::FILE_DELETE;
  case 5:
    return QPriority::FOLDER_RENAME;
  case 6:
    return QPriority::FILE_MOVED;
  case 7:
    return QPriority::FOLDER_CREATE;
  case 8:
    return QPriority::FILE_UPLOAD;
  case 9:
    return QPriority::FILE_MODIFIED;
  default:
    return QPriority::UNKNOWN;
  }
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
  case SyncStatus::MOVED:
    return "moved";
  case SyncStatus::MOVE_CANDIDATE:
    return "move_candidate";
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
  if (syncStatus == "moved")
    return SyncStatus::MOVED;
  if (syncStatus == "move_candidate")
    return SyncStatus::MOVE_CANDIDATE;
  return SyncStatus::UNKNOWN;
}

} // namespace sync_app
