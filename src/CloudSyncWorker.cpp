#include "CloudSyncWorker.hpp"
#include "ActivityStore.hpp"
#include "ApiClient.hpp"
#include "CloudFilesProvider.hpp"
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "ReconciliationService.hpp"
#include "SyncTree.hpp"
#include "SyncWorker.hpp"
#include "ThreadPool.hpp"
#include "Utility.hpp"
#include "UuidUtils.hpp"
#include "qobject.h"
#include "types.hpp"
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <rpcdce.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
namespace fs = std::filesystem;

namespace sync_app {

using filePriorityQ = decltype(std::priority_queue<
                               FileQueueEntry, std::vector<FileQueueEntry>,
                               Utility::PriorityComparator<FileQueueEntry>>());
using dirPriorityQ =
    decltype(std::priority_queue<
             DirectoryQueueEntry, std::vector<DirectoryQueueEntry>,
             Utility::PriorityComparator<DirectoryQueueEntry>>());

using FileQMapContainer =
    decltype(std::map<std::string, std::map<std::string, filePriorityQ>>());

using DirQMapContainer =
    decltype(std::map<std::string, std::map<std::string, dirPriorityQ>>());

CloudSyncWorker::CloudSyncWorker(
    DatabaseManager &dbManager, ApiClient &apiClient,
    ReconciliationService &reconcile, FileSystemScanner &scanner,
    SyncWorker &syncWorker, ActivityStore &activityStore,
    ThreadPool &uploadThreadPool, ThreadPool &downloadThreadPool,
    const std::string &syncPath, const std::string &userEmail,
    SyncTree &syncTree,
#ifdef _WIN32
    CloudFilesProvider *cfProvider,
#endif
    QObject *parent)
    : m_dbManager(dbManager), m_apiClient(apiClient), m_reconcile(reconcile),
      m_scanner(scanner), m_syncWorker(syncWorker),
      m_activityStore(activityStore), m_uploadThreadPool(uploadThreadPool),
      m_downloadThreadPool(downloadThreadPool), m_syncPath(syncPath),
      m_userEmail(userEmail), m_syncTree(syncTree),
#ifdef _WIN32
      m_cfProvider(cfProvider),
#endif
      m_stopThread(false) {
}

CloudSyncWorker::~CloudSyncWorker() { stop(); }

template <typename T>
static ActivityMetadata buildActivityMetadata(const T &t,
                                              const std::string &syncPath,
                                              const std::string &event) {
  ActivityMetadata item;
  if constexpr (std::is_same_v<T, SyncItem>) {
    item.uuid = t.id;
    item.absPath = t.path == "/" ? syncPath : syncPath + t.path;
    item.path = t.path;
    item.meta = t.meta;
    item.size = std::to_string(t.size);
    item.event = event;
    item.lastUpdated = std::to_string(Utility::getCurrentTime());
    item.name = t.name;
  }
  if constexpr (std::is_same_v<T, ActivityItem>) {
    item.uuid = t.id.toStdString();
    item.absPath = t.path == "/" ? syncPath : syncPath + t.path;
    item.path = t.path.toStdString();
    item.meta = t.meta.toStdString();
    item.size = t.size.toStdString();
    item.event = event;
    item.lastUpdated = std::to_string(Utility::getCurrentTime());
    item.name = t.name.toStdString();
  }

  return item;
}

void CloudSyncWorker::updateActivityMap(ReconciliationResult &reconciledItems) {

  auto folderDeleteActivity =
      Utility::convertToActivity<LocalFolderDeleteMetadata>(
          reconciledItems.foldersToDeleteLocal, ActivityStatus::LOCAL_DELETE);

  for (const auto &activity : folderDeleteActivity) {
    auto item = activity;
    item.type = "Deleted";
    m_dbManager.insertActivity(activity);
    addActivity(activity.id, activity);
  }

  auto folderMoveActivity = Utility::convertToActivity<DirectoryQueueEntry>(
      reconciledItems.dirsToMove, ActivityStatus::LOCAL_MOVE);

  for (const auto &activity : folderMoveActivity) {
    auto item = activity;
    item.type = "Moved";
    m_dbManager.insertActivity(item);
    addActivity(activity.id, activity);
  }

  auto fileDeleteActivity = Utility::convertToActivity<FileMetadata>(
      reconciledItems.filesToDeleteLocal, ActivityStatus::LOCAL_DELETE);

  for (const auto &activity : fileDeleteActivity) {
    auto item = activity;
    item.type = "Deleted";
    m_dbManager.insertActivity(item);
    addActivity(activity.id, activity);
  }

  auto fileMoveActivity = Utility::convertToActivity<FileQueueEntry>(
      reconciledItems.filesToMove, ActivityStatus::LOCAL_MOVE);

  for (const auto &activity : fileMoveActivity) {
    auto item = activity;
    item.type = "Moved";
    m_dbManager.insertActivity(item);
    addActivity(activity.id, activity);
  }

  auto fileDownloadActivity = Utility::convertToActivity<CloudFileMetadata>(
      reconciledItems.filesToDownload, ActivityStatus::DOWNLOAD);

  for (const auto &activity : fileDownloadActivity) {
    auto item = activity;
    item.type = "Added";
    m_dbManager.insertActivity(item);
    addActivity(activity.id, activity);
  }

  auto fileUpdateActivity = Utility::convertToActivity<CloudFileMetadata>(
      reconciledItems.filesToUpdate, ActivityStatus::DOWNLOAD);

  for (const auto &activity : fileUpdateActivity) {
    auto item = activity;
    item.type = "Edited";
    m_dbManager.insertActivity(item);
    addActivity(activity.id, activity);
  }

  auto folderCreateActivity =
      Utility::convertToActivity<LocalFolderCreateMetadata>(
          reconciledItems.foldersToCreateLocal,
          ActivityStatus::LOCAL_FOLDER_CREATE);

  for (const auto &activity : folderCreateActivity) {
    auto item = activity;
    item.type = "Added";
    m_dbManager.insertActivity(item);
    addActivity(activity.id, activity);
  }
}

void CloudSyncWorker::updateActivity(const std::string &key,
                                     const SyncItem &item) {
  m_activityStore.updateActivity(key, item);
  emit activityUpdated(key, item);
}

void CloudSyncWorker::addActivity(const std::string &key,
                                  const SyncItem &item) {
  m_activityStore.addActivity(key, item);
  emit activityAdded(key, item);
}

void CloudSyncWorker::removeActivity(const std::string &key) {
  m_activityStore.removeActivity(key);
  emit activityRemoved(key);
}

static void handleModifiedConflict(FileQueueEntry &cf, FileQueueEntry &lf,
                                   FileQMapContainer &filesInConflict,
                                   SyncStatus &localStatus) {
  switch (localStatus) {
  case SyncStatus::RENAME:
    // download the modified cloud file
    cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
    cf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
    // local file is renamed - it is diverged. we upload it as new
    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);
    return;

  case SyncStatus::MODIFIED:
    // download the modified cloud file
    cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
    cf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
    // upload the modified file as new
    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);
    return;

  case SyncStatus::DELETE:
    // prioritize the local delete. We delete it from cloud
    lf.priority = qPriorityToInt(QPriority::FILE_DELETE);
    lf.sync_status = syncStatusToString(SyncStatus::DELETE);

    filesInConflict[lf.uuid]["local"].push(lf);

    return;

  case SyncStatus::MOVED:
    // download the modified file
    cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
    cf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
    // local file moved - it is diverged. Upload it as new from new path
    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);

    return;

  default:

    return;
  }
}

static void handleRenameConflict(FileQueueEntry &cf, FileQueueEntry &lf,
                                 FileQMapContainer &filesInConflict,
                                 const SyncStatus &localStatus) {
  switch (localStatus) {
  case SyncStatus::RENAME:
    if (cf.hashvalue == lf.hashvalue) {
      // files are identical. Rename existing file to cloud name
      cf.priority = qPriorityToInt(QPriority::FILE_RENAME_CLOUD);
      cf.sync_status = syncStatusToString(SyncStatus::RENAME);

    } else {
      // cloud file renamed & modified. Download it.
      cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
      cf.sync_status = syncStatusToString(SyncStatus::CONFLICT_RENAME);
    }
    // upload the local renamed file as new to cloud
    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT_RENAME);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);

    return;

  case SyncStatus::MODIFIED:

    cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
    cf.sync_status = syncStatusToString(SyncStatus::CONFLICT_RENAME);

    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT_RENAME);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);

    return;
  case SyncStatus::DELETE:
    // prioritize the local delete. We delete it from cloud
    lf.priority = qPriorityToInt(QPriority::FILE_DELETE);
    lf.sync_status = syncStatusToString(SyncStatus::DELETE);

    filesInConflict[lf.uuid]["local"].push(lf);

    return;
  case SyncStatus::MOVED:
    if (cf.hashvalue == lf.hashvalue) {
      cf.priority = qPriorityToInt(QPriority::FILE_RENAME_CLOUD);
      cf.sync_status = syncStatusToString(SyncStatus::RENAME);

    } else {
      cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
      cf.sync_status = syncStatusToString(SyncStatus::CONFLICT_RENAME);
    }

    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT_RENAME);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);

    return;
  default:
    return;
  }
}
static void handleMoveConflict(FileQueueEntry &cf, FileQueueEntry &lf,
                               FileQMapContainer &filesInConflict,
                               const SyncStatus &localStatus) {
  switch (localStatus) {
  case SyncStatus::RENAME:
    if (cf.hashvalue == lf.hashvalue) {
      cf.priority = qPriorityToInt(QPriority::FILE_MOVED_CLOUD);
      cf.sync_status = syncStatusToString(SyncStatus::MOVED);

    } else {
      cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
      cf.sync_status = syncStatusToString(SyncStatus::CONFLICT_MOVE);
    }

    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT_MOVE);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);

    return;
  case SyncStatus::MODIFIED:

    cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
    cf.sync_status = syncStatusToString(SyncStatus::CONFLICT_MOVE);

    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT_MOVE);
    lf.uuid = lf.origin = UuidUtils::generate();

    return;
  case SyncStatus::DELETE:
    // prioritize the local delete. We delete it from cloud
    lf.priority = qPriorityToInt(QPriority::FILE_DELETE);
    lf.sync_status = syncStatusToString(SyncStatus::DELETE);

    filesInConflict[lf.uuid]["local"].push(lf);

    return;
  case SyncStatus::MOVED:
    if (cf.hashvalue == lf.hashvalue) {
      cf.priority = qPriorityToInt(QPriority::FILE_MOVED_CLOUD);
      cf.sync_status = syncStatusToString(SyncStatus::MOVED);

    } else {
      cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
      cf.sync_status = syncStatusToString(SyncStatus::CONFLICT_MOVE);
    }

    lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
    lf.sync_status = syncStatusToString(SyncStatus::CONFLICT_MOVE);
    lf.uuid = lf.origin = UuidUtils::generate();

    filesInConflict[cf.uuid]["cloud"].push(cf);
    filesInConflict[lf.uuid]["local"].push(lf);

    return;
  default:
    return;
  }
}
static void handleDeleteConflict(FileQueueEntry &cf, FileQueueEntry &lf,
                                 const SyncStatus &localStatus) {
  switch (localStatus) {
  case SyncStatus::RENAME:
    return;
  case SyncStatus::MODIFIED:
    return;
  case SyncStatus::DELETE:
    return;
  case SyncStatus::MOVED:
    return;
  default:
    return;
  }
}

