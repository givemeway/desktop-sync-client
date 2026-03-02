#include "CloudSyncWorker.hpp"
#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "ReconciliationService.hpp"
#include "SyncWorker.hpp"
#include "ThreadPool.hpp"
#include "Utility.hpp"
#include "UuidUtils.hpp"
#include "types.hpp"
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
namespace fs = std::filesystem;

namespace sync_app {

CloudSyncWorker::CloudSyncWorker(

    DatabaseManager &dbManager, ApiClient &apiClient,
    ReconciliationService &reconcile, FileSystemScanner &scanner,
    SyncWorker &syncWorker, ThreadPool &uploadThreadPool,
    ThreadPool &downloadThreadPool, const std::string &syncPath,
    const std::string &userEmail)
    : m_dbManager(dbManager), m_apiClient(apiClient), m_reconcile(reconcile),
      m_scanner(scanner), m_uploadThreadPool(uploadThreadPool),
      m_downloadThreadPool(downloadThreadPool), m_syncWorker(syncWorker),
      m_syncPath(syncPath), m_userEmail(userEmail), m_stopThread(false) {}

CloudSyncWorker::~CloudSyncWorker() { stop(); }

bool CloudSyncWorker::pollCloudToSyncToLocal() {
  std::optional<CloudMetadataResult> result;
  std::optional<std::vector<FileMetadata>> dbFiles{std::nullopt};
  std::optional<std::vector<DirectoryMetadata>> dbDirs{std::nullopt};
  std::optional<std::vector<FileQueueEntry>> dbQFiles{std::nullopt};
  std::optional<std::vector<DirectoryQueueEntry>> dbQDirs{std::nullopt};
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    dbQFiles = m_dbManager.getAllQueueFiles();
    dbQDirs = m_dbManager.getAllQueueDirectories();
  }
  for (auto &dq : *dbQDirs) {
    if (dq.sync_status == syncStatusToString(SyncStatus::DELETE) ||
        dq.sync_status == syncStatusToString(SyncStatus::RENAME) ||
        dq.sync_status == syncStatusToString(SyncStatus::MOVED)) {
      return {};
    }
  }
  for (auto &fq : *dbQFiles) {
    if (fq.sync_status == syncStatusToString(SyncStatus::DELETE) ||
        fq.sync_status == syncStatusToString(SyncStatus::RENAME) ||
        fq.sync_status == syncStatusToString(SyncStatus::MOVED)) {
      return {};
    }
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    result = m_apiClient.getMetadata();
    dbFiles = m_dbManager.getAllFiles();
    dbDirs = m_dbManager.getAllDirectories();
  }

  if (result.has_value() && result->success) {
    std::vector<CloudFileMetadata> files = result->files;
    std::vector<CloudFolderMetadata> folders = result->directories;

    auto reconciledItems =
        m_reconcile.reconcile(files, folders, *dbFiles, *dbDirs);

    auto filesToDownload = reconciledItems.filesToDownload;
    auto filesToDeleteLocal = reconciledItems.filesToDeleteLocal;
    auto filesToMove = reconciledItems.filesToMove;
    auto dirsToMove = reconciledItems.dirsToMove;
    auto foldersToCreateLocal = reconciledItems.foldersToCreateLocal;
    auto foldersToDeleteLocal = reconciledItems.foldersToDeleteLocal;
    auto filesToUpdate = reconciledItems.filesToUpdate;
    auto filesInConflict = reconciledItems.filesInConflict;

    std::cout << "[cloudsyncworker] filesToDownload: " << filesToDownload.size()
              << "\n";
    std::cout << "[cloudsyncworker] filesToDelete: "
              << filesToDeleteLocal.size() << "\n";
    std::cout << "[cloudsyncworker] foldersToCreateLocal: "
              << foldersToCreateLocal.size() << "\n";
    std::cout << "[cloudsyncworker] foldersToDeleteLocal: "
              << foldersToDeleteLocal.size() << "\n";
    std::cout << "[cloudsyncworker] FilesToMove: " << filesToMove.size()
              << "\n";
    std::cout << "[cloudsyncworker] DirsToMove: " << dirsToMove.size() << "\n";

    std::cout << "[cloudsyncworker] filesInConflict: " << filesInConflict.size()
              << "\n";
    std::cout << "[cloudsyncworker] filesToUpdate: " << filesToUpdate.size()
              << "\n";

    processDirsToMove(dirsToMove);

    processFoldersToDelete(foldersToDeleteLocal);

    processFilesToMove(filesToMove);

    processFilesToDelete(filesToDeleteLocal);

    processFoldersToCreate(foldersToCreateLocal);

    processFilesToDownload(filesToDownload);

    processFilesInConflict(filesInConflict);

    processFilesToUpdate(filesToUpdate);

    return true;

  } else {

    return false;
  }
}

