#ifndef SYNCCONTROLLER_HPP
#define SYNCCONTROLLER_HPP

#include "ActivityModel.hpp"
#include "types.hpp"
#include <QObject>
#include <QString>
#include <QVariantList>

namespace sync_app {

class CloudSyncWorker;
class SyncWorker;

class SyncController : public QObject {
  Q_OBJECT
  Q_PROPERTY(ActivityModel *activityModel READ activityModel CONSTANT)
  Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY isSyncingChanged)
  Q_PROPERTY(QString storageUsed READ storageUsed NOTIFY storageUsedChanged)

public:
  static void initialize(CloudSyncWorker *worker, SyncWorker *syncWorker) {
    if (!m_instance) {
      m_instance = new SyncController(worker, syncWorker);
    }
  }
  static SyncController *instance() { return m_instance; }
  SyncController(const SyncController &) = delete;
  SyncController &operator=(const SyncController &) = delete;

  // --- getters
  ActivityModel *activityModel() { return m_activityModel; }
  bool isSyncing() const { return m_isSyncing; }
  QString storageUsed() const { return m_storageUsed; }

  // QML callable actions
  Q_INVOKABLE void startSync();
  Q_INVOKABLE void pauseSync();
  Q_INVOKABLE void uploadFile(const QString &path);
  Q_INVOKABLE void deleteFile(const QString &path);

signals:
  void isSyncingChanged();
  void storageUsedChanged();
  void showError(const QString &message);

private slots:
  void onSyncStarted();
  void onSyncFinished();
  void onErrorOccurred(const QString &message);

private:
  explicit SyncController(CloudSyncWorker *syncEngine, SyncWorker *syncWorker,
                          QObject *parent = nullptr);
  static SyncController *m_instance;
  CloudSyncWorker *m_cloudSyncWorker = nullptr;
  SyncWorker *m_syncWorker = nullptr;
  ActivityModel *m_activityModel = nullptr;

  bool m_isSyncing = false;
  QString m_storageUsed = "0 MB";
};

} // namespace sync_app

#endif // SYNCCONTROLLER_HPP
