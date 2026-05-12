#pragma once

#include "ActivityModel.hpp"
#include "ExplorerModel.hpp"
#include "Utility.hpp"
#include "qtmetamacros.h"
#include "types.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <condition_variable>
#include <thread>

#ifndef SYNCCONTROLLER_HPP
#define SYNCCONTROLLER_HPP

namespace sync_app {

class CloudSyncWorker;
class CloudBackupManager;
class DatabaseManager;
class SyncWorker;

class SyncController : public QObject {
  Q_OBJECT

  // ── Models exposed to QML ─────────────────────────────────────────────────
  Q_PROPERTY(ActivityModel *activityModel READ activityModel CONSTANT)
  Q_PROPERTY(ExplorerModel *explorerModel READ explorerModel CONSTANT)

  // ── Sync state ────────────────────────────────────────────────────────────
  Q_PROPERTY(int64_t filesSyncing READ filesSyncing NOTIFY filesSyncingChanged)
  Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY isSyncingChanged)
  Q_PROPERTY(QString storageUsed READ storageUsed NOTIFY storageUsedChanged)
  Q_PROPERTY(QString quota READ quota NOTIFY quotaChanged)
  Q_PROPERTY(
      float usagePercentage READ usagePercentage NOTIFY usagePercentageChanged)

  // ── Explorer state ────────────────────────────────────────────────────────
  Q_PROPERTY(bool isExplorerLoading READ isExplorerLoading NOTIFY
                 isExplorerLoadingChanged)
  Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
  Q_PROPERTY(QStringList pathHistory READ pathHistory NOTIFY pathHistoryChanged)

  // -- Sync Path State --------------------------------------------------------
  Q_PROPERTY(QString syncPath READ syncPath NOTIFY syncFolderChanged)

public:
  // ── Singleton ─────────────────────────────────────────────────────────────
  static void initialize(CloudSyncWorker *syncWorker, SyncWorker *worker,
                         CloudBackupManager *backupManager,
                         DatabaseManager *dbManager,
                         const std::string &syncPath) {
    if (!m_instance) {
      m_instance = new SyncController(syncWorker, worker, backupManager,
                                      dbManager, syncPath);
    }
  }
  static SyncController *instance() { return m_instance; }
  SyncController(const SyncController &) = delete;
  SyncController &operator=(const SyncController &) = delete;
  void populateActivity();
  void start();
  void stop();

  // ── Getters ───────────────────────────────────────────────────────────────
  ActivityModel *activityModel() { return m_activityModel; }
  ExplorerModel *explorerModel() { return m_explorerModel; }
  bool isSyncing() const { return m_isSyncing; }
  int64_t filesSyncing() const { return m_filesSyncing; }
  QString storageUsed() const { return m_storageUsed; }
  QString quota() const { return m_quota; }
  float usagePercentage() const { return m_usagePercentage; }
  bool isExplorerLoading() const { return m_isExplorerLoading; }
  QString currentPath() const { return m_currentPath; }
  QStringList pathHistory() const { return m_pathHistory; }
  QString syncPath() const { return m_syncPath; }

  // ── QML invokables — sync ─────────────────────────────────────────────────
  Q_INVOKABLE void startSync();
  Q_INVOKABLE void pauseSync();
  Q_INVOKABLE void uploadFile(const QString &path);
  Q_INVOKABLE void setSyncFolder(const QString &syncPath);

  // ── QML invokables — explorer ─────────────────────────────────────────────
  Q_INVOKABLE void loadDirectory(const QString &path);
  Q_INVOKABLE void navigateBack();
  Q_INVOKABLE void downloadFile(const QString &id);
  Q_INVOKABLE void deleteFile(const QString &id);
  Q_INVOKABLE void deleteFolder(const QString &path);
  Q_INVOKABLE void toggleSelected(const QString &id);
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE QStringList selectedIds() const;

signals:
  // ── Sync signals ──────────────────────────────────────────────────────────
  void isSyncingChanged();
  void filesSyncingChanged();
  void storageUsedChanged();
  void showError(const QString &message);
  void syncFolderChanged();
  void usagePercentageChanged();
  void quotaChanged();

  // ── Explorer signals ──────────────────────────────────────────────────────
  void isExplorerLoadingChanged();
  void currentPathChanged();
  void pathHistoryChanged();

private slots:
  void onSyncStarted();
  void onSyncFinished();
  void onErrorOccurred(const QString &message);
  void onQuotaFetched(const int64_t &quota);
  // void onSyncPathChanged(const QString &syncPath);

private:
  explicit SyncController(CloudSyncWorker *syncEngine, SyncWorker *syncWorker,
                          CloudBackupManager *backupManager,
                          DatabaseManager *dbManager,
                          const std::string &syncPath,
                          QObject *parent = nullptr);

  static SyncController *m_instance;

  CloudSyncWorker *m_cloudSyncWorker = nullptr;
  CloudBackupManager *m_backupManager = nullptr;
  DatabaseManager *m_dbManager = nullptr;
  SyncWorker *m_syncWorker = nullptr;
  ActivityModel *m_activityModel = nullptr;
  ExplorerModel *m_explorerModel = nullptr;

  // ── State ─────────────────────────────────────────────────────────────────
  bool m_isSyncing = false;
  std::atomic<int64_t> m_filesSyncing = 0;
  QString m_storageUsed = "0 KB";
  int64_t m_quotaBytes = 10240000000;
  QString m_quota =
      QString::fromStdString(Utility::formatFileSize(m_quotaBytes));
  float m_usagePercentage = 0.0;
  bool m_isExplorerLoading = false;
  QString m_currentPath = "/";
  QString m_syncPath = "/";
  QStringList m_pathHistory = {"/"};
  // Timer to cleanup the Activity DB of children files
  std::mutex m_timerMutex;
  std::condition_variable m_cv;
  std::thread m_timerThread;
  std::atomic<bool> m_stopThread = false;

  void workerLoop();
};

} // namespace sync_app

#endif // SYNCCONTROLLER_HPP