std::string CloudSyncWorker::getCurrentTime() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return std::to_string(timestamp);
}
std::vector<std::string>
CloudSyncWorker::getPathComponents(const std::string &path) {
  if (path == "/" || path.empty()) {
    return {"/"};
  }

  std::vector<std::string> pathTree;
  std::string currentPath = "";
  std::stringstream ss(path);
  std::string token;

  while (std::getline(ss, token, '/')) {
    if (token.empty())
      continue;
    currentPath += "/" + token;
    pathTree.push_back(currentPath);
  }

  if (pathTree.empty()) {
    return {"/"};
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
  f.lastSynced = getCurrentTime();
  return f;
}

FileMetadata
CloudSyncWorker::constructFileMetadata(const FileQueueEntry &file) {
  FileMetadata f;
  f.filename = file.filename;
  f.path = file.path;
  f.absPath = file.absPath;
  f.dirID = file.dirID;
  f.inode = file.inode;
  f.hashvalue = file.hashvalue;
  f.last_modified = file.last_modified;
  f.lastSyncedHashValue = file.lastSyncedHashValue;
  f.origin = file.origin;
  f.uuid = file.uuid;
  f.size = file.size;
  f.versions = file.versions;
  f.lastSynced = getCurrentTime();
  return f;
}

DirectoryMetadata
CloudSyncWorker::getDirectoryMetadata(const std::string &path,
                                      const std::string &uuid) {
  DirectoryMetadata d;
  std::string dirAbsPath;
  dirAbsPath = path == "/" ? m_syncPath : m_syncPath + path;
  pathParts p = m_dbManager.getFolderDevice(fs::path(path));
  d.device = p.device;
  d.absPath = dirAbsPath;
  d.folder = p.folder;
  d.path = path;
  d.lastSynced = getCurrentTime();
  try {
    d.created_at = std::to_string(
        m_scanner.getUnixTimeStamp(fs::last_write_time(dirAbsPath)));

  } catch (const std::exception &e) {
    std::cerr << "[cloudsyncwoker] error fetching dir timestamp: " << e.what()
              << "\n";
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
  int count = 0;
  m_tasksPending = filesToDownload.size();
  std::cout << "[cloudsyncworker] total TASKS: " << m_tasksPending << std::endl;
  std::vector<std::future<bool>> fileDownloadTasks;

  for (const auto &file : filesToDownload) {
    count++;
    // Clone the client for this task
    auto clientPtr = m_apiClient.clone();
    auto downloadFunc = [this, count, file,
                         client = std::move(clientPtr)]() mutable {
      std::string fileAbsPath(
          file.path == "/" ? m_syncPath + "/" + file.filename
                           : m_syncPath + file.path + "/" + file.filename);
      std::string fileDirPath(
          fs::path(fileAbsPath).parent_path().generic_string());

      // 1. Tell SyncWorker to ignore the event we are about to trigger
      m_syncWorker.addIgnoreEvent(fileAbsPath, WatchEvent::Added);

      // 2. Ensure parent directory exists
      std::vector<std::string> paths = getPathComponents(file.path);
      try {
        if (!fs::exists(fileDirPath)) {
          for (auto &p : paths) {
            auto fp = p == "/" ? m_syncPath : m_syncPath + p;
            if (!fs::exists(fp)) {
              m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
            }
          }
          fs::create_directories(fileDirPath);
        }
      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] Unable to create Directory ->"
                  << fileDirPath << " Error: " << e.what() << "\n";
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            m_syncWorker.removeIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        --m_tasksPending;
        std::cout << "[cloudsyncworker] tasks: " << m_tasksPending << std::endl;
        if (m_tasksPending == 0) {
          std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
          m_tasksCV.notify_all();
        }
        return false;
      }

      // 3. Download WITHOUT the database lock using CLONED client
      bool downloadStatus = client->downloadFile(file, fileAbsPath);

      // 4. Update Database WITH the lock
      pathParts fp = m_dbManager.getFolderDevice(fs::path(file.path));

      if (downloadStatus) {

        std::vector<DirectoryMetadata> dirs;
        FileMetadata f(getFileMetadata(file, fileAbsPath));
        std::optional<DirectoryMetadata> dirExists{std::nullopt};
        {
          std::lock_guard<std::recursive_mutex> lock(
              m_dbManager.getSyncMutex());
          dirExists =
              m_dbManager.getDirectoryByPath(fp.device, fp.folder, file.path);
        }

        if (!dirExists.has_value()) {
          std::vector<std::string> paths = getPathComponents(file.path);
          for (auto &path : paths) {
            std::string uuid = "";
            if (file.dirIDs && file.dirIDs->count(path)) {
              uuid = file.dirIDs->at(path);
            } else if (path == file.path) {
              uuid = file.dirID;
            }
            if (!uuid.empty()) {
              auto d = getDirectoryMetadata(path, uuid);
              dirs.push_back(d);
            }
          }
        } else {
          auto d = getDirectoryMetadata(file.path, file.dirID);
          dirs.push_back(d);
        }

        bool fileUpdateStatus = false;
        {
          std::lock_guard<std::recursive_mutex> lock(
              m_dbManager.getSyncMutex());
          fileUpdateStatus = m_dbManager.insertFileWithDirectory(f, dirs);
        }

        if (fileUpdateStatus) {
          --m_tasksPending;
          std::cout << "[cloudsyncworker] tasks: " << m_tasksPending
                    << std::endl;
          if (m_tasksPending == 0) {
            std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
            m_tasksCV.notify_all();
          }
          return true;
        } else {
          --m_tasksPending;
          std::cout << "[cloudsyncworker] tasks: " << m_tasksPending
                    << std::endl;
          if (m_tasksPending == 0) {
            std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
            m_tasksCV.notify_all();
          }
          return false;
        }
      } else {
        std::cout << "[cloudsyncworker] download failed.. deleting.."
                  << fileAbsPath << std::endl;
        if (fs::exists(fileAbsPath)) {
          try {
            m_syncWorker.addIgnoreEvent(fileAbsPath, WatchEvent::Deleted);
            fs::remove(fileAbsPath);
          } catch (const std::exception &e) {
            m_syncWorker.removeIgnoreEvent(fileAbsPath, WatchEvent::Deleted);
            std::cerr << "[cloudsyncworker] file deletion failed >>" << e.what()
                      << "\n";
          }
        }
        --m_tasksPending;
        std::cout << "[cloudsyncworker] tasks: " << m_tasksPending << std::endl;
        if (m_tasksPending == 0) {
          std::cout << "[cloudsyncworker] LAST TASK PROCESSED: " << std::endl;
          m_tasksCV.notify_all();
        }
        return false;
      }
    };
    m_downloadThreadPool.enqueue(std::move(downloadFunc));
    // auto future = m_downloadThreadPool.enqueue(std::move(downloadFunc));
    //     fileDownloadTasks.emplace_back(std::move(future));
  }
  /*for (auto &task : fileDownloadTasks) {
    task.get();
  }
  */
}

void CloudSyncWorker::processFilesToDelete(
    const std::vector<FileMetadata> &filesToDeleteLocal) {
  for (auto &file : filesToDeleteLocal) {
    try {
      m_syncWorker.addIgnoreEvent(file.absPath, WatchEvent::Deleted);
      fs::remove(file.absPath);
      {
        std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
        m_dbManager.deleteFileByPath(file.path, file.filename);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] Exception:" << e.what()
                << " in deleting file->" << file.absPath << "\n";
    }
  }
}

void CloudSyncWorker::processFoldersToCreate(
    const std::vector<LocalFolderCreateMetadata> &foldersToCreateLocal) {
  for (auto &folder : foldersToCreateLocal) {

    auto paths = getPathComponents(folder.path);
    try {
      if (!fs::exists(folder.absPath)) {
        std::cout << "[cloudsyncworker] creating path ..." << folder.absPath
                  << "\n";
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 1. Register ignore intent
            m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        fs::create_directories(folder.absPath);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] unable to create path" << folder.absPath
                << " | " << e.what() << "\n";
      for (auto &p : paths) {
        auto fp = p == "/" ? m_syncPath : m_syncPath + p;
        if (!fs::exists(fp)) {
          // 1. Register ignore intent
          m_syncWorker.removeIgnoreEvent(fp, WatchEvent::Added);
        }
      }
      continue;
    }

    // 2. Database operation (locked)
    auto folderPaths = getPathComponents(folder.path);
    std::vector<DirectoryMetadata> dirs;
    for (auto &path : folderPaths) {
      std::string uuid = "";
      if (folder.dirIDs && folder.dirIDs->count(path)) {
        uuid = folder.dirIDs->at(path);
      } else if (path == folder.path) {
        uuid = folder.uuid;
      }

      if (!uuid.empty()) {
        auto d = getDirectoryMetadata(path, uuid);
        dirs.push_back(d);
      }
    }
    bool result = false;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      result = m_dbManager.createDirectoryPaths(dirs);
    }
    if (!result) {
      try {
        std::cout << "[cloudsyncworker] dbcreation failed reverting... "
                  << "\n";
        m_syncWorker.addIgnoreEvent(folder.absPath, WatchEvent::Deleted);
        fs::remove(folder.absPath);

      } catch (const std::exception &e) {
        m_syncWorker.removeIgnoreEvent(folder.absPath, WatchEvent::Deleted);
        std::cerr << "[cloudsyncworker] " << e.what() << "\n";
      }
    }
  }
}

