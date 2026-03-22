#include "CloudBackupManager.hpp"
#include "ApiClient.hpp"
#include <QThreadPool>

namespace sync_app {

// FetchDirectoryTask

FetchDirectoryTask::FetchDirectoryTask(CloudBackupManager *manager,
                                       std::unique_ptr<ApiClient> client,
                                       const std::string &path)
    : m_manager(manager), m_client(std::move(client)), m_path(path) {
  setAutoDelete(true); // Qt destroys this after run() finishes
}

void FetchDirectoryTask::run() {
  auto result = m_client->getDirectoryContents(m_path);
  if (result.has_value()) {
    // fires from background thread — SyncController uses Qt::QueuedConnection
    // so Qt automatically posts this to the main thread's event loop
    emit m_manager->directoryLoaded(result.value());
  } else {
    emit m_manager->directoryLoadFailed(QString::fromStdString(m_path));
  }
}

// DownloadFileTask

DownloadFileTask::DownloadFileTask(CloudBackupManager *manager,
                                   std::unique_ptr<ApiClient> client,
                                   const std::string &id)
    : m_manager(manager), m_client(std::move(client)), m_id(id) {
  setAutoDelete(true);
}

void DownloadFileTask::run() {
  bool result = m_client->downloadFileById(m_id);
  if (result) {
    emit m_manager->downloadComplete(QString::fromStdString(m_id));
  } else {
    emit m_manager->downloadFailed(QString::fromStdString(m_id));
  }
}

// DeleteFileTask

DeleteFileTask::DeleteFileTask(CloudBackupManager *manager,
                               std::unique_ptr<ApiClient> client,
                               const std::string &id)
    : m_manager(manager), m_client(std::move(client)), m_id(id) {
  setAutoDelete(true);
}

void DeleteFileTask::run() {
  bool result = m_client->deleteFileById(m_id);
  if (result) {
    emit m_manager->deleteFileComplete(QString::fromStdString(m_id));
  } else {
    emit m_manager->deleteFileFailed(QString::fromStdString(m_id));
  }
}

// DeleteFolderTask

DeleteFolderTask::DeleteFolderTask(CloudBackupManager *manager,
                                   std::unique_ptr<ApiClient> client,
                                   const std::string &path)
    : m_manager(manager), m_client(std::move(client)), m_path(path) {
  setAutoDelete(true);
}

void DeleteFolderTask::run() {
  DirectoryQueueEntry dq;
  dq.path = m_path;
  bool result = m_client->deleteFolder(dq);
  if (result) {
    emit m_manager->deleteFolderComplete(QString::fromStdString(m_path));
  } else {
    emit m_manager->deleteFolderFailed(QString::fromStdString(m_path));
  }
}

// CloudBackupManager

CloudBackupManager::CloudBackupManager(ApiClient &apiClient, QObject *parent)
    : QObject(parent), m_apiClient(apiClient) {}

void CloudBackupManager::fetchDirectory(const std::string &path) {
  // clone ApiClient so each task has its own independent HTTP connection
  auto task = new FetchDirectoryTask(this, m_apiClient.clone(), path);
  QThreadPool::globalInstance()->start(task);
}

void CloudBackupManager::downloadFile(const std::string &id) {
  auto task = new DownloadFileTask(this, m_apiClient.clone(), id);
  QThreadPool::globalInstance()->start(task);
}

void CloudBackupManager::deleteFile(const std::string &id) {
  auto task = new DeleteFileTask(this, m_apiClient.clone(), id);
  QThreadPool::globalInstance()->start(task);
}

void CloudBackupManager::deleteFolder(const std::string &path) {
  auto task = new DeleteFolderTask(this, m_apiClient.clone(), path);
  QThreadPool::globalInstance()->start(task);
}

} // namespace sync_app
