#include "SyncController.hpp"
#include "ActivityModel.hpp"
#include "CloudBackupManager.hpp"
#include "CloudSyncWorker.hpp"
#include "DatabaseManager.hpp"
#include "ExplorerModel.hpp"
#include "SyncWorker.hpp"
#include "Utility.hpp"
#include "qobject.h"
#include "types.hpp"
#include <QDebug>
#include <QString>
#include <QVariantList>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace sync_app {

SyncController *SyncController::m_instance = nullptr;

SyncController::SyncController(CloudSyncWorker *syncEngine,
                               SyncWorker *syncWorker,
                               CloudBackupManager *backupManager,
                               DatabaseManager *dbManager,
                               const std::string &syncPath, QObject *parent)
    : m_cloudSyncWorker(syncEngine), m_syncWorker(syncWorker),
      m_backupManager(backupManager), m_dbManager(dbManager),
      m_syncPath(QString::fromStdString(syncPath)), QObject(parent) {

  m_activityModel = new ActivityModel(this);
  m_explorerModel = new ExplorerModel(this);
  // ── CloudSyncWorker → ActivityModel ───────────────────────────────────────
  connect(m_cloudSyncWorker, &CloudSyncWorker::activityUpdated, m_activityModel,
          &ActivityModel::onActivityUpdated);

  connect(m_cloudSyncWorker, &CloudSyncWorker::activityRemoved, m_activityModel,
          &ActivityModel::onActivityRemoved);

  connect(m_cloudSyncWorker, &CloudSyncWorker::activityAdded, m_activityModel,
          &ActivityModel::onActivityAdded);

  connect(m_syncWorker, &SyncWorker::activityAdded, m_activityModel,
          &ActivityModel::onActivityAdded);

  connect(m_cloudSyncWorker, &CloudSyncWorker::quotaFetched, this,
          &SyncController::onQuotaFetched);

  connect(m_cloudSyncWorker, &CloudSyncWorker::isSyncing, this,
          [&](bool isSyncing) {
            m_isSyncing = isSyncing;
            emit isSyncingChanged();
          });

  // ── CloudSyncWorker → SyncController sync state ───────────────────────────
  connect(m_cloudSyncWorker, &CloudSyncWorker::syncStarted, this,
          &SyncController::onSyncStarted);

  connect(m_cloudSyncWorker, &CloudSyncWorker::syncStopped, this,
          &SyncController::onSyncFinished);

  connect(m_cloudSyncWorker, &CloudSyncWorker::errorOccurred, this,
          &SyncController::onErrorOccurred);

  connect(m_activityModel, &ActivityModel::filesSyncingChanged, this,
          [&](int64_t fileSyncing) {
            m_filesSyncing = fileSyncing;
            emit filesSyncingChanged();
          });
  // ── CloudBackupManager → ExplorerModel ────────────────────────────────────
  // Qt::QueuedConnection is required here because these signals fire from a
  // background QRunnable thread. QueuedConnection posts the slot call as an
  // event to the main thread's event loop, so ExplorerModel (a Qt model)
  // is always updated on the main thread — safe for QML.

  connect(
      m_backupManager, &CloudBackupManager::directoryLoaded, this,
      [this](const std::vector<ExplorerItem> &items) {
        m_explorerModel->populate(items);
        m_isExplorerLoading = false;
        emit isExplorerLoadingChanged();
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::directoryLoadFailed, this,
      [this](const QString &path) {
        qWarning() << "[SyncController] directory load failed:" << path;
        m_isExplorerLoading = false;
        emit isExplorerLoadingChanged();
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::downloadComplete, this,
      [this](const QString &id) {
        qDebug() << "[SyncController] download complete:" << id;
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::downloadFailed, this,
      [this](const QString &id) {
        qWarning() << "[SyncController] download failed:" << id;
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::deleteFileComplete, this,
      [this](const QString &id) {
        // remove from explorer model so UI updates immediately
        m_explorerModel->onRowRemoved(id.toStdString());
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::deleteFileFailed, this,
      [this](const QString &id) {
        qWarning() << "[SyncController] delete file failed:" << id;
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::deleteFolderComplete, this,
      [this](const QString &path) {
        m_explorerModel->onRowRemoved(path.toStdString());
      },
      Qt::QueuedConnection);

  connect(
      m_backupManager, &CloudBackupManager::deleteFolderFailed, this,
      [this](const QString &path) {
        qWarning() << "[SyncController] delete folder failed:" << path;
      },
      Qt::QueuedConnection);
}

// ── Sync slots
// ────────────────────────────────────────────────────────────────

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

void SyncController::onQuotaFetched(const int64_t &usage) {
  m_storageUsed = QString::fromStdString(Utility::formatFileSize(usage));
  auto usagePercentage =
      (static_cast<float>(usage) / static_cast<float>(m_quotaBytes));
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2) << usagePercentage;
  m_usagePercentage = usagePercentage;
  emit quotaChanged();
  emit usagePercentageChanged();
  emit storageUsedChanged();
}

// ── Sync invokables
// ───────────────────────────────────────────────────────────

void SyncController::startSync() { m_cloudSyncWorker->startSync(); }

void SyncController::pauseSync() { m_cloudSyncWorker->stopSync(); }

void SyncController::setSyncFolder(const QString &syncPath) {
  m_syncPath = syncPath;
  emit syncFolderChanged();
}

void SyncController::uploadFile(const QString &path) { Q_UNUSED(path) }

// ── Explorer invokables
// ───────────────────────────────────────────────────────

void SyncController::loadDirectory(const QString &path) {
  // save current path so navigateBack() can return to it
  std::string p = path.toStdString();
  std::vector<std::string> pathComponents{"/"};
  if (path != "/") {
    std::vector<std::string> additionalPathComponents =
        m_cloudSyncWorker->getPathComponents(p);
    pathComponents.insert(pathComponents.end(),
                          additionalPathComponents.begin(),
                          additionalPathComponents.end());
  }

  QStringList paths;

  for (const auto &pth : pathComponents) {
    paths.push_back(QString::fromStdString(pth));
  }
  m_pathHistory = paths;
  // m_pathHistory.append(m_currentPath);
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

void SyncController::selectAll() { m_explorerModel->selectAll(); }

QStringList SyncController::selectedIds() const {
  return m_explorerModel->selectedIds();
}

void SyncController::populateActivity() {
  std::lock_guard<std::recursive_mutex> lock(m_dbManager->getSyncMutex());
  auto activities = m_dbManager->getAllActivities();
  if (activities.has_value()) {
    m_activityModel->loadActivities(activities.value());
  }
}

void SyncController::workerLoop() {
  while (!m_stopThread) {
    if (m_stopThread)
      break;
    std::cout << "[synccontroller] cleaning up activity DB.." << std::endl;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager->getSyncMutex());
      m_dbManager->cleanupActivities();
    }
    populateActivity();
    // clean up DB after all the files are synced to cloud .
    for (int i = 0; i < 30 && !m_stopThread; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}
void SyncController::start() {
  m_stopThread = false;
  m_timerThread = std::thread(&SyncController::workerLoop, this);
}
void SyncController::stop() {
  m_stopThread = true;
  m_cv.notify_all();
  if (m_timerThread.joinable()) {
    m_timerThread.join();
  }
}

} // namespace sync_app