bool CloudSyncWorker::pollCloudToSyncToLocal() {
  std::optional<CloudMetadataResult> result;
  std::optional<std::vector<FileMetadata>> dbFiles{std::nullopt};
  std::optional<std::vector<DirectoryMetadata>> dbDirs{std::nullopt};
  std::optional<std::vector<FileQueueEntry>> dbQFiles{std::nullopt};
  std::optional<std::vector<DirectoryQueueEntry>> dbQDirs{std::nullopt};
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    dbQFiles = m_dbManager.getAllQueueFiles();
    dbQDirs = m_dbManager.getAllQueueDirectories();
  }

  for (auto &dq : *dbQDirs) {
    if (dq.sync_status == syncStatusToString(SyncStatus::DELETE) ||
        dq.sync_status == syncStatusToString(SyncStatus::RENAME) ||
        dq.sync_status == syncStatusToString(SyncStatus::MOVED)) {
      return {};
    }
  }
  for (auto &fq : *dbQFiles) {
    if (fq.sync_status == syncStatusToString(SyncStatus::DELETE) ||
        fq.sync_status == syncStatusToString(SyncStatus::RENAME) ||
        fq.sync_status == syncStatusToString(SyncStatus::MOVED)) {
      return {};
    }
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    result = m_apiClient.getMetadata();
    dbFiles = m_dbManager.getAllFiles();
    dbDirs = m_dbManager.getAllDirectories();
  }

  auto cloudTree = new SyncTree("/");
  auto localTree = new SyncTree("/");

  if (result.has_value() && result->success) {
    std::vector<CloudFileMetadata> files = result->files;
    std::vector<CloudFolderMetadata> folders = result->directories;

    auto reconciledItems =
        m_reconcile.reconcile(files, folders, *dbFiles, *dbDirs);

    auto filesToDownload = reconciledItems.filesToDownload;
    auto filesToDeleteLocal = reconciledItems.filesToDeleteLocal;
    auto filesToMove = reconciledItems.filesToMove;
    auto dirsToMove = reconciledItems.dirsToMove;
    auto foldersToCreateLocal = reconciledItems.foldersToCreateLocal;
    auto foldersToDeleteLocal = reconciledItems.foldersToDeleteLocal;
    auto filesToUpdate = reconciledItems.filesToUpdate;
    auto filesInConflict = reconciledItems.filesInConflict;

    std::cout << "[cloudsyncworker] filesToDownload: " << filesToDownload.size()
              << "\n";
    std::cout << "[cloudsyncworker] filesToDelete: "
              << filesToDeleteLocal.size() << "\n";
    std::cout << "[cloudsyncworker] foldersToCreateLocal: "
              << foldersToCreateLocal.size() << "\n";
    std::cout << "[cloudsyncworker] foldersToDeleteLocal: "
              << foldersToDeleteLocal.size() << "\n";
    std::cout << "[cloudsyncworker] FilesToMove: " << filesToMove.size()
              << "\n";
    std::cout << "[cloudsyncworker] DirsToMove: " << dirsToMove.size() << "\n";

    std::cout << "[cloudsyncworker] filesInConflict: " << filesInConflict.size()
              << "\n";
    std::cout << "[cloudsyncworker] filesToUpdate: " << filesToUpdate.size()
              << "\n";

    updateActivityMap(reconciledItems);

    processDirsToMove(dirsToMove);

    processFoldersToDelete(foldersToDeleteLocal);

    processFilesToMove(filesToMove);

    processFilesToDelete(filesToDeleteLocal);

    processFoldersToCreate(foldersToCreateLocal);

    processFilesToDownload(filesToDownload);

    processFilesInConflict(filesInConflict);

    processFilesToUpdate(filesToUpdate);

    return true;
  } else {

    return false;
  }
}

std::string CloudSyncWorker::getCurrentTime() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return std::to_string(timestamp);
}
std::vector<std::string>
CloudSyncWorker::getPathComponents(const std::string &path) {
  if (path == "/" || path.empty()) {
    return {"/"};
  }

  std::vector<std::string> pathTree;
  std::string currentPath = "";
  std::stringstream ss(path);
  std::string token;

  while (std::getline(ss, token, '/')) {
    if (token.empty())
      continue;
    currentPath += "/" + token;
    pathTree.push_back(currentPath);
  }

  if (pathTree.empty()) {
    return {"/"};
  }
  return pathTree;
}

FileMetadata CloudSyncWorker::getFileMetadata(const CloudFileMetadata &file,
                                              const std::string &absPath) {
  FileMetadata f;
  f.filename = file.filename;
  f.path = file.path;
  f.absPath = absPath;
  f.dirID = file.dirID;
  f.inode = m_scanner.getInode(absPath);
  f.hashvalue = file.hashvalue;
  f.last_modified = file.last_modified;
  f.lastSyncedHashValue = file.lastSyncedHashValue;
  f.origin = file.origin;
  f.uuid = file.uuid;
  f.size = file.size;
  f.versions = file.versions;
  f.lastSynced = getCurrentTime();
  return f;
}

FileMetadata
CloudSyncWorker::constructFileMetadata(const FileQueueEntry &file) {
  FileMetadata f;
  f.filename = file.filename;
  f.path = file.path;
  f.absPath = file.absPath;
  f.dirID = file.dirID;
  f.inode = file.inode;
  f.hashvalue = file.hashvalue;
  f.last_modified = file.last_modified;
  f.lastSyncedHashValue = file.lastSyncedHashValue;
  f.origin = file.origin;
  f.uuid = file.uuid;
  f.size = file.size;
  f.versions = file.versions;
  f.lastSynced = getCurrentTime();
  return f;
}

DirectoryMetadata
CloudSyncWorker::getDirectoryMetadata(const std::string &path,
                                      const std::string &uuid) {
  DirectoryMetadata d;
  std::string dirAbsPath;
  dirAbsPath = path == "/" ? m_syncPath : m_syncPath + path;
  pathParts p = m_dbManager.getFolderDevice(fs::path(path));
  d.device = p.device;
  d.absPath = dirAbsPath;
  d.folder = p.folder;
  d.path = path;
  d.lastSynced = getCurrentTime();
  try {
    d.created_at = std::to_string(
        m_scanner.getUnixTimeStamp(fs::last_write_time(dirAbsPath)));

  } catch (const std::exception &e) {
    std::cerr << "[cloudsyncwoker] error fetching dir timestamp: " << e.what()
              << "\n";
    auto now = std::chrono::system_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();
    d.created_at = std::to_string(timestamp);
  }
  d.inode = m_scanner.getInode(dirAbsPath);
  d.uuid = uuid;
  return d;
}