void CloudSyncWorker::processFoldersToDelete(
    const std::vector<LocalFolderDeleteMetadata> &foldersToDeleteLocal) {
  for (auto &folder : foldersToDeleteLocal) {
    // 1. Tell SyncWorker to ignore
    m_syncWorker.addIgnoreEvent(folder.absPath, WatchEvent::Deleted);
    try {
      if (fs::exists(folder.absPath)) {
        fs::remove_all(folder.absPath);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] exception: " << e.what()
                << " | unable to delete folder: " << folder.path << "\n";
      m_syncWorker.removeIgnoreEvent(folder.absPath, WatchEvent::Deleted);
      continue;
    }
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      m_dbManager.deleteDirectory(folder.path);
    }
  }
}
void CloudSyncWorker::processDirsToMove(
    const std::vector<DirectoryQueueEntry> &dirsToMove) {

  for (auto &d : dirsToMove) {
    std::string oldAbsPath;
    std::string newAbsPath;
    oldAbsPath = *d.old_path == "/" ? m_syncPath : m_syncPath + *d.old_path;
    newAbsPath = d.path == "/" ? m_syncPath : m_syncPath + d.path;
    try {

      if (fs::exists(oldAbsPath)) {
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        fs::rename(oldAbsPath, newAbsPath);
      }

    } catch (const std::exception &e) {
      m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
      std::cout << "[cloudsyncworker] move failed: " << e.what() << std::endl;
      std::cout << "[cloudsyncworker] newPath : " << d.path
                << " |  oldPath : " << *d.old_path << std::endl;
      continue;
    }
    bool isMoved;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      isMoved = m_dbManager.moveDirectory(d.path, *d.old_path, d, false);
    }
    if (isMoved) {
      std::cout << "[cloudsyncwoker] MOVE SUCCESSFULL :" << std::endl;
      std::cout << "[cloudsyncwoker] OLD: " << *d.old_path
                << " | NEW: " << d.path << std::endl;
    } else {
      std::cout << "[cloudsyncwoker] MOVE FAILED :" << std::endl;
      std::cout << "[cloudsyncwoker] OLD: " << *d.old_path
                << " | NEW: " << d.path << std::endl;
    }
  }
}

