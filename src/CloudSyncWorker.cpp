#include "CloudSyncWorker.hpp"
#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "ReconciliationService.hpp"
#include <iostream>
namespace sync_app {

CloudSyncWorker::CloudSyncWorker(DatabaseManager &dbManager,
                                 ApiClient &apiClient,
                                 ReconciliationService &reconcile,
                                 FileSystemScanner &scanner,
                                 const std::string &syncPath,
                                 const std::string &userEmail)
    : m_dbManager(dbManager), m_apiClient(apiClient), m_reconcile(reconcile),
      m_scanner(scanner), m_syncPath(syncPath), m_userEmail(userEmail),
      m_stopThread(false) {}

CloudSyncWorker::~CloudSyncWorker() { stop(); }
void CloudSyncWorker::processQueue() {

};

void CloudSyncWorker::pollCloudToSyncToLocal() {
  auto result = m_apiClient.getMetadata();
  auto dbFiles = m_dbManager.getAllFiles();
  auto dbDirs = m_dbManager.getAllDirectories();
  if (result.has_value() && result->success) {
    std::vector<CloudFileMetadata> files = result->files;
    std::vector<CloudFolderMetadata> folders = result->directories;
    auto reconciledItems =
        m_reconcile.reconcile(files, folders, *dbFiles, *dbDirs);
    auto filesToDownload = reconciledItems.filesToDownload;
    auto filesToDeleteLocal = reconciledItems.filesToDeleteLocal;
    auto filesToRename = reconciledItems.filesToRename;
    auto foldersToCreateLocal = reconciledItems.foldersToCreateLocal;
    auto foldersToDeleteLocal = reconciledItems.foldersToDeleteLocal;
    auto filesToUpdate = reconciledItems.filesToUpdate;
    auto filesInConflict = reconciledItems.filesInConflict;
    processFilesToDownload(filesToDownload);
    processFilesToDelete(filesToDeleteLocal);
    processFoldersToCreate(foldersToCreateLocal);
  } else {
  }
}
std::vector<std::string>
CloudSyncWorker::getPathComponents(const std::string &path) {

  std::vector<std::string> tokens;
  std::stringstream ss(path);
  std::string token;
  while (std::getline(ss, token, '/')) {
    tokens.push_back(token);
  }
  if (tokens.size() <= 2 && tokens[1].empty()) {
    return std::vector<std::string>{"/"};
  }

  std::vector<std::string> pathTree;
  for (int i = 1; i < tokens.size(); i++) {
    std::string path = "";
    for (int j = 1; j < i + 1; j++) {
      path += "/" + tokens[j];
    }
    pathTree.push_back(path);
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
  return f;
}

DirectoryMetadata
CloudSyncWorker::getDirectoryMetadata(const std::string &path,
                                      const std::string &uuid) {
  DirectoryMetadata d;
  std::string dirAbsPath;
  dirAbsPath = path == "/" ? m_syncPath : m_syncPath + path;
  pathParts p = m_dbManager.getFolderDevice(std::filesystem::path(path));
  d.device = p.device;
  d.absPath = dirAbsPath;
  d.folder = p.folder;
  d.path = path;
  try {
    d.created_at = std::to_string(m_scanner.getUnixTimeStamp(
        std::filesystem::last_write_time(dirAbsPath)));

  } catch (const std::exception &e) {
    std::cerr << "[cloudsyncwoker] error fetching dir timestamp: " << e.what()
              << std::endl;
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
  for (auto &file : filesToDownload) {

    std::string fileAbsPath(file.path == "/"
                                ? m_syncPath + "/" + file.filename
                                : m_syncPath + file.path + "/" + file.filename);
    std::string fileDirPath(
        std::filesystem::path(fileAbsPath).parent_path().generic_string());
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      pathParts fp =
          m_dbManager.getFolderDevice(std::filesystem::path(file.path));
      bool downloadStatus = m_apiClient.downloadFile(file, fileAbsPath);

      if (downloadStatus) {
        std::vector<DirectoryMetadata> dirs;
        FileMetadata f(getFileMetadata(file, fileAbsPath));
        auto dirExists =
            m_dbManager.getDirectoryByPath(fp.device, fp.folder, file.path);
        if (!dirExists.has_value()) {
          std::vector<std::string> paths = getPathComponents(file.path);
          for (auto path : paths) {
            auto uuid = (*file.dirIDs).find(path)->second;
            auto d = getDirectoryMetadata(path, uuid);
            dirs.push_back(d);
          }
        } else {
          auto d = getDirectoryMetadata(f.path, file.dirID);
          dirs.push_back(d);
        }
        m_dbManager.insertFileWithDirectory(f, dirs);
      } else {
        std::filesystem::remove(fileAbsPath);
      }
    }
  }
}
void CloudSyncWorker::processFilesToDelete(
    const std::vector<FileMetadata> &filesToDeleteLocal) {
  for (auto &file : filesToDeleteLocal) {
    try {
      {
        std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
        std::filesystem::remove(file.absPath);
        m_dbManager.deleteFileByPath(file.path, file.filename);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] Exception:" << e.what()
                << " in deleting file->" << file.absPath << std::endl;
    }
  }
}

void CloudSyncWorker::processFoldersToCreate(
    const std::vector<LocalFolderCreateMetadata> &foldersToCreateLocal) {
  for (auto &folder : foldersToCreateLocal) {
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      try {
        if (!std::filesystem::exists(folder.absPath)) {
          std::cout << "[cloudsyncworker] creating path ..." << folder.absPath
                    << std::endl;
          std::filesystem::create_directories(folder.absPath);
        }
      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] unable to create path" << folder.absPath
                  << " | " << e.what() << std::endl;
        continue;
      }
      auto folderPaths = getPathComponents(folder.path);
      std::vector<DirectoryMetadata> dirs;
      for (auto &path : folderPaths) {
        auto uuid = (*folder.dirIDs).find(path)->second;
        auto d = getDirectoryMetadata(path, uuid);
        dirs.push_back(d);
      }
      auto result = m_dbManager.createDirectoryPaths(dirs);
      if (!result) {
        std::filesystem::remove(folder.absPath);
      }
    }
  }
}

void CloudSyncWorker::processFoldersToDelete(
    const std::vector<LocalFolderDeleteMetadata> &foldersToDeleteLocal) {
  for (auto &folder : foldersToDeleteLocal) {
  }
}

void CloudSyncWorker::processFilesToRename(
    const std::vector<LocalFileRenameMetadata> &filesToRename) {

  for (auto &file : filesToRename) {
  }
}

void CloudSyncWorker::processFilesInConflict(
    const std::vector<CloudFileMetadata> &filesInConflict) {
  for (auto &file : filesInConflict) {
  }
}

void CloudSyncWorker::processFilesToUpdate(
    const std::vector<CloudFileMetadata> &filesToUpdate) {

  for (auto &file : filesToUpdate) {
  }
}

void CloudSyncWorker::start() {
  m_stopThread = false;
  m_workerThread = std::thread(&CloudSyncWorker::run, this);
}

void CloudSyncWorker::run() {
  while (!m_stopThread) {
    pollCloudToSyncToLocal();
    // Poll every 30 seconds, or check stopThread more frequently
    for (int i = 0; i < 30 && !m_stopThread; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void CloudSyncWorker::stop() {
  m_stopThread = true;
  if (m_workerThread.joinable()) {
    m_workerThread.join();
  }
}
} // namespace sync_app