void CloudSyncWorker::processFilesToDownload(
    const std::vector<CloudFileMetadata> &filesToDownload) {

  m_tasksPending = filesToDownload.size();
  std::cout << "[cloudsyncworker] total TASKS: " << m_tasksPending << std::endl;
  std::vector<std::future<bool>> fileDownloadTasks;

  for (const auto &file : filesToDownload) {
    // Clone the client for this task
    auto clientPtr = m_apiClient.clone();
    std::unique_ptr<CloudFilesProvider> cfProvider = nullptr;

    if (m_cfProvider) {
      cfProvider = m_cfProvider->clone();
    }
    auto downloadFunc = [this, file, cfProvider = std::move(cfProvider),
                         client = std::move(clientPtr)]() mutable {
      std::string fileAbsPath(
          file.path == "/" ? m_syncPath + "/" + file.filename
                           : m_syncPath + file.path + "/" + file.filename);
      std::string fileDirPath(
          fs::path(fileAbsPath).parent_path().generic_string());

      std::vector<std::string> paths = getPathComponents(file.path);

      auto noCfProvider = [&]() {
        m_syncWorker.addIgnoreEvent(fileAbsPath, WatchEvent::Added);
        try {
          if (!fs::exists(fileDirPath)) {
            for (auto &p : paths) {
              auto fp = p == "/" ? m_syncPath : m_syncPath + p;
              if (!fs::exists(fp)) {
                m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
              }
            }
            fs::create_directories(fileDirPath);
          }
        } catch (const std::exception &e) {
          std::cerr << "[cloudsyncworker] Unable to create Directory ->"
                    << fileDirPath << " Error: " << e.what() << "\n";
          for (auto &p : paths) {
            auto fp = p == "/" ? m_syncPath : m_syncPath + p;
            if (!fs::exists(fp)) {
              m_syncWorker.removeIgnoreEvent(fp, WatchEvent::Added);
            }
          }
          --m_tasksPending;
          std::cout << "[cloudsyncworker] tasks: " << m_tasksPending
                    << std::endl;
          if (m_tasksPending == 0) {
            std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
            m_tasksCV.notify_one();
          }
          return false;
        }

        return client->downloadFile(
            file, fileAbsPath, [this](const std::string &key, double progress) {
              auto it = m_activityStore.getActivity(key);
              if (it.has_value()) {
                it->progress = progress;
                it->isActive = true;
                it->inQueue = false;
                it->isDone = progress >= 100.0;
                it->lastUpdated = std::to_string(Utility::getCurrentTime());
                //                m_dbManager.insertActivity(it.value());
                updateActivity(key, it.value());
              }
            });
      };

      bool downloadStatus = false;
#ifdef _WIN32
      if (cfProvider) {
        auto it = m_activityStore.getActivity(file.uuid);
        it->progress = 0.0;
        it->isActive = true;
        it->inQueue = false;
        it->isDone = false;
        m_dbManager.insertActivity(it.value());
        updateActivity(file.uuid, it.value());

        downloadStatus = cfProvider->createFilePlaceholder(file, paths);

        it->progress = 100.0;
        it->isDone = true;
        it->lastUpdated = std::to_string(Utility::getCurrentTime());
        m_dbManager.insertActivity(it.value());
        updateActivity(file.uuid, it.value());
      }
      if (!cfProvider) {
        downloadStatus = noCfProvider();
      }
#endif
#ifndef _WIN32
      // 3. Download WITHOUT the database lock using CLONED client
      downloadStatus = noCfProvider();
#endif
      // 4. Update Database WITH the lock
      pathParts fp = m_dbManager.getFolderDevice(fs::path(file.path));

      if (downloadStatus) {

        std::vector<DirectoryMetadata> dirs;
        FileMetadata f(getFileMetadata(file, fileAbsPath));
        f.lastSynced = std::to_string(Utility::getCurrentTime());
        f.isCloudOnly = true;
        std::optional<DirectoryMetadata> dirExists{std::nullopt};
        {
          std::lock_guard<std::recursive_mutex> lock(
              m_dbManager.getSyncMutex());
          dirExists =
              m_dbManager.getDirectoryByPath(fp.device, fp.folder, file.path);
        }

        if (!dirExists.has_value()) {
          std::vector<std::string> paths = getPathComponents(file.path);
          for (auto &path : paths) {
            std::string uuid = "";
            if (file.dirIDs && file.dirIDs->count(path)) {
              uuid = file.dirIDs->at(path);
            } else if (path == file.path) {
              uuid = file.dirID;
            }
            if (!uuid.empty()) {
              auto d = getDirectoryMetadata(path, uuid);
              d.lastSynced = std::to_string(Utility::getCurrentTime());
              dirs.push_back(d);
            }
          }
        } else {
          auto d = getDirectoryMetadata(file.path, file.dirID);
          d.lastSynced = std::to_string(Utility::getCurrentTime());
          dirs.push_back(d);
        }

        bool fileUpdateStatus = false;
        {
          std::lock_guard<std::recursive_mutex> lock(
              m_dbManager.getSyncMutex());
          fileUpdateStatus = m_dbManager.insertFileWithDirectory(f, dirs);
        }

        if (fileUpdateStatus) {
          --m_tasksPending;
          std::cout << "[cloudsyncworker] tasks: " << m_tasksPending
                    << std::endl;
          if (m_tasksPending == 0) {
            std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
            m_tasksCV.notify_one();
          }
          return true;
        } else {
          --m_tasksPending;
          std::cout << "[cloudsyncworker] tasks: " << m_tasksPending
                    << std::endl;
          if (m_tasksPending == 0) {
            std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
            m_tasksCV.notify_one();
          }
          return false;
        }
      } else {
        auto syncItem = m_activityStore.getActivity(file.uuid);
        syncItem->isActive = false;
        syncItem->isError = true;
        m_dbManager.insertActivity(syncItem.value());
        updateActivity(file.uuid, syncItem.value());
        std::cout << "[cloudsyncworker] download failed.. deleting.."
                  << fileAbsPath << std::endl;
        if (fs::exists(fileAbsPath)) {
          try {
            m_syncWorker.addIgnoreEvent(fileAbsPath, WatchEvent::Deleted);
            fs::remove(fileAbsPath);
          } catch (const std::exception &e) {
            m_syncWorker.removeIgnoreEvent(fileAbsPath, WatchEvent::Deleted);
            std::cerr << "[cloudsyncworker] file deletion failed >>" << e.what()
                      << "\n";
          }
        }
        --m_tasksPending;
        std::cout << "[cloudsyncworker] tasks: " << m_tasksPending << std::endl;
        if (m_tasksPending == 0) {
          std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
          m_tasksCV.notify_one();
        }
        return false;
      }
    };
    m_downloadThreadPool.enqueue(std::move(downloadFunc));
  }
}

void CloudSyncWorker::processFilesToDelete(
    const std::vector<FileMetadata> &filesToDeleteLocal) {
  for (auto &file : filesToDeleteLocal) {
    auto syncItem = m_activityStore.getActivity(file.uuid);
    syncItem->isActive = true;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(file.uuid, syncItem.value());

    try {
      m_syncWorker.addIgnoreEvent(file.absPath, WatchEvent::Deleted);
      fs::remove(file.absPath);
      {
        std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
        bool result = m_dbManager.deleteFileByPath(file.path, file.filename);
        if (result) {
          syncItem->isActive = false;
          syncItem->isDone = true;
          syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
          m_dbManager.insertActivity(syncItem.value());
          updateActivity(file.uuid, syncItem.value());
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] Exception:" << e.what()
                << " in deleting file->" << file.absPath << "\n";
      syncItem->isActive = false;
      syncItem->isError = true;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());
    }
  }
}

void CloudSyncWorker::processFoldersToCreate(
    const std::vector<LocalFolderCreateMetadata> &foldersToCreateLocal) {
  for (auto &folder : foldersToCreateLocal) {

    auto syncItem = m_activityStore.getActivity(folder.uuid);
    syncItem->isActive = true;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(folder.uuid, syncItem.value());

    auto paths = getPathComponents(folder.path);
    if (m_cfProvider) {
      std::string absPath;
      absPath = folder.path == "/" ? m_syncPath : m_syncPath + folder.path;
      m_cfProvider->createDirsPlaceholder(absPath, folder.dirIDs.value(),
                                          paths);
    }
    if (!m_cfProvider) {
      try {
        if (!fs::exists(folder.absPath)) {
          std::cout << "[cloudsyncworker] creating path ..." << folder.absPath
                    << "\n";
          for (auto &p : paths) {
            auto fp = p == "/" ? m_syncPath : m_syncPath + p;
            if (!fs::exists(fp)) {
              m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
            }
          }
          fs::create_directories(folder.absPath);
        }
      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] unable to create path" << folder.absPath
                  << " | " << e.what() << "\n";
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 1. Register ignore intent
            m_syncWorker.removeIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        syncItem->isActive = false;
        syncItem->isError = true;
        m_dbManager.insertActivity(syncItem.value());
        updateActivity(folder.uuid, syncItem.value());
        continue;
      }
    }

    // 2. Database operation (locked)
    auto folderPaths = getPathComponents(folder.path);
    std::vector<DirectoryMetadata> dirs;
    for (auto &path : folderPaths) {
      std::string uuid = "";

      if (folder.dirIDs && folder.dirIDs->count(path)) {
        uuid = folder.dirIDs->at(path);
      } else if (path == folder.path) {
        uuid = folder.uuid;
      }

      if (!uuid.empty()) {
        auto d = getDirectoryMetadata(path, uuid);
        d.lastSynced = std::to_string(Utility::getCurrentTime());
        dirs.push_back(d);
      }
    }

    bool result = false;

    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      result = m_dbManager.createDirectoryPaths(dirs);
    }
    if (!result) {
      syncItem->isActive = false;
      syncItem->isError = true;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(folder.uuid, syncItem.value());

      try {
        std::cout << "[cloudsyncworker] dbcreation failed reverting... "
                  << "\n";
        m_syncWorker.addIgnoreEvent(folder.absPath, WatchEvent::Deleted);
        fs::remove(folder.absPath);

      } catch (const std::exception &e) {
        m_syncWorker.removeIgnoreEvent(folder.absPath, WatchEvent::Deleted);
        std::cerr << "[cloudsyncworker] " << e.what() << "\n";
      }
    } else {
      syncItem->isActive = false;
      syncItem->isDone = true;
      syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(folder.uuid, syncItem.value());
    }
  }
}