void CloudSyncWorker::processFilesToMove(
    const std::vector<FileQueueEntry> &filesToMove) {
  for (auto &file : filesToMove) {

    std::string oldRelPath{*file.old_path};
    std::string newRelPath{file.path};
    std::string oldAbsPath{file.old_path == "/"
                               ? m_syncPath + "/" + *file.old_filename
                               : m_syncPath + *file.old_path + "/" +
                                     *file.old_filename};
    std::string newAbsPath{file.absPath};

    newAbsPath = file.path == "/"
                     ? m_syncPath + "/" + file.filename
                     : m_syncPath + file.path + "/" + file.filename;
    try {
      if (fs::exists(oldAbsPath)) {
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        fs::rename(oldAbsPath, newAbsPath);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] unable to rename from ->"
                << *file.old_filename << " to->" << file.filename << "\n";
      m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
      m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
      continue;
    }
    std::string path = *file.old_path;
    std::string filename = *file.old_filename;
    FileMetadata f(Utility::convert<FileMetadata>(file));
    bool result = false;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      result = m_dbManager.updateFileWithTransaction(f, path, filename);
    }
    if (!result) {
      try {
        std::cout << "[cloudsyncworker] file update failed in DB. Renaming "
                     "the file back to its origin name"
                  << "\n";
        if (fs::exists(newAbsPath)) {
          m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Moved);
          m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Moved);
          fs::rename(newAbsPath, oldAbsPath);
        }
      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] unable to rename" << e.what()
                  << oldAbsPath << "\n";
        m_syncWorker.removeIgnoreEvent(oldAbsPath, WatchEvent::Moved);
        m_syncWorker.removeIgnoreEvent(oldAbsPath, WatchEvent::Moved);
      }
    }
  }
}

