#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sync {

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
};

struct DirectoryMetadata {
  std::string uuid;
  std::string device;
  std::string folder;
  std::string path;
  std::string created_at;
  std::string absPath;
  std::string inode;
};

struct FileQueueEntry : public FileMetadata {
  std::string sync_status;
  std::optional<std::string> old_path;
  std::optional<std::string> old_filename;
  
  // Default constructor
  FileQueueEntry() = default;
  
  // Constructor from FileMetadata (similar to spread operator)
  FileQueueEntry(const FileMetadata& f) 
    : FileMetadata(f) {}
};

struct DirectoryQueueEntry : public DirectoryMetadata {
  std::string sync_status;
  std::optional<std::string> old_path;
  
  // Default constructor
  DirectoryQueueEntry() = default;
  
  // Constructor from DirectoryMetadata (similar to spread operator)
  DirectoryQueueEntry(const DirectoryMetadata& d) 
    : DirectoryMetadata(d) {}
};

struct CloudFileMetadata {
  std::string uuid;
  std::string path;
  std::string filename;
  std::string last_modified;
  std::string hashvalue;
  int64_t size;
  std::string origin;
  std::string lastSyncedHashValue;
  int32_t versions;
  std::optional<std::string> conflictId;
};

struct CloudFolderMetadata {
  std::string uuid;
  std::string device;
  std::string folder;
  std::string path;
  std::string created_at;
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

} // namespace sync