void CloudSyncWorker::processFoldersToDelete(
    const std::vector<LocalFolderDeleteMetadata> &foldersToDeleteLocal) {
  for (auto &folder : foldersToDeleteLocal) {
    auto syncItem = m_activityStore.getActivity(folder.uuid);
    syncItem->isActive = true;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(folder.uuid, syncItem.value());

    // 1. Tell SyncWorker to ignore
    m_syncWorker.addIgnoreEvent(folder.absPath, WatchEvent::Deleted);
    try {
      if (fs::exists(folder.absPath)) {
        fs::remove_all(folder.absPath);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] exception: " << e.what()
                << " | unable to delete folder: " << folder.path << "\n";
      m_syncWorker.removeIgnoreEvent(folder.absPath, WatchEvent::Deleted);
      syncItem->isActive = false;
      syncItem->isError = true;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(folder.uuid, syncItem.value());
      continue;
    }
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      bool result = m_dbManager.deleteDirectory(folder.path);
      if (result) {
        syncItem->isActive = false;
        syncItem->isDone = true;
        syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
        m_dbManager.insertActivity(syncItem.value());
        updateActivity(folder.uuid, syncItem.value());
      } else {
        syncItem->isActive = false;
        syncItem->isError = true;
        m_dbManager.insertActivity(syncItem.value());
        updateActivity(folder.uuid, syncItem.value());
      }
    }
  }
}
void CloudSyncWorker::processDirsToMove(
    const std::vector<DirectoryQueueEntry> &dirsToMove) {

  for (auto &d : dirsToMove) {
    std::string oldAbsPath;
    std::string newAbsPath;
    oldAbsPath = *d.old_path == "/" ? m_syncPath : m_syncPath + *d.old_path;
    newAbsPath = d.path == "/" ? m_syncPath : m_syncPath + d.path;
    std::string oldParent = fs::path(oldAbsPath).parent_path().generic_string();
    std::string newParent = fs::path(newAbsPath).parent_path().generic_string();
    bool isRenamed = false;

    if (oldParent == newParent) {
      isRenamed = true;
      std::cout << "[cloudsyncworker] dir isRenamed: " << isRenamed << " | "
                << newAbsPath << std::endl;
    }

    auto syncItem = m_activityStore.getActivity(d.uuid);
    syncItem->isActive = true;
    syncItem->inQueue = false;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(d.uuid, syncItem.value());
    try {

      if (fs::exists(oldAbsPath)) {
        if (isRenamed) {
          m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Renamed);
        } else {
          m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        }
        fs::rename(oldAbsPath, newAbsPath);
      }

    } catch (const std::exception &e) {
      if (isRenamed) {
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Renamed);
      } else {
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
      }
      std::cout << "[cloudsyncworker] move failed: " << e.what() << std::endl;
      std::cout << "[cloudsyncworker] newPath : " << d.path
                << " |  oldPath : " << *d.old_path << std::endl;
      syncItem->isActive = false;
      syncItem->inQueue = false;
      syncItem->isError = true;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(d.uuid, syncItem.value());
      continue;
    }
    bool isMoved;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      isMoved = m_dbManager.moveDirectory(d.path, *d.old_path, d, false);
    }
    if (isMoved) {
      std::cout << "[cloudsyncwoker] MOVE SUCCESSFULL :" << std::endl;
      std::cout << "[cloudsyncwoker] OLD: " << *d.old_path
                << " | NEW: " << d.path << std::endl;
      syncItem->isActive = false;
      syncItem->isDone = true;
      syncItem->inQueue = false;
      syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(d.uuid, syncItem.value());
      if (m_cfProvider) {
        std::wstring absPathW = fs::path(newAbsPath).make_preferred().wstring();
        bool isMarkedInSync = m_cfProvider->markInSync(absPathW);
        if (isMarkedInSync) {
          std::cout << "[cloudsyncwoker] MARKED IN SYNC  :" << std::endl;
        }
      }
    } else {
      std::cout << "[cloudsyncwoker] MOVE FAILED :" << std::endl;
      std::cout << "[cloudsyncwoker] OLD: " << *d.old_path
                << " | NEW: " << d.path << std::endl;
    }
  }
}

void CloudSyncWorker::processFilesToMove(
    const std::vector<FileQueueEntry> &filesToMove) {
  for (auto &file : filesToMove) {
    auto syncItem = m_activityStore.getActivity(file.uuid);
    syncItem->isActive = true;
    syncItem->inQueue = false;
    syncItem->isError = false;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(file.uuid, syncItem.value());

    std::string oldRelPath{*file.old_path};
    std::string newRelPath{file.path};
    std::string oldAbsPath{file.old_path == "/"
                               ? m_syncPath + "/" + *file.old_filename
                               : m_syncPath + *file.old_path + "/" +
                                     *file.old_filename};
    std::string newAbsPath{file.absPath};

    newAbsPath = file.path == "/"
                     ? m_syncPath + "/" + file.filename
                     : m_syncPath + file.path + "/" + file.filename;
    std::string oldParent = fs::path(oldAbsPath).parent_path().generic_string();
    std::string newParent = fs::path(newAbsPath).parent_path().generic_string();
    bool isRenamed = false;

    if (oldParent == newParent) {
      isRenamed = true;
      std::cout << "[cloudsyncworker] file isRenamed: " << isRenamed << " | "
                << newAbsPath << std::endl;
    }
    try {
      if (fs::exists(oldAbsPath)) {
        if (!isRenamed) {
          m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        }
        if (isRenamed) {
          m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Renamed);
        }
        fs::rename(oldAbsPath, newAbsPath);
      }
      syncItem->isDone = true;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());

    } catch (...) {
      std::cerr << "[cloudsyncworker] unable to rename from ->"
                << *file.old_filename << " to->" << file.filename << "\n";
      if (!isRenamed) {
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
      }
      if (isRenamed) {
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Renamed);
      }
      syncItem->isError = true;
      syncItem->isActive = false;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());
      continue;
    }
    std::string path = *file.old_path;
    std::string filename = *file.old_filename;
    FileMetadata f(Utility::convert<FileMetadata>(file));
    f.lastSynced = std::to_string(Utility::getCurrentTime());
    bool result = false;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      result = m_dbManager.updateFileWithTransaction(f, path, filename);
    }
    if (!result) {
      try {
        std::cout << "[cloudsyncworker] file update failed in DB. Renaming "
                     "the file back to its origin name"
                  << "\n";
        if (fs::exists(newAbsPath)) {
          if (!isRenamed) {
            m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Moved);
            // m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Moved);
          }
          if (isRenamed) {
            m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Renamed);
          }
          fs::rename(newAbsPath, oldAbsPath);
        }
      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] unable to rename" << e.what()
                  << oldAbsPath << "\n";
        if (!isRenamed) {
          m_syncWorker.removeIgnoreEvent(oldAbsPath, WatchEvent::Moved);
          // m_syncWorker.removeIgnoreEvent(oldAbsPath, WatchEvent::Moved);
        }
        if (isRenamed) {
          m_syncWorker.removeIgnoreEvent(oldAbsPath, WatchEvent::Renamed);
        }
      }
      syncItem->isError = true;
      syncItem->isActive = false;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());
    } else {
      syncItem->isDone = true;
      syncItem->isActive = false;
      syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());
      if (m_cfProvider) {
        std::wstring absPathW = fs::path(newAbsPath).make_preferred().wstring();
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Modified);
        bool isMarkedInSync = m_cfProvider->markInSync(absPathW);
        if (isMarkedInSync) {
          std::cout << "[cloudsyncwoker] FILE MARKED IN SYNC : " << std::endl;
        }
      }
    }
  }
}

void CloudSyncWorker::processFilesInConflict(
    const std::vector<CloudFileMetadata> &filesInConflict) {
  for (auto &file : filesInConflict) {
    // rename the local file
    // download the file from the cloud
    // add events to ignore list to prevent adding it into the queue.
    // let the renamed file be added as new file in the queue so it can be
    // uploaded to cloud later.
    // 1. creating conflict name for the file
    auto timestamp = getCurrentTime();
    std::string absPath = file.path == "/"
                              ? m_syncPath + "/" + file.filename
                              : m_syncPath + file.path + "/" + file.filename;
    fs::path p(absPath);
    std::string ext = p.extension().string();
    std::string stem = p.stem().string();
    std::string conflictName = stem + "( conflict copy ) - " + timestamp + ext;
    std::string newAbsPath = file.path == "/"
                                 ? m_syncPath + "/" + conflictName
                                 : m_syncPath + file.path + "/" + conflictName;
    std::string dirPath = fs::path(absPath).parent_path().string();
    std::vector<std::string> paths = getPathComponents(dirPath);
    if (fs::exists(absPath)) {
      // 2. ignoring the events created for file rename
      try {
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        fs::rename(absPath, newAbsPath);

      } catch (...) {
        std::cerr << "[cloudsyncworker] unable to rename from " << absPath
                  << "-> to ->" << newAbsPath << std::endl;
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
        continue;
      }
    }
    // 3.add the conflicted file into the file table ( main & queue)
    if (!fs::exists(dirPath)) {
      try {
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 4. ignore the folder creation events that would be triggered
            // while creating directory
            m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        fs::create_directories(dirPath);

      } catch (...) {
        std::cout << "[cloudsyncworker] unable to create directory " << dirPath
                  << std::endl;
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 4. ignore the folder creation events that would be triggered
            // while creating directory
            m_syncWorker.removeIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        continue;
      }
    }
    // 5. add an event to ignore the file that is being downloaded
    m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Added);
    auto result = m_apiClient.downloadFile(
        file, absPath, [this](const std::string &key, double progress) {
          auto it = m_activityStore.getActivity(key);
          if (it.has_value()) {
            it->progress = progress;
            it->isActive = true;
            it->inQueue = false;
            it->isDone = progress >= 100.0;
            //            m_dbManager.insertActivity(it.value());
            updateActivity(key, it.value());
          }
        });
    if (!result) {
      if (fs::exists(absPath)) {
        try {
          // 6. if the file download fails, remove its state entry in FS &
          // ignore the delete event
          m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
          fs::remove(absPath);

        } catch (...) {
          std::cerr << "[cloudsyncworker] unable to delete the file that "
                       "failed to download->"
                    << absPath << std::endl;
          m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
        }
      }
      continue;
    }
    // 7. lock the syncworker until the DB is updated for the file addition
    std::vector<std::string> paths_ = getPathComponents(file.path);
    std::vector<DirectoryMetadata> dirs;
    // 8. getting the folder  components for the file path that needs to be
    // inserted into Directory DB.
    for (auto &path : paths_) {
      DirectoryMetadata d;
      std::string uuid = "";
      if (file.dirIDs && file.dirIDs->count(path)) {
        uuid = file.dirIDs->at(path);
      } else if (path == file.path) {
        uuid = file.dirID;
      }

      if (!uuid.empty()) {
        d = getDirectoryMetadata(path, uuid);
        dirs.push_back(d);
      }
    }

    FileMetadata cloudFile(getFileMetadata(file, absPath));
    FileMetadata conflictedFile(getFileMetadata(file, newAbsPath));

    conflictedFile.filename = conflictName;
    conflictedFile.versions = 1;
    conflictedFile.hashvalue = m_scanner.calculateHash(newAbsPath);
    conflictedFile.lastSyncedHashValue = conflictedFile.hashvalue;
    conflictedFile.inode = m_scanner.getInode(newAbsPath);
    conflictedFile.size = fs::file_size(newAbsPath);
    conflictedFile.lastSynced = getCurrentTime();
    conflictedFile.last_modified = std::to_string(
        m_scanner.getUnixTimeStamp(fs::last_write_time(newAbsPath)));
    conflictedFile.origin = conflictedFile.uuid = UuidUtils::generate();
    DirectoryMetadata d = getDirectoryMetadata(file.path, file.dirID);
    DirectoryQueueEntry conflictedDirQueue{
        Utility::constructDirectoryQueueEntry(d)};

    FileQueueEntry fq;
    fq = Utility::constructFileQueueEntry(conflictedFile, SyncStatus::NEW);
    conflictedDirQueue.sync_status =
        syncStatusToString(SyncStatus::FILE_LINKED);
    // 9. update the DB for the file that was downloaded
    bool result_ = false;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      result_ = m_dbManager.insertFileAndQueueWithDirectory(
          cloudFile, conflictedFile, fq, conflictedDirQueue, dirs);
    }
    if (!result_) {
      try {
        // 10. if the DB update fails, remove the file that was downloaded
        // to ensure integrity
        m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Deleted);
        if (fs::exists(absPath)) {
          // 11. ignore the event for the file removed from FS.
          fs::remove(absPath);
        }
        if (fs::exists(newAbsPath)) {
          fs::remove(newAbsPath);
        }
      } catch (...) {
        m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Deleted);
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Deleted);
      }
      continue;
    }
  }
}