void CloudSyncWorker::processFilesInConflict(
    const std::vector<CloudFileMetadata> &filesInConflict) {
  for (auto &file : filesInConflict) {
    // rename the local file
    // download the file from the cloud
    // add events to ignore list to prevent adding it into the queue.
    // let the renamed file be added as new file in the queue so it can be
    // uploaded to cloud later.
    // 1. creating conflict name for the file
    auto timestamp = getCurrentTime();
    std::string absPath = file.path == "/"
                              ? m_syncPath + "/" + file.filename
                              : m_syncPath + file.path + "/" + file.filename;
    fs::path p(absPath);
    std::string ext = p.extension().string();
    std::string stem = p.stem().string();
    std::string conflictName = stem + "( conflict copy ) - " + timestamp + ext;
    std::string newAbsPath = file.path == "/"
                                 ? m_syncPath + "/" + conflictName
                                 : m_syncPath + file.path + "/" + conflictName;
    std::string dirPath = fs::path(absPath).parent_path().string();
    std::vector<std::string> paths = getPathComponents(dirPath);
    if (fs::exists(absPath)) {
      // 2. ignoring the events created for file rename
      try {
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        fs::rename(absPath, newAbsPath);

      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] unable to rename from " << absPath
                  << "-> to ->" << newAbsPath << std::endl;
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Moved);
        continue;
      }
    }
    // 3.add the conflicted file into the file table ( main & queue)
    if (!fs::exists(dirPath)) {
      try {
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 4. ignore the folder creation events that would be triggered
            // while creating directory
            m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        fs::create_directories(dirPath);

      } catch (const std::exception &e) {
        std::cout << "[cloudsyncworker] unable to create directory " << dirPath
                  << std::endl;
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 4. ignore the folder creation events that would be triggered
            // while creating directory
            m_syncWorker.removeIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        continue;
      }
    }
    // 5. add an event to ignore the file that is being downloaded
    m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Added);
    auto result = m_apiClient.downloadFile(file, absPath);
    if (!result) {
      if (fs::exists(absPath)) {
        try {
          // 6. if the file download fails, remove its state entry in FS &
          // ignore the delete event
          m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
          fs::remove(absPath);

        } catch (const std::exception &e) {
          std::cerr << "[cloudsyncworker] unable to delete the file that "
                       "failed to download->"
                    << absPath << std::endl;
          m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
        }
      }
      continue;
    }
    // 7. lock the syncworker until the DB is updated for the file addition
    std::vector<std::string> paths_ = getPathComponents(file.path);
    std::vector<DirectoryMetadata> dirs;
    // 8. getting the folder  components for the file path that needs to be
    // inserted into Directory DB.
    for (auto &path : paths_) {
      DirectoryMetadata d;
      std::string uuid = "";
      if (file.dirIDs && file.dirIDs->count(path)) {
        uuid = file.dirIDs->at(path);
      } else if (path == file.path) {
        uuid = file.dirID;
      }

      if (!uuid.empty()) {
        d = getDirectoryMetadata(path, uuid);
        dirs.push_back(d);
      }
    }

    FileMetadata cloudFile(getFileMetadata(file, absPath));
    FileMetadata conflictedFile(getFileMetadata(file, newAbsPath));

    conflictedFile.filename = conflictName;
    conflictedFile.versions = 1;
    conflictedFile.hashvalue = m_scanner.calculateHash(newAbsPath);
    conflictedFile.lastSyncedHashValue = conflictedFile.hashvalue;
    conflictedFile.inode = m_scanner.getInode(newAbsPath);
    conflictedFile.size = fs::file_size(newAbsPath);
    conflictedFile.lastSynced = getCurrentTime();
    conflictedFile.last_modified = std::to_string(
        m_scanner.getUnixTimeStamp(fs::last_write_time(newAbsPath)));
    conflictedFile.origin = conflictedFile.uuid = UuidUtils::generate();
    DirectoryMetadata d = getDirectoryMetadata(file.path, file.dirID);
    DirectoryQueueEntry conflictedDirQueue{
        Utility::constructDirectoryQueueEntry(d)};

    FileQueueEntry fq;
    fq = Utility::constructFileQueueEntry(conflictedFile, SyncStatus::NEW);
    conflictedDirQueue.sync_status =
        syncStatusToString(SyncStatus::FILE_LINKED);
    // 9. update the DB for the file that was downloaded
    bool result_ = false;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      result_ = m_dbManager.insertFileAndQueueWithDirectory(
          cloudFile, conflictedFile, fq, conflictedDirQueue, dirs);
    }
    if (!result_) {
      try {
        // 10. if the DB update fails, remove the file that was downloaded
        // to ensure integrity
        m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Deleted);
        if (fs::exists(absPath)) {
          // 11. ignore the event for the file removed from FS.
          fs::remove(absPath);
        }
        if (fs::exists(newAbsPath)) {
          fs::remove(newAbsPath);
        }
      } catch (const std::exception &e) {
        m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Deleted);
        m_syncWorker.removeIgnoreEvent(newAbsPath, WatchEvent::Deleted);
      }
      continue;
    }
  }
}

