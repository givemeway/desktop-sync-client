#include "SyncController.hpp"
#include "ActivityModel.hpp"
#include "CloudBackupManager.hpp"
#include "CloudSyncWorker.hpp"
#include "ExplorerModel.hpp"
#include "SyncWorker.hpp"
#include <QDebug>
#include <QString>
#include <QVariantList>

namespace sync_app {

SyncController *SyncController::m_instance = nullptr;

SyncController::SyncController(CloudSyncWorker *syncEngine,
                               SyncWorker *syncWorker,
                               CloudBackupManager *backupManager,
                               QObject *parent)
    : QObject(parent), m_cloudSyncWorker(syncEngine),
      m_syncWorker(syncWorker), m_backupManager(backupManager) {

  m_activityModel = new ActivityModel(this);
  m_explorerModel = new ExplorerModel(this);

  // ── CloudSyncWorker → ActivityModel ───────────────────────────────────────
  connect(m_cloudSyncWorker, &CloudSyncWorker::activityUpdated,
          m_activityModel, &ActivityModel::onActivityUpdated);

  connect(m_cloudSyncWorker, &CloudSyncWorker::activityRemoved,
          m_activityModel, &ActivityModel::onActivityRemoved);

  connect(m_cloudSyncWorker, &CloudSyncWorker::activityAdded,
          m_activityModel, &ActivityModel::onActivityAdded);

  connect(m_syncWorker, &SyncWorker::activityAdded,
          m_activityModel, &ActivityModel::onActivityAdded);

  // ── CloudSyncWorker → SyncController sync state ───────────────────────────
  connect(m_cloudSyncWorker, &CloudSyncWorker::syncStarted,
          this, &SyncController::onSyncStarted);

  connect(m_cloudSyncWorker, &CloudSyncWorker::syncStopped,
          this, &SyncController::onSyncFinished);

  connect(m_cloudSyncWorker, &CloudSyncWorker::errorOccurred,
          this, &SyncController::onErrorOccurred);

  // ── CloudBackupManager → ExplorerModel ────────────────────────────────────
  // Qt::QueuedConnection is required here because these signals fire from a
  // background QRunnable thread. QueuedConnection posts the slot call as an
  // event to the main thread's event loop, so ExplorerModel (a Qt model)
  // is always updated on the main thread — safe for QML.

  connect(m_backupManager, &CloudBackupManager::directoryLoaded,
          this, [this](const std::vector<ExplorerItem> &items) {
            m_explorerModel->populate(items);
            m_isExplorerLoading = false;
            emit isExplorerLoadingChanged();
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::directoryLoadFailed,
          this, [this](const QString &path) {
            qWarning() << "[SyncController] directory load failed:" << path;
            m_isExplorerLoading = false;
            emit isExplorerLoadingChanged();
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::downloadComplete,
          this, [this](const QString &id) {
            qDebug() << "[SyncController] download complete:" << id;
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::downloadFailed,
          this, [this](const QString &id) {
            qWarning() << "[SyncController] download failed:" << id;
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::deleteFileComplete,
          this, [this](const QString &id) {
            // remove from explorer model so UI updates immediately
            m_explorerModel->onRowRemoved(id.toStdString());
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::deleteFileFailed,
          this, [this](const QString &id) {
            qWarning() << "[SyncController] delete file failed:" << id;
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::deleteFolderComplete,
          this, [this](const QString &path) {
            m_explorerModel->onRowRemoved(path.toStdString());
          }, Qt::QueuedConnection);

  connect(m_backupManager, &CloudBackupManager::deleteFolderFailed,
          this, [this](const QString &path) {
            qWarning() << "[SyncController] delete folder failed:" << path;
          }, Qt::QueuedConnection);
}

// ── Sync slots ────────────────────────────────────────────────────────────────

void SyncController::onSyncStarted() {
  m_isSyncing = true;
  emit isSyncingChanged();
}

void SyncController::onSyncFinished() {
  m_isSyncing = false;
  emit isSyncingChanged();
}

void SyncController::onErrorOccurred(const QString &message) {
  qWarning() << "[SyncController] sync error:" << message;
  emit showError(message);
}

// ── Sync invokables ───────────────────────────────────────────────────────────

void SyncController::startSync() {}

void SyncController::pauseSync() {}

void SyncController::uploadFile(const QString &path) {
  Q_UNUSED(path)
}

// ── Explorer invokables ───────────────────────────────────────────────────────

void SyncController::loadDirectory(const QString &path) {
  // save current path so navigateBack() can return to it
  m_pathHistory.append(m_currentPath);
  emit pathHistoryChanged();

  m_currentPath = path;
  emit currentPathChanged();

  // show spinner immediately before background task starts
  m_isExplorerLoading = true;
  emit isExplorerLoadingChanged();

  m_backupManager->fetchDirectory(path.toStdString());
}

void SyncController::navigateBack() {
  if (m_pathHistory.isEmpty())
    return;

  m_currentPath = m_pathHistory.last();
  m_pathHistory.removeLast();
  emit currentPathChanged();
  emit pathHistoryChanged();

  m_isExplorerLoading = true;
  emit isExplorerLoadingChanged();

  m_backupManager->fetchDirectory(m_currentPath.toStdString());
}

void SyncController::downloadFile(const QString &id) {
  m_backupManager->downloadFile(id.toStdString());
}

void SyncController::deleteFile(const QString &id) {
  m_backupManager->deleteFile(id.toStdString());
}

void SyncController::deleteFolder(const QString &path) {
  m_backupManager->deleteFolder(path.toStdString());
}

void SyncController::toggleSelected(const QString &id) {
  auto ids = m_explorerModel->selectedIds();
  bool currentlySelected = ids.contains(id);
  m_explorerModel->setSelected(id, !currentlySelected);
}

void SyncController::selectAll() {
  m_explorerModel->selectAll();
}

QStringList SyncController::selectedIds() const {
  return m_explorerModel->selectedIds();
}

} // namespace sync_app