void CloudSyncWorker::processFilesToUpdate(
    const std::vector<CloudFileMetadata> &filesToUpdate) {
  for (auto &file : filesToUpdate) {
    auto syncItem = m_activityStore.getActivity(file.uuid);
    syncItem->isActive = true;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(file.uuid, syncItem.value());

    std::string absPath = file.path == "/"
                              ? m_syncPath + file.filename
                              : m_syncPath + file.path + "/" + file.filename;
    m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);
    bool result = m_apiClient.downloadFile(
        file, absPath, [this](const std::string &key, double progress) {
          auto it = m_activityStore.getActivity(key);
          if (it.has_value()) {
            it->progress = progress;
            it->isActive = true;
            it->inQueue = false;
            it->isDone = progress >= 100.0;
            //            m_dbManager.insertActivity(it.value());
            updateActivity(key, it.value());
          }
        });
    if (result) {
      syncItem->isActive = false;
      syncItem->isDone = true;
      syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());

      FileMetadata f(getFileMetadata(file, absPath));
      f.lastSynced = std::to_string(Utility::getCurrentTime());
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      bool isUpdated =
          m_dbManager.updateFileWithTransaction(f, file.path, file.filename);
      if (!isUpdated) {
        // if we are here it means the file update on FS is success! and only
        // the db update failed retry updating the DB again
        continue;
      }
    } else {
      syncItem->isActive = false;
      syncItem->isError = true;
      m_dbManager.insertActivity(syncItem.value());
      updateActivity(file.uuid, syncItem.value());
    }
  }
}

std::optional<bool>
CloudSyncWorker::deleteFolderInCloud(DirectoryQueueEntry &dq) {
  // delete folder
  auto syncItem = m_activityStore.getActivity(dq.uuid);
  syncItem->isActive = true;
  syncItem->inQueue = false;
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(dq.uuid, syncItem.value());
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto d = m_dbManager.getDirectoryByPath(dq.device, dq.folder, dq.path);
    if (d.has_value()) {
      std::cout << "[cloudsyncwoker] Dir To delete exists locally:" << dq.path
                << " \n";
      return std::nullopt;
    }
  }
  bool isCreated = m_apiClient.deleteFolder(dq);

  if (!isCreated) {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushDirEntry(dq);
    std::cout << "[cloudsyncworker] Error deleting folder -> " << dq.path
              << " in cloud" << "\n";
    syncItem->isActive = false;
    syncItem->isDone = false;
    syncItem->isError = true;
    syncItem->inQueue = false;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(dq.uuid, syncItem.value());
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto isQRemoved =
        m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
    if (!isQRemoved) {
      std::cout << "[cloudsyncwoker] DirQueue delete failed" << dq.path
                << " \n";
      return std::nullopt;
    }
  }

  syncItem->isActive = false;
  syncItem->isDone = true;
  syncItem->isError = false;
  syncItem->inQueue = false;
  syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(dq.uuid, syncItem.value());
  auto item = buildActivityMetadata(syncItem.value(), m_syncPath, "Deleted");
  std::cout << "[cloudsyncworker] folder -> " << dq.path << " deleted in cloud"
            << "\n";
  return true;
}

std::optional<bool> CloudSyncWorker::moveFolderInCloud(DirectoryQueueEntry &dq,
                                                       bool isRename) {
  auto syncItem = m_activityStore.getActivity(dq.uuid);
  syncItem->isActive = true;
  syncItem->inQueue = false;
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(dq.uuid, syncItem.value());

  bool isRenamed = m_apiClient.moveFolder(dq, isRename);

  if (!isRenamed) {
    std::cout << "[cloudsyncworker] Error renaming -> " << *dq.old_path
              << " => " << dq.path << " in cloud" << "\n";
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushDirEntry(dq);

    syncItem->isActive = false;
    syncItem->isDone = false;
    syncItem->isError = true;
    syncItem->inQueue = false;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(dq.uuid, syncItem.value());

    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto isQRemoved =
        m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
    if (!isQRemoved) {
      std::cout << "[cloudsyncworker] DirQueue delete failed:  " << *dq.old_path
                << std::endl;
      return std::nullopt;
    }
  }

  syncItem->isActive = false;
  syncItem->isDone = true;
  syncItem->isError = false;
  syncItem->inQueue = false;
  syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(dq.uuid, syncItem.value());
  std::cout << "[cloudsyncworker] folder =>" << *dq.old_path
            << " renamed in cloud to =>" << dq.path << "\n";
  return true;
}

std::optional<bool>
CloudSyncWorker::createFolderInCloud(DirectoryQueueEntry &dq) {
  auto syncItem = m_activityStore.getActivity(dq.uuid);
  syncItem->isActive = true;
  syncItem->inQueue = false;
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(dq.uuid, syncItem.value());
  // create folder
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto d = m_dbManager.getDirectoryByPath(dq.device, dq.folder, dq.path);
    if (!d.has_value()) {
      std::cout << "[cloudsyncworker] Dir: " << dq.path
                << " not found in Directory Table \n";
      return std::nullopt;
    }
  }

  bool isCreated = m_apiClient.createFolder(dq);

  if (!isCreated) {
    std::cout << "[cloudsyncworker] Error creating folder -> " << dq.path
              << " in cloud" << "\n";
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushDirEntry(dq);
    syncItem->isActive = false;
    syncItem->isDone = false;
    syncItem->isError = true;
    syncItem->inQueue = false;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(dq.uuid, syncItem.value());
    return false;
  }

  if (m_cfProvider) {
    m_cfProvider->convertToPlaceholder(dq);
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto isQRemoved =
        m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
    if (!isQRemoved) {
      std::cerr << "[cloudsyncworker] DirQueue delete failed \n";
      return std::nullopt;
    }
  }

  syncItem->isActive = false;
  syncItem->isDone = true;
  syncItem->inQueue = false;
  syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(dq.uuid, syncItem.value());

  std::cout << "[cloudsyncworker] folder -> " << dq.path
            << " created in cloud \n";
  return true;
}

std::optional<bool> CloudSyncWorker::uploadFile(ApiClient &client,
                                                CloudFilesProvider &cfProvider,
                                                const FileQueueEntry &fq) {
  using DirTree = std::optional<std::vector<DirectoryMetadata>>;
  DirTree dirTree = getFileDirTree(fq);

  FileMetadata f = constructFileMetadata(fq);

  bool isUploaded = client.uploadFile(
      fq, dirTree.value(), false,
      [this](const std::string &key, double progress) {
        auto it = m_activityStore.getActivity(key);
        if (it.has_value()) {
          progress = std::clamp(progress, 0.0, 100.0);

          int oldPercent = static_cast<int>(std::floor(it->progress));
          int newPercent = static_cast<int>(std::floor(progress));

          if (newPercent > oldPercent || progress >= 100.0) {
            it->progress = progress;
            it->isActive = true;
            it->inQueue = false;
            //            m_dbManager.insertActivity(it.value());
            updateActivity(key, it.value());
          } else {
            it->progress = progress; // keep internal state current
            it->isActive = true;
            it->inQueue = false;
          }
        }
      });

  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] file upload failed: " << fq.filename
              << "\n";
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushFileEntry(fq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    std::wstring absPathW = cfProvider.toWide(fq.absPath);
    cfProvider.dehydrateFile(absPathW, fq.uuid);

    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  auto item = m_activityStore.getActivity(f.uuid);
  if (item.has_value()) {
    item->lastUpdated = std::to_string(Utility::getCurrentTime());
    item->isDone = true;
    item->isActive = false;
    item->progress = 100.00;
    m_dbManager.insertActivity(item.value());
    updateActivity(f.uuid, item.value());
  }

  if (!isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed : " << fq.filename
              << "\n";
    return std::nullopt;
  }

  std::cout << "[cloudsyncworker] file->" << fq.filename << " uploaded to cloud"
            << "\n";

  return true;
}