void CloudSyncWorker::processFilesToUpdate(
    const std::vector<CloudFileMetadata> &filesToUpdate) {
  for (auto &file : filesToUpdate) {
    std::string absPath = file.path == "/"
                              ? m_syncPath + file.filename
                              : m_syncPath + file.path + "/" + file.filename;
    m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);
    bool result = m_apiClient.downloadFile(file, absPath);
    if (result) {
      if (!result) {
        // we need a way to revert the file to its previous state
        // or retry downloading the file again.
        continue;
      }
      FileMetadata f(getFileMetadata(file, absPath));
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      bool isUpdated =
          m_dbManager.updateFileWithTransaction(f, file.path, file.filename);
      if (!isUpdated) {
        // if we are here it means the file update on FS is success! and only
        // the db update failed retry updating the DB again
        continue;
      }
    }
  }
}

std::optional<bool>
CloudSyncWorker::deleteFolderInCloud(DirectoryQueueEntry &dq) {
  // delete folder
  std::cout << "[cloudsyncworker] device: " << dq.device
            << " folder: " << dq.folder << " path: " << dq.path << std::endl;
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto d = m_dbManager.getDirectoryByPath(dq.device, dq.folder, dq.path);
    if (d.has_value()) {
      std::cout << "[cloudsyncwoker] Dir To delete exists locally:" << dq.path
                << " \n";
      return std::nullopt;
    }
  }
  bool isCreated = m_apiClient.deleteFolder(dq);
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto isQRemoved =
        m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
    if (!isQRemoved) {
      // retry
      std::cout << "[cloudsyncwoker] DirQueue delete failed" << dq.path
                << " \n";
      return std::nullopt;
    }
  }
  if (!isCreated) {
    m_syncWorker.pushDirEntry(dq);
    std::cout << "[cloudsyncworker] Error deleting folder -> " << dq.path
              << " in cloud" << "\n";
    return false;
  }

  std::cout << "[cloudsyncworker] folder -> " << dq.path << " deleted in cloud"
            << "\n";
  return true;
}

std::optional<bool> CloudSyncWorker::moveFolderInCloud(DirectoryQueueEntry &dq,
                                                       bool isRename) {
  bool isRenamed = m_apiClient.moveFolder(dq, isRename);
  if (!isRenamed) {
    std::cout << "[cloudsyncworker] Error renaming -> " << *dq.old_path
              << " => " << dq.path << " in cloud" << "\n";
    m_syncWorker.pushDirEntry(dq);
    return false;
  }
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto isQRemoved =
        m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
    if (!isQRemoved) {
      std::cout << "[cloudsyncworker] DirQueue delete failed:  " << *dq.old_path
                << std::endl;
      return std::nullopt;
    }
  }
  std::cout << "[cloudsyncworker] folder =>" << *dq.old_path
            << " renamed in cloud to =>" << dq.path << "\n";
  return true;
}

