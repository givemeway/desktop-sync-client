#include "SyncController.hpp"
#include "ActivityModel.hpp"
#include "CloudSyncWorker.hpp"
#include "SyncWorker.hpp"
#include <QDebug>
#include <QString>
#include <QVariantList>

namespace sync_app {

SyncController *SyncController::m_instance = nullptr;

SyncController::SyncController(CloudSyncWorker *syncEngine,
                               SyncWorker *syncWorker, QObject *parent)
    : QObject(parent), m_cloudSyncWorker(syncEngine), m_syncWorker(syncWorker) {

  m_activityModel = new ActivityModel(this);

  // CloudSyncWorker → ActivityModel
  connect(m_cloudSyncWorker, &CloudSyncWorker::activityUpdated, m_activityModel,
          &ActivityModel::onActivityUpdated);

  connect(m_cloudSyncWorker, &CloudSyncWorker::activityRemoved, m_activityModel,
          &ActivityModel::onActivityRemoved);

  connect(m_cloudSyncWorker, &CloudSyncWorker::activityAdded, m_activityModel,
          &ActivityModel::onActivityAdded);

  connect(m_syncWorker, &SyncWorker::activityAdded, m_activityModel,
          &ActivityModel::onActivityAdded);

  // CloudSyncWorker → SyncController state
  connect(m_cloudSyncWorker, &CloudSyncWorker::syncStarted, this,
          &SyncController::onSyncStarted);

  connect(m_cloudSyncWorker, &CloudSyncWorker::syncStopped, this,
          &SyncController::onSyncFinished);

  connect(m_cloudSyncWorker, &CloudSyncWorker::errorOccurred, this,
          &SyncController::onErrorOccurred);
}

void SyncController::onSyncStarted() {
  m_isSyncing = true;
  emit isSyncingChanged();
}

void SyncController::onSyncFinished() {
  m_isSyncing = false;
  emit isSyncingChanged();
}

void SyncController::onErrorOccurred(const QString &message) {
  qWarning() << "Sync error:" << message;
}

void SyncController::startSync() {}
void SyncController::pauseSync() { /* pause logic */ }
void SyncController::uploadFile(const QString &path) {}
void SyncController::deleteFile(const QString &path) {}

} // namespace sync_app