std::optional<bool>
CloudSyncWorker::uploadModifiedFile(ApiClient &client,
                                    CloudFilesProvider &cfProvider,
                                    const FileQueueEntry &fq) {
  // update file
  bool isUploaded = client.uploadFile(
      fq, {}, true, [this](const std::string &key, double progress) {
        auto it = m_activityStore.getActivity(key);
        if (it.has_value()) {
          progress = std::clamp(progress, 0.0, 100.0);

          int oldPercent = static_cast<int>(std::floor(it->progress));
          int newPercent = static_cast<int>(std::floor(progress));

          if (newPercent > oldPercent || progress >= 100.0) {
            it->progress = progress;
            it->isActive = true;
            it->inQueue = false;
            // it->isDone = progress >= 100.0;
            //            m_dbManager.insertActivity(it.value());
            updateActivity(key, it.value());
          } else {
            it->progress = progress; // keep internal state current
            it->isActive = true;
            it->inQueue = false;
            // it->isDone = progress >= 100.0;
          }
        }
      });

  FileMetadata f = constructFileMetadata(fq);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] unable to update the file: " << fq.absPath
              << "\n";
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushFileEntry(fq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    std::wstring absPathW(fq.absPath.begin(), fq.absPath.end());
    cfProvider.markInSync(absPathW);
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (!isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed : " << fq.filename
              << "\n";
    return std::nullopt;
  }

  auto item = m_activityStore.getActivity(f.uuid);
  if (item.has_value()) {
    item->lastUpdated = std::to_string(Utility::getCurrentTime());
    item->isDone = true;
    item->isActive = false;
    item->progress = 100.00;
    m_dbManager.insertActivity(item.value());
    updateActivity(f.uuid, item.value());
  }
  std::cout << "[cloudsyncworker] file->" << fq.filename << " uploaded to cloud"
            << "\n";
  return true;
}

std::optional<bool> CloudSyncWorker::deleteFile(ApiClient &client,
                                                const FileQueueEntry &fq) {
  auto syncItem = m_activityStore.getActivity(fq.uuid);
  syncItem->isActive = true;
  syncItem->inQueue = false;
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(fq.uuid, syncItem.value());
  bool isUploaded = client.deleteFile(fq);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] unable to delete file in cloud: "
              << fq.absPath << "\n";
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushFileEntry(fq);
    syncItem->isActive = false;
    syncItem->inQueue = false;
    syncItem->isDone = false;
    syncItem->isError = true;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(fq.uuid, syncItem.value());
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (!isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed : " << fq.absPath
              << "\n";
    return std::nullopt;
  }
  syncItem->isActive = false;
  syncItem->isDone = true;
  syncItem->inQueue = false;
  syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(fq.uuid, syncItem.value());

  std::cout << "[cloudsyncworker] file->" << fq.absPath
            << " deleted from  cloud" << "\n";

  return true;
}

std::optional<std::vector<DirectoryMetadata>>

CloudSyncWorker::getFileDirTree(const FileQueueEntry &fq) {

  std::vector<DirectoryMetadata> dirTree;

  std::vector<std::string> pathComponents = getPathComponents(fq.path);

  std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());

  for (auto &path : pathComponents) {
    pathParts p = m_dbManager.getFolderDevice(fs::path(path));
    std::optional<DirectoryMetadata> dir{std::nullopt};
    dir = m_dbManager.getDirectoryByPath(p.device, p.folder, path);
    if (dir.has_value()) {
      dirTree.push_back(*dir);
    } else {
      return std::nullopt;
    }
  }
  return dirTree;
}

std::optional<bool> CloudSyncWorker::renameFile(ApiClient &client,
                                                CloudFilesProvider &cfProvider,
                                                const FileQueueEntry &fq) {
  auto syncItem = m_activityStore.getActivity(fq.uuid);
  syncItem->isActive = true;
  syncItem->inQueue = false;
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(fq.uuid, syncItem.value());

  auto dirTree = getFileDirTree(fq);

  bool isUploaded = client.renameFile(fq, dirTree.value());
  FileMetadata f = constructFileMetadata(fq);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] unable to rename file->" << *fq.old_filename
              << " to ->" << fq.filename << "\n";
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    m_syncWorker.pushFileEntry(fq);
    syncItem->isActive = false;
    syncItem->isError = true;
    syncItem->isDone = false;
    syncItem->inQueue = false;
    m_dbManager.insertActivity(syncItem.value());
    updateActivity(fq.uuid, syncItem.value());
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    std::wstring absPathNormalizedW =
        fs::path(fq.absPath).make_preferred().wstring();
    bool isMarkedInSync = cfProvider.markInSync(absPathNormalizedW);
    m_syncWorker.addIgnoreEvent(fq.absPath, WatchEvent::Modified);
    if (isMarkedInSync) {
      std::cout << "[cloudsyncworker] marked IN SYNC: " << fq.absPath
                << std::endl;
    }
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (!isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed : "
              << *fq.old_filename << " to ->" << fq.filename << "\n";
    return std::nullopt;
  }
  syncItem->isActive = false;
  syncItem->isError = false;
  syncItem->isDone = true;
  syncItem->inQueue = false;
  syncItem->lastUpdated = std::to_string(Utility::getCurrentTime());
  m_dbManager.insertActivity(syncItem.value());
  updateActivity(fq.uuid, syncItem.value());

  std::cout << "[cloudsyncworker] file->" << *fq.old_filename << " renamed to->"
            << fq.filename << " in cloud "
            << "\n";
  return true;
}

void CloudSyncWorker::processQueueToSyncUp() {
  while (!m_syncWorker.fileIsEmpty() || !m_syncWorker.dirIsEmpty()) {
    std::optional<DirectoryQueueEntry> dq = m_syncWorker.popNextDirEntry();

    if (dq.has_value()) {
      if (dq->sync_status == syncStatusToString(SyncStatus::DELETE)) {
        deleteFolderInCloud(*dq);
      }
      if (dq->sync_status == syncStatusToString(SyncStatus::RENAME)) {
        moveFolderInCloud(*dq, true);
      }
      if (dq->sync_status == syncStatusToString(SyncStatus::MOVED)) {
        moveFolderInCloud(*dq, false);
      }
      if (dq->sync_status == syncStatusToString(SyncStatus::NEW)) {
        createFolderInCloud(*dq);
      }
      if (dq->sync_status == syncStatusToString(SyncStatus::FILE_LINKED)) {
      }
    }

    std::optional<FileQueueEntry> fq = m_syncWorker.popNextFileEntry();

    if (fq.has_value()) {
      auto uploadFunc = [this, fq]() mutable {
        auto client = this->m_apiClient.clone();
        auto cfProvider = this->m_cfProvider->clone();
        if (fq->sync_status == syncStatusToString(SyncStatus::DELETE)) {
          deleteFile(*client, *fq);
        }
        if (fq->sync_status == syncStatusToString(SyncStatus::MOVED)) {
          renameFile(*client, *cfProvider, *fq);
        }
        if (fq->sync_status == syncStatusToString(SyncStatus::RENAME)) {
          renameFile(*client, *cfProvider, *fq);
        }
        if (fq->sync_status == syncStatusToString(SyncStatus::NEW)) {
          uploadFile(*client, *cfProvider, *fq);
        }
        if (fq->sync_status == syncStatusToString(SyncStatus::MODIFIED)) {
          uploadModifiedFile(*client, *cfProvider, *fq);
        }
      };
      m_uploadThreadPool.enqueue(uploadFunc);
    }
  }
};

void CloudSyncWorker::initActivityAndPriorityQ() {

  std::optional<std::vector<FileQueueEntry>> qFiles;

  std::optional<std::vector<DirectoryQueueEntry>> qDirs;

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    qFiles = m_dbManager.getAllQueueFiles();
    qDirs = m_dbManager.getAllQueueDirectories();
  }

  for (auto &fq : qFiles.value()) {
    auto activity = Utility::resolveActivityStatus<FileQueueEntry>(fq);
    if (activity.has_value()) {
      auto syncItem =
          Utility::convertToActivity<FileQueueEntry>(fq, activity.value());
      addActivity(syncItem.id, syncItem);
    }
    m_syncWorker.pushFileEntry(fq);
  }

  for (auto &dq : qDirs.value()) {
    auto activity = Utility::resolveActivityStatus<DirectoryQueueEntry>(dq);
    if (activity.has_value()) {
      auto syncItem =
          Utility::convertToActivity<DirectoryQueueEntry>(dq, activity.value());
      addActivity(syncItem.id, syncItem);
    }
    m_syncWorker.pushDirEntry(dq);
  }
}

