#include "types.hpp"
#include <atomic>
#include <string>
#include <thread>
namespace sync_app {

class DatabaseManager;
class ApiClient;
class FileSystemScanner;
class SyncWorker;
class ReconciliationService;

class CloudSyncWorker {
public:
  CloudSyncWorker(DatabaseManager &dbManager, ApiClient &apiClient,
                  ReconciliationService &reconcile, FileSystemScanner &scanner,
                  SyncWorker &syncWorker, const std::string &syncPath,
                  const std::string &userEmail);
  ~CloudSyncWorker();
  void start();
  void stop();

private:
  DatabaseManager &m_dbManager;
  ApiClient &m_apiClient;
  ReconciliationService &m_reconcile;
  FileSystemScanner &m_scanner;
  SyncWorker &m_syncWorker;
  std::string m_syncPath;
  std::string m_userEmail;
  std::thread m_workerThread;
  std::atomic<bool> m_stopThread;
  void pollCloudToSyncToLocal();
  void run();
  void processQueueToSyncUp();
  std::string getCurrentTime();
  bool createLocalDirectory(const std::string &path);
  DirectoryMetadata getDirectoryMetadata(const std::string &path,
                                         const std::string &uuid);
  FileMetadata getFileMetadata(const CloudFileMetadata &cloudFile,
                               const std::string &absPath);
  FileMetadata constructFileMetadata(const FileQueueEntry &f);

  std::vector<std::string> getPathComponents(const std::string &path);
  void
  processFilesToDownload(const std::vector<CloudFileMetadata> &filesToDownload);

  void
  processFilesToDelete(const std::vector<FileMetadata> &filesToDeleteLocal);

  void processFoldersToCreate(
      const std::vector<LocalFolderCreateMetadata> &foldersToCreateLocal);

  void processFoldersToDelete(
      const std::vector<LocalFolderDeleteMetadata> &foldersToDeleteLocal);
  void processFilesToRename(
      const std::vector<LocalFileRenameMetadata> &filesToRename);

  void
  processFilesInConflict(const std::vector<CloudFileMetadata> &filesInConflict);

  void
  processFilesToUpdate(const std::vector<CloudFileMetadata> &filesToUpdate);
};

} // namespace sync_app
