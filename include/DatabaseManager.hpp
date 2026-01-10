#pragma once
#include "types.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sync {
struct pathParts {
  std::string device;
  std::string folder;
};
class DatabaseManager {
public:
  DatabaseManager(const std::string &dbPath, const std::string &syncPath);
  ~DatabaseManager();

  // Connection management
  bool open();
  void close();
  void initializeSchema();

  // File operations
  std::optional<std::vector<FileMetadata>> getAllFiles();
  std::optional<std::vector<FileQueueEntry>> getAllQueueFiles();
  std::optional<std::vector<DirectoryQueueEntry>> getAllQueueDirectories();
  std::optional<FileMetadata> getFileByOrigin(const std::string &origin);
  std::optional<FileMetadata> getFileByPath(const std::string &path,
                                            const std::string &filename);
  std::optional<FileQueueEntry> getFileQueueByPath(const std::string &path,
                                                   const std::string &filename);
  std::optional<std::vector<FileMetadata>>
  getAllFilesInDirectory(const std::string &path);
  bool insertFile(const FileMetadata &file, const FileQueueEntry &fileQueue);
  bool updateFile(const FileMetadata &file);
  bool deleteFile(const std::string &path, const std::string &filename,
                  const FileQueueEntry &fq);
  bool deleteFilesByPath(const std::string &path);
  bool upsertFile(const FileMetadata &file);

  // Directory operations
  std::optional<std::vector<DirectoryMetadata>> getAllDirectories();
  std::optional<DirectoryMetadata> getDirectoryByPath(const std::string &device,
                                                      const std::string &folder,
                                                      const std::string &path);
  bool moveDirectory(const std::string &path, const std::string &oldPath,
                     const DirectoryQueueEntry &dq);
  bool insertDirectory(const DirectoryMetadata &dir,
                       const DirectoryQueueEntry &dirQueue);
  bool updateDirectory(const DirectoryMetadata &dir);
  bool deleteDirectory(const std::string &path);
  bool deleteFolderWithTransaction(const std::string &path,
                                   const DirectoryQueueEntry &dq);
  bool upsertDirectory(const DirectoryMetadata &dir);

  // File Queue operations
  std::optional<std::vector<FileQueueEntry>> getFileQueue();
  bool insertFileQueue(const FileQueueEntry &entry);
  bool updateFileQueue(const FileQueueEntry &entry);
  bool deleteFileQueue(const std::string &path, const std::string &filename);
  bool upsertFileQueue(const FileQueueEntry &entry);

  // Directory Queue operations
  std::optional<std::vector<DirectoryQueueEntry>> getDirectoryQueue();
  bool insertDirectoryQueue(const DirectoryQueueEntry &entry);
  bool updateDirectoryQueue(const DirectoryQueueEntry &entry);
  bool deleteDirectoryQueue(const std::string &uuid);
  bool upsertDirectoryQueue(const DirectoryQueueEntry &entry);
  bool moveDirectoryQueue(const std::string &path, const std::string &oldPath);

  pathParts getFolderDevice(const std::filesystem::path &path);

private:
  std::string m_dbPath;
  std::string m_syncPath;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sync
