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
  DatabaseManager(const std::string &dbPath);
  ~DatabaseManager();

  // Connection management
  bool open();
  void close();
  void initializeSchema();

  // File operations
  std::vector<FileMetadata> getAllFiles();
  std::vector<FileQueueEntry> getAllQueueFiles();
  std::optional<FileMetadata> getFileByOrigin(const std::string &origin);
  std::optional<FileMetadata> getFileByPath(const std::string &path,
                                            const std::string &filename);
  bool insertFile(const FileMetadata &file);
  bool updateFile(const FileMetadata &file);
  bool deleteFile(const std::string &origin);
  bool upsertFile(const FileMetadata &file);

  // Directory operations
  std::vector<DirectoryMetadata> getAllDirectories();
  std::optional<DirectoryMetadata> getDirectoryByPath(const std::string &device,
                                                      const std::string &folder,
                                                      const std::string &path);
  bool insertDirectory(const DirectoryMetadata &dir);
  bool updateDirectory(const DirectoryMetadata &dir);
  bool deleteDirectory(const std::string &uuid);
  bool upsertDirectory(const DirectoryMetadata &dir);

  // File Queue operations
  std::vector<FileQueueEntry> getFileQueue();
  bool insertFileQueue(const FileQueueEntry &entry);
  bool updateFileQueue(const FileQueueEntry &entry);
  bool deleteFileQueue(const std::string &origin);
  bool upsertFileQueue(const FileQueueEntry &entry);

  // Directory Queue operations
  std::vector<DirectoryQueueEntry> getDirectoryQueue();
  bool insertDirectoryQueue(const DirectoryQueueEntry &entry);
  bool updateDirectoryQueue(const DirectoryQueueEntry &entry);
  bool deleteDirectoryQueue(const std::string &uuid);
  bool upsertDirectoryQueue(const DirectoryQueueEntry &entry);

  pathParts getFolderDevice(const std::filesystem::path &path);

private:
  std::string m_dbPath;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sync
