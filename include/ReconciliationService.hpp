#pragma once
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "types.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sync {

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
  ReconciliationService(DatabaseManager &dbManager,
                        const std::string &syncPath);

  ReconciliationResult
  reconcile(const std::vector<CloudFileMetadata> &cloudFiles,
            const std::vector<CloudFolderMetadata> &cloudDirs,
            const std::vector<FileMetadata> &dbFiles,
            const std::vector<DirectoryMetadata> &dbDirs);

  void reconcileLocalState(const std::vector<ScannedFile> &scannedFiles,
                           const std::vector<ScannedDirectory> &scannedDirs);

private:
  DatabaseManager &m_dbManager;
  std::string m_syncPath;
  FileSystemScanner m_scanner;

  // Helper methods
  std::vector<std::string> splitDbPath(const std::string &p);
  std::optional<struct PathDiff>
  findRenameDepthFromPath(const std::string &oldPath,
                          const std::string &newPath);
  std::vector<RenameInfo>
  detectDirRenames(const std::vector<DirectoryQueueEntry> &entries);
  std::vector<RenameInfo>
  collapseDirRenames(const std::vector<RenameInfo> &renames);
  void reconcileDirRenamedCandidates(
      const std::vector<RenameInfo> &localFoldersRenamed);

  std::string getUniqueKey(const std::string &dir, const std::string &filename);

  // Internal state management helpers
  std::optional<FileQueueEntry> localInQueueByAnyPath(
      const CloudFileMetadata &cloudFile,
      const std::map<std::string, FileQueueEntry> &localQueueByOrigin,
      const std::map<std::string, std::vector<FileQueueEntry>>
          &localQueueByUuid,
      const std::map<std::string, FileQueueEntry> &localQueueByPath);
};

struct PathDiff {
  int32_t depth;
  std::optional<std::string> oldSegment;
  std::optional<std::string> newSegment;
};

} // namespace sync