void CloudSyncWorker::controlThread() {

  /*TODO
      1. create the UUID Map of the synced DB
      2. create the UUID map of the CloudFileMetadata & cloudFolderMetadata
      3. compute the differences between the cloud & synced state
      4. create the UUID map for the local DB
      5.  compute the difference between the local & synced state
      6. compute the differences between the changes computed to identify
          conflicts
      7. delegate operations - upload / download give priority to cloud
     move / rename / delete followed by local move / rename / delete
  */
  std::optional<CloudMetadataResult> result{std::nullopt};
  std::optional<std::vector<FileMetadata>> dbFiles{std::nullopt};
  std::optional<std::vector<SyncedMetadata>> syncedItems{std::nullopt};
  std::optional<std::vector<DirectoryMetadata>> dbDirs{std::nullopt};
  std::optional<std::vector<FileQueueEntry>> dbQFiles{std::nullopt};
  std::optional<std::vector<DirectoryQueueEntry>> dbQDirs{std::nullopt};
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    result = m_apiClient.getMetadata();
    syncedItems = m_dbManager.getAllSyncedItems();
    dbFiles = m_dbManager.getAllFiles();
    dbDirs = m_dbManager.getAllDirectories();
  }
  if (result->success) {
    auto cloudFiles = result->files;
    auto cloudDirs = result->directories;

    std::map<std::string, CloudFileMetadata> cloudFileByUuid;
    std::map<std::string, CloudFolderMetadata> cloudDirByUuid;
    std::map<std::string, CloudFolderMetadata> cloudDirByPath;
    std::map<std::string, FileMetadata> localFileByUuid;
    std::map<std::string, DirectoryMetadata> localDirByUuid;
    std::map<std::string, SyncedMetadata> syncedItemsByOrigin;

    ReconciliationResult result;

    for (const auto &cf : cloudFiles) {
      cloudFileByUuid[cf.uuid] = cf;
    }
    for (const auto &cd : cloudDirs) {
      cloudDirByUuid[cd.uuid] = cd;
      cloudDirByPath[cd.path] = cd;
    }
    for (const auto &f : dbFiles.value()) {
      localFileByUuid[f.uuid] = f;
    }
    for (const auto &d : dbDirs.value()) {
      localDirByUuid[d.uuid] = d;
    }
    for (const auto &s : syncedItems.value()) {
      syncedItemsByOrigin[s.origin] = s;
    }

    std::vector<FileQueueEntry> filesToSync;
    std::vector<DirectoryQueueEntry> dirsToSync;
    FileQMapContainer filesInConflict;
    FileQMapContainer filesToSyncMap;
    std::map<std::string, FileQueueEntry> filesToSyncPathMap;

    std::vector<DirectoryQueueEntry> dirsInConflict;
    std::map<std::string, DirectoryQueueEntry> dirsToSyncMap;
    std::map<std::string, DirectoryQueueEntry> dirsToUpSyncPathMap;

    //-------------------------------------------------------
    // compute differences between cloud & synced state;
    //-------------------------------------------------------
    // 1. cloud files not synced

    for (const auto &f : cloudFiles) {
      auto it = syncedItemsByOrigin.find(f.origin);
      if (it == syncedItemsByOrigin.end()) {
        // new file added in cloud
        FileQueueEntry fq =
            Utility::convert<FileQueueEntry>(f, SyncStatus::NEW);
        fq.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
        fq.syncType = "cloud";
        filesToSync.push_back(fq);
      }
      if (it != syncedItemsByOrigin.end() && it->second.name != f.filename &&
          it->second.parentID == f.dirID) {
        // file renamed in cloud
        FileQueueEntry fq = Utility::convert<FileQueueEntry>(
            f, SyncStatus::RENAME, "", it->second.name);
        fq.priority = qPriorityToInt(QPriority::FILE_RENAME_CLOUD);
        fq.syncType = "cloud";
        filesToSync.push_back(fq);
      }
      if (it != syncedItemsByOrigin.end() && it->second.name == f.filename &&
          it->second.parentID != f.dirID) {
        // file moved in cloud
        auto old_path = m_dbManager.getPathById(it->second.uuid);
        FileQueueEntry fq = Utility::convert<FileQueueEntry>(
            f, SyncStatus::MOVED, old_path.value(), it->second.name);
        fq.priority = qPriorityToInt(QPriority::FILE_MOVED_CLOUD);
        fq.syncType = "cloud";
        filesToSync.push_back(fq);
      }
      if (it != syncedItemsByOrigin.end() &&
          f.hashvalue != it->second.hashvalue) {
        // file modified in cloud
        FileQueueEntry fq = Utility::convert<FileQueueEntry>(
            f, SyncStatus::MODIFIED, "", it->second.name);
        fq.priority = qPriorityToInt(QPriority::FILE_MODIFIED_CLOUD);
        fq.syncType = "cloud";
        filesToSync.push_back(fq);
      }
    }
    for (const auto &s : syncedItems.value()) {
      auto it = cloudFileByUuid.find(s.origin);
      if (it == cloudFileByUuid.end()) {
        // file deleted in cloud
        std::optional<std::string> path;
        std::optional<FileMetadata> file;
        {
          std::lock_guard<std::recursive_mutex> lock(
              m_dbManager.getSyncMutex());
          path = m_dbManager.getPathById(s.uuid);
          file = m_dbManager.getFileByPath(path.value(), s.name);
        }
        FileQueueEntry fq =
            Utility::convert<FileQueueEntry>(file.value(), SyncStatus::DELETE);
        fq.priority = qPriorityToInt(QPriority::FILE_DELETE_CLOUD);
        fq.syncType = "cloud";
        filesToSync.push_back(fq);
      }
    }

    // 2. cloud dirs not synced
    for (const auto &d : cloudDirs) {
      auto it = syncedItemsByOrigin.find(d.uuid);
      if (it == syncedItemsByOrigin.end()) {
        // new dir in cloud
        LocalFolderCreateMetadata cf;
        cf.absPath = d.path == "/" ? m_syncPath : m_syncPath + d.path;
        cf.path = d.path;
        cf.folder = d.folder;
        cf.uuid = d.uuid;
        cf.device = d.device;
        cf.created_at = d.created_at;
        auto dirPaths = getPathComponents(d.path);

        for (auto &path : dirPaths) {
          auto it = cloudDirByPath.find(path);
          if (it != cloudDirByPath.end()) {
            if (!cf.dirIDs) {
              cf.dirIDs = std::map<std::string, std::string>();
            }
            (*cf.dirIDs)[path] = it->second.uuid;
          }
        }
        // result.foldersToCreateLocal.push_back(cf);
        DirectoryQueueEntry dq =
            Utility::convert<DirectoryQueueEntry>(cf, SyncStatus::NEW, "");
        dq.syncType = "cloud";
        dq.priority = qPriorityToInt(QPriority::FOLDER_CREATE_CLOUD);
        dirsToSync.push_back(dq);
      }
    }
    for (const auto &s : syncedItems.value()) {
      // dir deleted in cloud
      auto it = cloudDirByUuid.find(s.origin);
      if (it == cloudDirByUuid.end()) {
        LocalFolderDeleteMetadata dq;
        std::optional<std::string> path = "";
        {
          std::lock_guard<std::recursive_mutex> lock(
              m_dbManager.getSyncMutex());
          path = m_dbManager.getPathById(s.uuid);
        }
        dq.absPath =
            path.value() == "/" ? m_syncPath : m_syncPath + path.value();
        auto part = m_dbManager.getFolderDevice(path.value());
        dq.inode = s.inode;
        dq.folder = s.name;
        dq.device = part.device;
        dq.path = path.value();
        dq.uuid = s.uuid;
        dq.created_at = s.last_modified;
        //        result.foldersToDeleteLocal.push_back(dq);
        DirectoryQueueEntry d =
            Utility::convert<DirectoryQueueEntry>(dq, SyncStatus::DELETE, "");
        d.syncType = "cloud";
        d.priority = qPriorityToInt(QPriority::FOLDER_DELETE_CLOUD);
        dirsToSync.push_back(d);
      }
    }

    // 3. local files not synced
    for (const auto &f : dbFiles.value()) {
      auto it = syncedItemsByOrigin.find(f.origin);
      if (it == syncedItemsByOrigin.end()) {
        // new file in local
        FileQueueEntry fq;
        fq = Utility::convert<FileQueueEntry>(f, SyncStatus::NEW);
        fq.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
        fq.syncType = "local";
        filesToSync.push_back(fq);
        //        result.filesToUpSync.push_back(fq);
      }
      if (it != syncedItemsByOrigin.end() &&
          f.hashvalue != it->second.hashvalue) {
        // file modified in local
        FileQueueEntry fq;
        fq = Utility::convert<FileQueueEntry>(f, SyncStatus::MODIFIED);
        fq.priority = qPriorityToInt(QPriority::FILE_MODIFIED);
        fq.syncType = "local";
        filesToSync.push_back(fq);
        //        result.filesToUpSync.push_back(fq);
      }
      if (it != syncedItemsByOrigin.end() && f.filename != it->second.name &&
          f.dirID == it->second.parentID) {
        FileQueueEntry fq;
        auto path = m_dbManager.getPathById(it->second.uuid);
        fq = Utility::convert<FileQueueEntry>(f, SyncStatus::RENAME,
                                              path.value(), it->second.name);
        fq.syncType = "local";
        fq.priority = qPriorityToInt(QPriority::FILE_RENAME);
        filesToSync.push_back(fq);
        //        result.filesToUpSync.push_back(fq);
      }
      if (it != syncedItemsByOrigin.end() && f.filename == it->second.name &&
          f.dirID != it->second.parentID) {
        FileQueueEntry fq;
        auto path = m_dbManager.getPathById(it->second.uuid);
        fq = Utility::convert<FileQueueEntry>(f, SyncStatus::MOVED,
                                              path.value(), it->second.name);
        fq.syncType = "local";
        fq.priority = qPriorityToInt(QPriority::FILE_MOVED);
        filesToSync.push_back(fq);
        //        result.filesToUpSync.push_back(fq);
      }
    }
    for (const auto &f : syncedItems.value()) {
      auto it = localFileByUuid.find(f.origin);
      if (it == localFileByUuid.end()) {
        // file deleted local
        FileQueueEntry fq;
        auto path = m_dbManager.getPathById(f.uuid);
        fq.path = path.value();
        fq.filename = f.name;
        fq.absPath =
            path.value() == "/" ? m_syncPath : m_syncPath + path.value();
        fq.dirID = f.parentID;
        fq.hashvalue = f.hashvalue;
        fq.inode = f.inode;
        fq.last_modified = f.last_modified;
        fq.sync_status = syncStatusToString(SyncStatus::DELETE);
        fq.syncType = "local";
        fq.priority = qPriorityToInt(QPriority::FILE_DELETE);
        filesToSync.push_back(fq);
        //        result.filesToUpSync.push_back(fq);
      }
    }

    // 4. local dirs not synced
    for (const auto &d : dbDirs.value()) {
      auto it = syncedItemsByOrigin.find(d.uuid);
      if (it == syncedItemsByOrigin.end()) {
        // new dir in local
        DirectoryQueueEntry dq;
        dq = Utility::convert<DirectoryQueueEntry>(d, SyncStatus::NEW);
        dq.priority = qPriorityToInt(QPriority::FOLDER_CREATE);
        dq.syncType = "local";
        dirsToSync.push_back(dq);
        //        result.dirsToUpSync.push_back(dq);
      }
      if (it != syncedItemsByOrigin.end()) {
        auto path = m_dbManager.getPathById(it->second.uuid);
        DirectoryQueueEntry dq;
        if (d.folder != it->second.name && path.value() == d.path) {
          dq = Utility::convert<DirectoryQueueEntry>(d, SyncStatus::RENAME, "",
                                                     it->second.name);
          dq.priority = qPriorityToInt(QPriority::FOLDER_RENAME);
        }
        if (d.folder == it->second.name && path.value() != d.path) {
          dq = Utility::convert<DirectoryQueueEntry>(d, SyncStatus::MOVED,
                                                     path.value());
          dq.priority = qPriorityToInt(QPriority::FOLDER_MOVED);
        }
        dq.syncType = "local";
        dirsToSync.push_back(dq);
        //        result.dirsToUpSync.push_back(dq);
      }
    }
    for (const auto &d : syncedItems.value()) {
      auto it = localDirByUuid.find(d.uuid);
      if (it == localDirByUuid.end()) {
        // dir deleted local
        DirectoryQueueEntry dq;
        auto path = m_dbManager.getPathById(d.uuid);
        dq.absPath =
            path.value() == "/" ? m_syncPath : m_syncPath + path.value();
        dq.uuid = d.uuid;
        dq.inode = d.inode;
        dq.folder = d.name;
        auto part = m_dbManager.getFolderDevice(path.value());
        dq.device = part.device;
        dq.path = path.value();
        dq.created_at = d.last_modified;
        dq.sync_status = syncStatusToString(SyncStatus::DELETE);
        dq.syncType = "local";
        dq.priority = qPriorityToInt(QPriority::FOLDER_DELETE);
        dirsToSync.push_back(dq);
        //        result.dirsToUpSync.push_back(dq);
      }
    }

    for (const auto &f : filesToSync) {

      auto itUuid = filesToSyncMap.find(f.uuid);
      std::string path = f.path + f.filename;
      auto itPath = filesToSyncPathMap.find(path);
      if (itPath == filesToSyncPathMap.end()) {
        filesToSyncPathMap[path] = f;
      } else {
        FileQueueEntry file = itPath->second;
        if (file.uuid == f.uuid) {
          filesToSyncPathMap.erase(path);
        } else {
          // new file conflict
          FileQueueEntry f1 = f;
          FileQueueEntry f2 = file;
          FileQueueEntry cf;
          FileQueueEntry lf;
          if (f1.syncType == "local" && f2.syncType == "cloud") {
            cf = f2;
            lf = f1;
          }
          if (f1.syncType == "cloud" && f2.syncType == "local") {
            cf = f1;
            lf = f2;
          }

          cf.priority = qPriorityToInt(QPriority::FILE_DOWNLOAD);
          lf.priority = qPriorityToInt(QPriority::FILE_UPLOAD);
          cf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
          lf.sync_status = syncStatusToString(SyncStatus::CONFLICT);
          lf.uuid = lf.origin = UuidUtils::generate();
          filesToSyncPathMap.erase(path);
          filesInConflict[cf.uuid]["cloud"].push(cf);
          filesInConflict[cf.uuid]["local"].push(lf);
        }
      }
      if (itUuid == filesToSyncMap.end()) {
        filesToSyncMap[f.uuid][f.syncType.value()].push(f);
      }
      if (itUuid != filesToSyncMap.end()) {
        auto fileInMap = itUuid->second;
        auto it = fileInMap.find("cloud");
        if (it != fileInMap.end() && f.syncType == "cloud") {
          filesToSyncMap[f.uuid]["cloud"].push(f);
        }
        if (it == fileInMap.end() && f.syncType == "local") {
          filesToSyncMap[f.uuid]["local"].push(f);
        }
        if ((it != fileInMap.end() && f.syncType == "local") ||
            (it == fileInMap.end() && f.syncType == "cloud")) {
          // conflict resolution
          if (it->first == "cloud" && f.syncType == "local") {
            while (!it->second.empty()) {
              auto fi = it->second.top();
              it->second.pop();
              FileQueueEntry cf = fi;
              FileQueueEntry lf = f;
              auto localStatus = stringToSyncStatus(lf.sync_status);
              auto cloudStatus = stringToSyncStatus(cf.sync_status);
              if (cloudStatus == SyncStatus::MODIFIED) {
                handleModifiedConflict(cf, lf, filesInConflict, localStatus);
              }
              if (cloudStatus == SyncStatus::RENAME) {
                handleRenameConflict(cf, lf, filesInConflict, localStatus);
              }
              if (cloudStatus == SyncStatus::MOVED) {
                handleMoveConflict(cf, lf, filesInConflict, localStatus);
              }
              if (cloudStatus == SyncStatus::DELETE) {
                handleDeleteConflict(cf, lf, localStatus);
              }
            }
            filesToSyncMap.erase(itUuid);
          }
          if (it->first == "local" && f.syncType == "cloud") {
            while (!it->second.empty()) {
              auto fi = it->second.top();
              it->second.pop();
              FileQueueEntry cf = f;
              FileQueueEntry lf = fi;
              auto localStatus = stringToSyncStatus(lf.sync_status);
              auto cloudStatus = stringToSyncStatus(cf.sync_status);
              if (cloudStatus == SyncStatus::MODIFIED) {
                handleModifiedConflict(cf, lf, filesInConflict, localStatus);
              }
              if (cloudStatus == SyncStatus::RENAME) {
                handleRenameConflict(cf, lf, filesInConflict, localStatus);
              }
              if (cloudStatus == SyncStatus::MOVED) {
                handleMoveConflict(cf, lf, filesInConflict, localStatus);
              }
              if (cloudStatus == SyncStatus::DELETE) {
                handleDeleteConflict(cf, lf, localStatus);
              }
            }
            filesToSyncMap.erase(itUuid);
          }
        }
      }
    }
    for (auto &d : dirsToSync) {
    }
  }
}