std::optional<bool>
CloudSyncWorker::createFolderInCloud(DirectoryQueueEntry &dq) {
  // create folder
  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto d = m_dbManager.getDirectoryByPath(dq.device, dq.folder, dq.path);
    if (!d.has_value()) {
      std::cout << "[cloudsyncworker] Dir: " << dq.path
                << " not found in Directory Table \n";
      return std::nullopt;
    }
  }

  bool isCreated = m_apiClient.createFolder(dq);
  if (!isCreated) {
    std::cout << "[cloudsyncworker] Error creating folder -> " << dq.path
              << " in cloud" << "\n";
    m_syncWorker.pushDirEntry(dq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    auto isQRemoved =
        m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
    if (!isQRemoved) {
      std::cerr << "[cloudsyncworker] DirQueue delete failed \n";
      return std::nullopt;
    }
  }

  std::cout << "[cloudsyncworker] folder -> " << dq.path
            << " created in cloud \n";
  return true;
}

std::optional<bool> CloudSyncWorker::uploadFile(ApiClient &client,
                                                const FileQueueEntry &fq) {

  std::vector<DirectoryMetadata> dirTree;
  std::vector<std::string> pathComponents = getPathComponents(fq.path);

  for (auto &path : pathComponents) {
    pathParts p = m_dbManager.getFolderDevice(fs::path(path));
    std::optional<DirectoryMetadata> dir{std::nullopt};
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      dir = m_dbManager.getDirectoryByPath(p.device, p.folder, path);
    }
    if (dir.has_value()) {
      dirTree.push_back(*dir);
    } else {
      return false;
    }
  }

  FileMetadata f = constructFileMetadata(fq);
  bool isUploaded = client.uploadFile(fq, dirTree);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] file upload failed: " << fq.filename
              << "\n";
    m_syncWorker.pushFileEntry(fq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed" << fq.filename
              << "\n";
    return std::nullopt;
  }

  std::cout << "[cloudsyncworker] file->" << fq.filename << " uploaded to cloud"
            << "\n";
  --m_upSyncTasks;
  if (m_upSyncTasks == 0) {
    m_upSyncTasksCV.notify_all();
  }
  return true;
}

std::optional<bool>
CloudSyncWorker::uploadModifiedFile(ApiClient &client,
                                    const FileQueueEntry &fq) {
  // update file
  bool isUploaded = client.uploadFile(fq, {}, true);
  FileMetadata f = constructFileMetadata(fq);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] unable to update the file: " << fq.absPath
              << "\n";
    m_syncWorker.pushFileEntry(fq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed" << fq.filename
              << "\n";
    return std::nullopt;
  }

  std::cout << "[cloudsyncworker] file->" << fq.filename << " uploaded to cloud"
            << "\n";
  --m_upSyncTasks;
  if (m_upSyncTasks == 0) {
    m_upSyncTasksCV.notify_all();
  }
  return true;
}

std::optional<bool> CloudSyncWorker::deleteFile(ApiClient &client,
                                                const FileQueueEntry &fq) {
  // delete file
  bool isUploaded = client.deleteFile(fq);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] unable to delete file in cloud: "
              << fq.absPath << "\n";
    m_syncWorker.pushFileEntry(fq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (isFileUpdated) {
    std::cout << "[cloudsyncworker] fileQueue delete failed" << fq.absPath
              << "\n";
    return std::nullopt;
  }

  std::cout << "[cloudsyncworker] file->" << fq.absPath
            << " deleted from  cloud" << "\n";
  --m_upSyncTasks;
  if (m_upSyncTasks == 0) {
    m_upSyncTasksCV.notify_all();
  }
  return true;
}

std::optional<bool> CloudSyncWorker::renameFile(ApiClient &client,
                                                const FileQueueEntry &fq) {
  bool isUploaded = client.renameFile(fq);
  FileMetadata f = constructFileMetadata(fq);
  bool isFileUpdated = false;

  if (!isUploaded) {
    std::cout << "[cloudsyncworker] unable to rename file->" << *fq.old_filename
              << " to ->" << fq.filename << "\n";
    m_syncWorker.pushFileEntry(fq);
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    isFileUpdated = m_dbManager.deleteFileQueue(fq.path, fq.filename);
  }

  if (!isFileUpdated) {
    std::cout << "[cloudsyncworker] unable to delete fileQueue"
              << *fq.old_filename << " to ->" << fq.filename << "\n";
    return std::nullopt;
  }

  std::cout << "[cloudsyncworker] file->" << *fq.old_filename << " renamed to->"
            << fq.filename << " in cloud "
            << "\n";
  --m_upSyncTasks;
  if (m_upSyncTasks == 0) {
    m_upSyncTasksCV.notify_all();
  }
  return true;
}

