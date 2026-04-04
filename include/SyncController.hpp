#pragma once

#include "ActivityModel.hpp"
#include "ExplorerModel.hpp"
#include "qtmetamacros.h"
#include "types.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#ifndef SYNCCONTROLLER_HPP
#define SYNCCONTROLLER_HPP

namespace sync_app {

class CloudSyncWorker;
class CloudBackupManager;
class SyncWorker;

class SyncController : public QObject {
  Q_OBJECT

  // ── Models exposed to QML ─────────────────────────────────────────────────
  Q_PROPERTY(ActivityModel *activityModel READ activityModel CONSTANT)
  Q_PROPERTY(ExplorerModel *explorerModel READ explorerModel CONSTANT)

  // ── Sync state ────────────────────────────────────────────────────────────
  Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY isSyncingChanged)
  Q_PROPERTY(QString storageUsed READ storageUsed NOTIFY storageUsedChanged)

  // ── Explorer state ────────────────────────────────────────────────────────
  Q_PROPERTY(bool isExplorerLoading READ isExplorerLoading NOTIFY
                 isExplorerLoadingChanged)
  Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
  Q_PROPERTY(QStringList pathHistory READ pathHistory NOTIFY pathHistoryChanged)

public:
  // ── Singleton ─────────────────────────────────────────────────────────────
  static void initialize(CloudSyncWorker *syncWorker, SyncWorker *worker,
                         CloudBackupManager *backupManager) {
    if (!m_instance) {
      m_instance = new SyncController(syncWorker, worker, backupManager);
    }
  }
  static SyncController *instance() { return m_instance; }
  SyncController(const SyncController &) = delete;
  SyncController &operator=(const SyncController &) = delete;

  // ── Getters ───────────────────────────────────────────────────────────────
  ActivityModel *activityModel() { return m_activityModel; }
  ExplorerModel *explorerModel() { return m_explorerModel; }
  bool isSyncing() const { return m_isSyncing; }
  QString storageUsed() const { return m_storageUsed; }
  bool isExplorerLoading() const { return m_isExplorerLoading; }
  QString currentPath() const { return m_currentPath; }
  QStringList pathHistory() const { return m_pathHistory; }

  // ── QML invokables — sync ─────────────────────────────────────────────────
  Q_INVOKABLE void startSync();
  Q_INVOKABLE void pauseSync();
  Q_INVOKABLE void uploadFile(const QString &path);

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
  void storageUsedChanged();
  void showError(const QString &message);

  // ── Explorer signals ──────────────────────────────────────────────────────
  void isExplorerLoadingChanged();
  void currentPathChanged();
  void pathHistoryChanged();

private slots:
  void onSyncStarted();
  void onSyncFinished();
  void onErrorOccurred(const QString &message);

private:
  explicit SyncController(CloudSyncWorker *syncEngine, SyncWorker *syncWorker,
                          CloudBackupManager *backupManager,
                          QObject *parent = nullptr);

  static SyncController *m_instance;

  CloudSyncWorker *m_cloudSyncWorker = nullptr;
  CloudBackupManager *m_backupManager = nullptr;
  SyncWorker *m_syncWorker = nullptr;
  ActivityModel *m_activityModel = nullptr;
  ExplorerModel *m_explorerModel = nullptr;

  // ── State ─────────────────────────────────────────────────────────────────
  bool m_isSyncing = false;
  QString m_storageUsed = "0 MB";
  bool m_isExplorerLoading = false;
  QString m_currentPath = "/";
  QStringList m_pathHistory = {"/"};
};

} // namespace sync_app

#endif // SYNCCONTROLLER_HPP
