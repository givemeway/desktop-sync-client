#pragma once
#include "DatabaseManager.hpp"
#include "ThreadPool.hpp"
#include "types.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sync_app {

class FileSystemScanner;
class ThreadPool;
struct RenameInfo {
  std::string inode;
  std::string uuid;
  std::string folder;
  std::string created_at;
  std::string device;
  int32_t depth;
  std::optional<std::string> oldSegment;
  std::optional<std::string> newSegment;
  std::string oldPath;
  std::string newPath;
};

class ReconciliationService {
public:
  ReconciliationService(DatabaseManager &dbManager, FileSystemScanner &scanner,
                        ThreadPool &threadpool, const std::string &syncPath);

  ReconciliationResult
  reconcile(const std::vector<CloudFileMetadata> &cloudFiles,
            const std::vector<CloudFolderMetadata> &cloudDirs,
            const std::vector<FileMetadata> &dbFiles,
            const std::vector<DirectoryMetadata> &dbDirs);

  bool reconcileLocalState(const std::vector<ScannedFile> &scannedFiles,
                           const std::vector<ScannedDirectory> &scannedDirs);

private:
  DatabaseManager &m_dbManager;
  std::string m_syncPath;
  FileSystemScanner &m_scanner;
  ThreadPool &m_threadPool;

  // Helper methods
  std::vector<std::string> splitDbPath(const std::string &p);
  std::string getUniqueKey(const std::string &dir, const std::string &filename);

  // Internal state management helpers
  std::optional<FileQueueEntry> localInQueueByAnyPath(
      const CloudFileMetadata &cloudFile,
      const std::map<std::string, FileQueueEntry> &localQueueByOrigin,
      const std::map<std::string, std::vector<FileQueueEntry>>
          &localQueueByUuid,
      const std::map<std::string, FileQueueEntry> &localQueueByPath);
  std::vector<std::string> getPathComponents(const std::string &path);

  FileQueueEntry createFileMetadata(const FileMetadata &f,
                                    const ScannedFile &sFile,
                                    bool isNewfile = true,
                                    const std::string &dirID = "");
  DirectoryMetadata createDirectoryMetadata(const std::string &path,
                                            bool isNewDir = true,
                                            const std::string &uuid = "");
  DirectoryQueueEntry createDirectoryQueueEntry(const DirectoryMetadata &d,
                                                SyncStatus &status);
  DirectoryQueueEntry createFileQueueEntry(const FileMetadata &f,
                                           SyncStatus &status);
};

} // namespace sync_app
