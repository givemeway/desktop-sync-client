#pragma once
#include "qobject.h"
#include "qtmetamacros.h"
#include "types.hpp"
#include <atomic>
#include <condition_variable>
#include <string>
#include <thread>
namespace sync_app {

class DatabaseManager;
class ApiClient;
class FileSystemScanner;
class SyncWorker;
class ReconciliationService;
class ThreadPool;
class SyncTree;
struct ActivityStore;

#ifdef _WIN32
class CloudFilesProvider;
#endif

class CloudSyncWorker : public QObject {
  Q_OBJECT
public:
  explicit CloudSyncWorker(DatabaseManager &dbManager, ApiClient &apiClient,
                           ReconciliationService &reconcile,
                           FileSystemScanner &scanner, SyncWorker &syncWorker,
                           ActivityStore &activityStore,
                           ThreadPool &uploadThreadPool,
                           ThreadPool &downloadThreadPool,
                           const std::string &syncPath,
                           const std::string &userEmail, SyncTree &syncTree,
#ifdef _WIN32
                           CloudFilesProvider *cfProvider = nullptr,
#endif
                           QObject *parent = nullptr);
  ~CloudSyncWorker();
  void start();
  void stop();
  std::vector<std::string> getPathComponents(const std::string &path);

  void startSync();
  void stopSync();
  void getQuota();

private:
#ifdef _WIN32
  CloudFilesProvider *m_cfProvider = nullptr;
#endif
  DatabaseManager &m_dbManager;
  ApiClient &m_apiClient;
  ActivityStore &m_activityStore;
  ReconciliationService &m_reconcile;
  FileSystemScanner &m_scanner;
  SyncWorker &m_syncWorker;
  ThreadPool &m_uploadThreadPool;
  ThreadPool &m_downloadThreadPool;
  SyncTree &m_syncTree;

  std::string m_syncPath;
  std::string m_userEmail;
  std::thread m_uploadThread;
  std::thread m_downloadThread;
  std::atomic<bool> m_stopThread;
  std::mutex m_syncMutex;
  std::mutex m_syncDownMutex;
  std::mutex m_syncUpMutex;
  std::atomic<size_t> m_tasksPending = 0;
  std::atomic<int> m_upSyncTasks = 0;
  std::condition_variable m_tasksCV;
  std::mutex m_tasksPendingMutex;
  std::condition_variable m_upSyncTasksCV;

  bool pollCloudToSyncToLocal();
  void runSyncDown();
  void runSyncUp();
  void processQueueToSyncUp();
  void controlThread();

  std::string getCurrentTime();
  bool createLocalDirectory(const std::string &path);
  DirectoryMetadata getDirectoryMetadata(const std::string &path,
                                         const std::string &uuid);
  FileMetadata getFileMetadata(const CloudFileMetadata &cloudFile,
                               const std::string &absPath);
  FileMetadata constructFileMetadata(const FileQueueEntry &f);

  void
  processFilesToDownload(const std::vector<CloudFileMetadata> &filesToDownload);

  void
  processFilesToDelete(const std::vector<FileMetadata> &filesToDeleteLocal);

  void processFoldersToCreate(
      const std::vector<LocalFolderCreateMetadata> &foldersToCreateLocal);

  void processFoldersToDelete(
      const std::vector<LocalFolderDeleteMetadata> &foldersToDeleteLocal);

  void processFilesToMove(const std::vector<FileQueueEntry> &filesToMove);

  void processDirsToMove(const std::vector<DirectoryQueueEntry> &dirsToMove);

  void
  processFilesInConflict(const std::vector<CloudFileMetadata> &filesInConflict);

  void
  processFilesToUpdate(const std::vector<CloudFileMetadata> &filesToUpdate);

  std::optional<bool> deleteFolderInCloud(DirectoryQueueEntry &dq);

  std::optional<bool> moveFolderInCloud(DirectoryQueueEntry &dq,
                                        bool isRename = true);

  std::optional<bool> createFolderInCloud(DirectoryQueueEntry &dq);

  std::optional<bool> uploadFile(ApiClient &client,
                                 CloudFilesProvider &cfProvider,
                                 const FileQueueEntry &fq);

  std::optional<bool> uploadModifiedFile(ApiClient &client,
                                         CloudFilesProvider &cfProvider,
                                         const FileQueueEntry &fq);

  std::optional<bool> deleteFile(ApiClient &client, const FileQueueEntry &fq);

  std::optional<bool> renameFile(ApiClient &client,
                                 CloudFilesProvider &cfProvider,
                                 const FileQueueEntry &fq);

  std::optional<bool> moveFile(ApiClient &client, const FileQueueEntry &fq);

  void updateFileQueueEntry(FileQueueEntry &fq, FileQueueEntry &fq_1,
                            std::vector<FileQueueEntry> &filesInConflict,
                            const SyncStatus &status);

  void updateActivityMap(ReconciliationResult &result);

  void updateActivity(const std::string &key, const SyncItem &item);

  void addActivity(const std::string &key, const SyncItem &item);

  void removeActivity(const std::string &key);

  void initActivityAndPriorityQ();

  std::optional<std::vector<DirectoryMetadata>>
  getFileDirTree(const FileQueueEntry &fq);

signals:
  void activityAdded(const std::string &key, const SyncItem &item,
                     bool isDBActivity = false);
  void isSyncing(bool isSyncing);
  void activityUpdated(const std::string &key, const SyncItem &item);
  void activityRemoved(const std::string &key);
  void syncStarted();
  void syncStopped();
  void errorOccurred(const QString &message);
  void quotaFetched(const int64_t &quota);
};

} // namespace sync_app
