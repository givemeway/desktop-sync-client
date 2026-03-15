#pragma once
#include "FilesystemWatcher.hpp"
#include "qobject.h"
#include "types.hpp"
#include <string>
#ifndef SYNC_WORKER_HPP
#define SYNC_WORKER_HPP

namespace sync_app {

class ThreadPool;
class DatabaseManager;
class FileSystemScanner;
class ActivityStore;

class SyncWorker : public QObject {
  Q_OBJECT
public:
  SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
             ActivityStore &activityStore, ThreadPool &threadPool,
             const std::string &syncPath);
  ~SyncWorker();
  void handleAdded(const std::string &path);
  void handleDeleted(const std::string &path);
  void handleRenamed(const std::string &path, const std::string &oldPath,
                     bool isMoved = false);
  void handleModified(const std::string &path);
  void start();
  void stop();
  void enqueueEvent(WatchEvent event, const std::string &path,
                    const std::string &oldPath);
  void addIgnoreEvent(const std::string &path, WatchEvent event);
  void removeIgnoreEvent(const std::string &path, WatchEvent event);
  void removeEventMap(const std::string &inode, const std::string &path = "");
  void workerLoop();
  FileMetadata constructFileMetadata(const ScannedFile &scannedFile,
                                     const FileQueueEntry &f,
                                     const std::string &dirID = "");
  DirectoryMetadata
  constructDirectoryMetadata(const ScannedDirectory &scannedDir,
                             const DirectoryQueueEntry &d);

  bool isHighPriorityLocalTaskQueued() const;
  void pushFileEntry(const FileQueueEntry &fq);
  void pushDirEntry(const DirectoryQueueEntry &dq);
  bool fileIsEmpty();
  bool dirIsEmpty();

  std::optional<FileQueueEntry> popNextFileEntry();
  std::optional<DirectoryQueueEntry> popNextDirEntry();

  void addActivity(const std::string &key, const SyncItem &item);

  template <typename T>
  static std::optional<ActivityStatus> resolveActivityStatus(T &t) {

    if constexpr (std::is_same_v<T, FileQueueEntry>) {
      SyncStatus syncStatus = stringToSyncStatus(t.sync_status);
      switch (syncStatus) {
      case SyncStatus::NEW:
        return ActivityStatus::UPLOAD;
      case SyncStatus::DELETE:
        return ActivityStatus::CLOUD_DELETE;
      case SyncStatus::RENAME:
        return ActivityStatus::CLOUD_RENAME;
      case SyncStatus::MOVED:
        return ActivityStatus::CLOUD_MOVE;
      default:
        return std::nullopt;
      }
    }
    if constexpr (std::is_same_v<T, DirectoryQueueEntry>) {
      SyncStatus syncStatus = stringToSyncStatus(t.sync_status);
      switch (syncStatus) {
      case SyncStatus::NEW:
        return ActivityStatus::CLOUD_FOLDER_CREATE;
      case SyncStatus::DELETE:
        return ActivityStatus::CLOUD_DELETE;
      case SyncStatus::RENAME:
        return ActivityStatus::CLOUD_RENAME;
      case SyncStatus::MOVED:
        return ActivityStatus::CLOUD_MOVE;
      default:
        return std::nullopt;
      }
    }
  }

signals:
  void activityAdded(const std::string &key, const SyncItem &item);

private:
  std::function<void()> m_uploadCallback;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sync_app
#endif