void CloudSyncWorker::processQueueToSyncUp() {
  std::vector<std::future<bool>> syncUpTasks;

  while (!m_syncWorker.dirIsEmpty() || !m_syncWorker.fileIsEmpty()) {
    DirectoryQueueEntry dq;
    FileQueueEntry fq;
    if (!m_syncWorker.dirIsEmpty()) {
      dq = m_syncWorker.topDirEntry();
      m_syncWorker.popDirEntry();

      if (dq.sync_status == syncStatusToString(SyncStatus::DELETE)) {
        deleteFolderInCloud(dq);
      }
      if (dq.sync_status == syncStatusToString(SyncStatus::RENAME)) {
        moveFolderInCloud(dq, true);
      }
      if (dq.sync_status == syncStatusToString(SyncStatus::MOVED)) {
        moveFolderInCloud(dq, false);
      }
      if (dq.sync_status == syncStatusToString(SyncStatus::NEW)) {
        createFolderInCloud(dq);
      }
    }
    if (!m_syncWorker.fileIsEmpty()) {
      fq = m_syncWorker.topFileEntry();
      m_syncWorker.popFileEntry();
      auto clientPtr = m_apiClient.clone();

      auto uploadFunc = [this, fq, client = std::move(clientPtr)]() mutable {
        if (fq.sync_status == syncStatusToString(SyncStatus::DELETE)) {
          ++m_upSyncTasks;
          deleteFile(*client, fq);
        }
        if (fq.sync_status == syncStatusToString(SyncStatus::MOVED)) {
          ++m_upSyncTasks;
          renameFile(*client, fq);
        }
        if (fq.sync_status == syncStatusToString(SyncStatus::RENAME)) {
          ++m_upSyncTasks;
          renameFile(*client, fq);
        }
        if (fq.sync_status == syncStatusToString(SyncStatus::NEW)) {
          ++m_upSyncTasks;
          uploadFile(*client, fq);
        }
        if (fq.sync_status == syncStatusToString(SyncStatus::MODIFIED)) {
          ++m_upSyncTasks;
          uploadModifiedFile(*client, fq);
        }
      };
      //    auto future = m_uploadThreadPool.enqueue(std::move(uploadFunc));
      m_uploadThreadPool.enqueue(std::move(uploadFunc));
      //  syncUpTasks.emplace_back(std::move(future));
    }
  }

  std::optional<std::vector<FileQueueEntry>> queueFiles{std::nullopt};
  std::optional<std::vector<DirectoryQueueEntry>> queueDirs{std::nullopt};

  {
    std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
    queueFiles = m_dbManager.getAllQueueFiles();
    queueDirs = m_dbManager.getAllQueueDirectories();
  }

  if (queueFiles.has_value() && queueDirs.has_value()) {

    auto localDirs = *queueDirs;
    auto localFiles = *queueFiles;
    for (auto &dq : localDirs) {

      if (dq.sync_status == syncStatusToString(SyncStatus::FILE_LINKED)) {
        pathParts p = m_dbManager.getFolderDevice(fs::path(dq.path));
        std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
        m_dbManager.deleteDirectoryQueue(p.device, p.folder, dq.path);
      }
    }
  }
};

void CloudSyncWorker::start() {
  m_stopThread = false;
  m_uploadThread = std::thread(&CloudSyncWorker::runSyncDown, this);
  m_downloadThread = std::thread(&CloudSyncWorker::runSyncUp, this);
}

void CloudSyncWorker::runSyncDown() {
  while (!m_stopThread) {
    std::cout << "[cloudsyncworker] processing CLOUD queue..." << std::endl;

    pollCloudToSyncToLocal();
    std::unique_lock<std::mutex> lock(m_tasksPendingMutex);
    m_tasksCV.wait(lock,
                   [this] { return m_tasksPending == 0 || m_stopThread; });
    for (int i = 0; i < 5 && !m_stopThread; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void CloudSyncWorker::runSyncUp() {
  while (!m_stopThread) {

    std::cout << "[cloudsyncworker] processing LOCAL queue..." << std::endl;

    processQueueToSyncUp();

    std::unique_lock<std::mutex> lock(m_upSyncTasksMutex);

    m_upSyncTasksCV.wait(
        lock, [this]() { return m_upSyncTasks == 0 || m_stopThread; });

    for (int i = 0; i < 5 && !m_stopThread; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void CloudSyncWorker::controlThread() {
  // reconcile
}

void CloudSyncWorker::stop() {
  m_stopThread = true;
  if (m_workerThread.joinable()) {
    m_workerThread.join();
  }
}
} // namespace sync_app