void CloudSyncWorker::startSync() { start(); }

void CloudSyncWorker::stopSync() { stop(); }

void CloudSyncWorker::getQuota() {
  auto quota = m_apiClient.getQuotaUsage();
  if (quota.has_value()) {
    std::cout << "[cloudsyncworker] Quota Size: " << quota.value() << std::endl;
    emit quotaFetched(quota.value());
  }
}

void CloudSyncWorker::start() {
  m_stopThread = false;
  std::cout << "[cloudsyncworker] starting sync..." << std::endl;
  initActivityAndPriorityQ();
  m_uploadThread = std::thread(&CloudSyncWorker::runSyncDown, this);
  m_downloadThread = std::thread(&CloudSyncWorker::runSyncUp, this);
  std::cout << "[cloudsyncworker] sync started." << std::endl;
  emit syncStarted();
  emit isSyncing(true);
  getQuota();
}

void CloudSyncWorker::runSyncDown() {
  while (!m_stopThread) {
    std::cout << "[cloudsyncworker] processing CLOUD queue..." << std::endl;

    pollCloudToSyncToLocal();
    std::unique_lock<std::mutex> lock(m_tasksPendingMutex);
    m_tasksCV.wait(lock,
                   [this] { return m_tasksPending == 0 || m_stopThread; });
    if (m_stopThread)
      break;
    for (int i = 0; i < 5 && !m_stopThread; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void CloudSyncWorker::runSyncUp() {
  while (!m_stopThread) {
    std::cout << "[cloudsyncworker] processing LOCAL queue..." << std::endl;
    processQueueToSyncUp();
    std::unique_lock<std::mutex> lock(m_syncWorker.getUpSyncMutex());
    m_syncWorker.getUpSyncCV().wait(lock, [this]() {
      return !m_syncWorker.fileIsEmpty() || !m_syncWorker.dirIsEmpty() ||
             m_stopThread;
    });
    if (m_stopThread)
      break;
  }
}
void CloudSyncWorker::stop() {
  m_stopThread = true;
  m_tasksCV.notify_all();
  m_syncWorker.getUpSyncCV().notify_all();
  std::cout << "[cloudsyncworker] stopping sync..." << std::endl;
  if (m_uploadThread.joinable()) {
    m_uploadThread.join();
  }
  if (m_downloadThread.joinable()) {
    m_downloadThread.join();
  }
  emit syncStopped();
  emit isSyncing(false);
  std::cout << "[cloudsyncwoker] sync stopped " << std::endl;
}
} // namespace sync_app
