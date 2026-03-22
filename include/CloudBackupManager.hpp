#pragma once

#include "types.hpp"
#include <QObject>
#include <QRunnable>
#include <QThreadPool>
#include <memory>
#include <string>

namespace sync_app {
class CloudBackupManager;
class ApiClient;

// ─────────────────────────────────────────────────────────────────────────────
// QRunnable tasks — one per operation, all use QThreadPool::globalInstance()
// setAutoDelete(true) means Qt destroys the task after run() finishes
// ─────────────────────────────────────────────────────────────────────────────

class FetchDirectoryTask : public QRunnable {
public:
  FetchDirectoryTask(CloudBackupManager *manager,
                     std::unique_ptr<ApiClient> client,
                     const std::string &path);
  void run() override;

private:
  CloudBackupManager *m_manager;
  std::unique_ptr<ApiClient> m_client;
  std::string m_path;
};

class DownloadFileTask : public QRunnable {
public:
  DownloadFileTask(CloudBackupManager *manager,
                   std::unique_ptr<ApiClient> client, const std::string &id);
  void run() override;

private:
  CloudBackupManager *m_manager;
  std::unique_ptr<ApiClient> m_client;
  std::string m_id;
};

class DeleteFileTask : public QRunnable {
public:
  DeleteFileTask(CloudBackupManager *manager, std::unique_ptr<ApiClient> client,
                 const std::string &id);
  void run() override;

private:
  CloudBackupManager *m_manager;
  std::unique_ptr<ApiClient> m_client;
  std::string m_id;
};

class DeleteFolderTask : public QRunnable {
public:
  DeleteFolderTask(CloudBackupManager *manager,
                   std::unique_ptr<ApiClient> client, const std::string &path);
  void run() override;

private:
  CloudBackupManager *m_manager;
  std::unique_ptr<ApiClient> m_client;
  std::string m_path;
};

// ─────────────────────────────────────────────────────────────────────────────
// CloudBackupManager
// Handles all on-demand UI operations: browse, download, delete.
// CloudSyncWorker handles background sync — this class never touches that.
// ─────────────────────────────────────────────────────────────────────────────

class CloudBackupManager : public QObject {
  Q_OBJECT
public:
  explicit CloudBackupManager(ApiClient &apiClient, QObject *parent = nullptr);

  // called by SyncController — each spawns a QRunnable on globalInstance()
  void fetchDirectory(const std::string &path);
  void downloadFile(const std::string &id);
  void deleteFile(const std::string &id);
  void deleteFolder(const std::string &path);

signals:
  // directoryLoaded/Failed fire from background thread —
  // SyncController connects with Qt::QueuedConnection to hop back to main
  // thread
  void directoryLoaded(const std::vector<ExplorerItem> &items);
  void directoryLoadFailed(const QString &path);

  void downloadComplete(const QString &id);
  void downloadFailed(const QString &id);

  void deleteFileComplete(const QString &id);
  void deleteFileFailed(const QString &id);

  void deleteFolderComplete(const QString &path);
  void deleteFolderFailed(const QString &path);

private:
  ApiClient &m_apiClient;
};

} // namespace sync_app
