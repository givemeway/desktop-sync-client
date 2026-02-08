#pragma once
#include "types.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace sync_app {
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

  bool deleteAllFilesInQueue();

  bool deleteAllDirsInQueue();

  bool moveFile(const FileMetadata &f, const FileQueueEntry &fq);

  bool moveFiles(
      const std::map<std::string, std::vector<FileQueueEntry>> &movedFiles);

  bool insertFile(const FileMetadata &file, const FileQueueEntry &fileQueue);

  bool updateFile(const FileMetadata &file);

  bool deleteFile(const std::string &path, const std::string &filename,
                  const FileQueueEntry &fq);

  bool deleteFilesByPath(const std::string &path);

  bool upsertFile(const FileMetadata &file);

  bool deleteFileByPath(const std::string &path, const std::string &filename);

  bool updateFileWithTransaction(const FileMetadata &f, const std::string &path,
                                 const std::string &filename);
  // Directory operations
  std::optional<std::vector<DirectoryMetadata>> getAllDirectories();

  std::optional<std::vector<DirectoryMetadata>>
  getDirsByPath(const std::string &path);

  std::optional<DirectoryMetadata> getDirectoryByPath(const std::string &device,
                                                      const std::string &folder,
                                                      const std::string &path);
  bool createDirectoryPaths(const std::vector<DirectoryMetadata> &dirs);

  bool
  insertFileWithDirectory(FileMetadata &f,
                          const std::vector<DirectoryMetadata> &pathComponents);
  bool
  insertFileAndQueueWithDirectory(FileMetadata &cloudFile,
                                  FileMetadata &conflictedFile,
                                  FileQueueEntry &conflictedFileQueue,
                                  DirectoryQueueEntry &conflictedDirQueue,
                                  const std::vector<DirectoryMetadata> &dirs);
  std::optional<
      std::tuple<std::vector<FileQueueEntry>, std::vector<DirectoryQueueEntry>>>
  getDirQueueByInode(const std::string &inode);

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

  std::optional<DirectoryQueueEntry>
  getDirectoryQueueByPath(const std::string &device, const std::string &folder,
                          const std::string &path);

  bool insertDirectoryQueue(const DirectoryQueueEntry &entry);

  bool updateDirectoryQueue(const DirectoryQueueEntry &entry);

  bool deleteDirectoryQueue(const std::string &device,
                            const std::string &folder, const std::string &path);

  bool upsertDirectoryQueue(const DirectoryQueueEntry &entry);

  bool moveDirectoryQueue(const std::string &path, const std::string &oldPath);

  bool deleteOrphanItemsInQueue(const std::string &path,
                                const std::string &oldPath);

  pathParts getFolderDevice(const std::filesystem::path &path);

  std::recursive_mutex &getSyncMutex();

  bool insertFilesAndDirectories(const DirectoryMetadata &dir,
                                 const DirectoryQueueEntry &dirQ,
                                 const std::string &path,
                                 const std::string &oldPath);
  std::optional<std::vector<FileQueueEntry>>
  getFileQueueByInode(const std::string &inode);

  bool updateMovedFile(const FileQueueEntry &fq, const FileMetadata &f);

  std::optional<std::vector<DirectoryMetadata>>
  getAllDirsInPath(const std::string &path);

  std::vector<FileMetadata> getFilesInDirectory(const std::string &path);
  bool reconcileLocalState(const std::vector<FileMetadata> &filesToAdd,
                           const std::vector<FileQueueEntry> &filesToDelete,
                           const std::vector<DirectoryMetadata> &dirsToAdd,
                           const std::vector<DirectoryQueueEntry> &dirsToDelete,
                           const std::vector<FileMetadata> &filesToModify,
                           const std::vector<FileQueueEntry> &filesToMove,
                           const std::vector<DirectoryQueueEntry> &dirsToMove);

private:
  std::string m_dbPath;
  std::string m_syncPath;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::recursive_mutex m_syncMutex;
};

} // namespace sync_app
