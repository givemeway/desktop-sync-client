#include "CloudSyncWorker.hpp"
#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "ReconciliationService.hpp"
#include "SyncWorker.hpp"
#include "UuidUtils.hpp"
#include "types.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace fs = std::filesystem;

namespace sync_app {

CloudSyncWorker::CloudSyncWorker(DatabaseManager &dbManager,
                                 ApiClient &apiClient,
                                 ReconciliationService &reconcile,
                                 FileSystemScanner &scanner,
                                 SyncWorker &syncWorker,
                                 const std::string &syncPath,
                                 const std::string &userEmail)
    : m_dbManager(dbManager), m_apiClient(apiClient), m_reconcile(reconcile),
      m_scanner(scanner), m_syncWorker(syncWorker), m_syncPath(syncPath),
      m_userEmail(userEmail), m_stopThread(false) {}

CloudSyncWorker::~CloudSyncWorker() { stop(); }

bool CloudSyncWorker::pollCloudToSyncToLocal() {
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
    std::cout << "[cloudsyncworker] filesToDownload: " << filesToDownload.size()
              << std::endl;
    std::cout << "[cloudsyncworker] filesToDelete: "
              << filesToDeleteLocal.size() << std::endl;
    std::cout << "[cloudsyncworker] foldersToCreateLocal: "
              << foldersToCreateLocal.size() << std::endl;
    std::cout << "[cloudsyncworker] foldersToDeleteLocal: "
              << foldersToDeleteLocal.size() << std::endl;
    std::cout << "[cloudsyncworker] processFilesToRename: "
              << filesToRename.size() << std::endl;
    std::cout << "[cloudsyncworker] filesInConflict: " << filesInConflict.size()
              << std::endl;
    std::cout << "[cloudsyncworker] filesToUpdate: " << filesToUpdate.size()
              << std::endl;
    std::cout << "[cloudsyncworker] Calling processFilesToDownload..."
              << std::endl;
    processFilesToDownload(filesToDownload);
    std::cout << "[cloudsyncworker] Calling processFilesToDelete..."
              << std::endl;
    processFilesToDelete(filesToDeleteLocal);
    std::cout << "[cloudsyncworker] Calling processFoldersToCreate..."
              << std::endl;
    processFoldersToCreate(foldersToCreateLocal);
    std::cout << "[cloudsyncworker] Calling processFoldersToDelete..."
              << std::endl;
    processFoldersToDelete(foldersToDeleteLocal);
    std::cout << "[cloudsyncworker] Calling processFilesToRename..."
              << std::endl;
    processFilesToRename(filesToRename);
    std::cout << "[cloudsyncworker] Calling processFilesInConflict..."
              << std::endl;
    processFilesInConflict(filesInConflict);
    std::cout << "[cloudsyncworker] Calling processFilesToUpdate..."
              << std::endl;
    processFilesToUpdate(filesToUpdate);
    std::cout
        << "[cloudsyncworker] pollCloudToSyncToLocal finished successfully."
        << std::endl;
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
  int count = 0;
  for (auto &file : filesToDownload) {
    count++;
    std::cout << "[cloudsyncworker] [" << count << "/" << filesToDownload.size()
              << "] Processing download for: " << file.filename << " in "
              << file.path << std::endl;

    std::string fileAbsPath(file.path == "/"
                                ? m_syncPath + "/" + file.filename
                                : m_syncPath + file.path + "/" + file.filename);
    std::string fileDirPath(
        fs::path(fileAbsPath).parent_path().generic_string());

    // 1. Tell SyncWorker to ignore the event we are about to trigger when
    // download is initiated
    m_syncWorker.addIgnoreEvent(fileAbsPath, WatchEvent::Added);

    // 2. Ensure parent directory exists
    try {
      if (!fs::exists(fileDirPath)) {
        std::vector<std::string> paths = getPathComponents(file.path);
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 1. Register ignore intent
            m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        fs::create_directories(fileDirPath);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] Unable to create Directory ->"
                << fileDirPath << " Error: " << e.what() << std::endl;
      // Note: We don't remove from ignoreMap because if the event never
      // triggers, the token just stays there until consumed or timed out
      // TODO: create a method removeIgnoreEvent - to remove stale event that
      // never triggered
      continue;
    }

    // 3. Download WITHOUT the database lock
    bool downloadStatus = m_apiClient.downloadFile(file, fileAbsPath);

    // 4. Update Database WITH the lock
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      pathParts fp = m_dbManager.getFolderDevice(fs::path(file.path));
      if (downloadStatus) {
        std::vector<DirectoryMetadata> dirs;
        FileMetadata f(getFileMetadata(file, fileAbsPath));
        auto dirExists =
            m_dbManager.getDirectoryByPath(fp.device, fp.folder, file.path);
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
        m_dbManager.insertFileWithDirectory(f, dirs);
      } else {
        std::cout << "[cloudsyncworker] download failed.. deleting.."
                  << fileAbsPath << std::endl;
        fs::remove(fileAbsPath);
      }
    }
  }
}

void CloudSyncWorker::processFilesToDelete(
    const std::vector<FileMetadata> &filesToDeleteLocal) {
  for (auto &file : filesToDeleteLocal) {
    try {
      m_syncWorker.addIgnoreEvent(file.absPath, WatchEvent::Added);
      {
        std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
        fs::remove(file.absPath);
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

    try {
      if (!fs::exists(folder.absPath)) {
        std::cout << "[cloudsyncworker] creating path ..." << folder.absPath
                  << std::endl;
        auto paths = getPathComponents(folder.path);
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
                << " | " << e.what() << std::endl;
      continue;
    }

    // 2. Database operation (locked)
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
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
      auto result = m_dbManager.createDirectoryPaths(dirs);
      if (!result) {
        std::cout << "[cloudsyncworker] dbcreation failed reverting... "
                  << std::endl;
        fs::remove(folder.absPath);
      }
    }
  }
}

void CloudSyncWorker::processFoldersToDelete(
    const std::vector<LocalFolderDeleteMetadata> &foldersToDeleteLocal) {
  for (auto &folder : foldersToDeleteLocal) {
    // 1. Tell SyncWorker to ignore
    m_syncWorker.addIgnoreEvent(folder.absPath, WatchEvent::Deleted);
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      try {
        if (fs::exists(folder.absPath)) {
          fs::remove_all(folder.absPath);
        }
      } catch (const std::exception &e) {
        std::cerr << "[cloudsyncworker] exception: " << e.what()
                  << " | unable to delete folder: " << folder.path << std::endl;
        continue;
      }
      m_dbManager.deleteDirectory(folder.path);
    }
  }
}

void CloudSyncWorker::processFilesToRename(
    const std::vector<LocalFileRenameMetadata> &filesToRename) {
  for (auto &file : filesToRename) {
    FileMetadata of(file.oldFile);
    CloudFileMetadata nf(file.newFile);
    std::string oldRelPath(of.path);
    std::string newRelPath(nf.path);
    std::string oldAbsPath(of.absPath);
    std::string newAbsPath;
    newAbsPath = nf.path == "/" ? m_syncPath + "/" + nf.filename
                                : m_syncPath + nf.path + "/" + nf.filename;
    try {
      if (fs::exists(oldAbsPath)) {
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        fs::rename(oldAbsPath, newAbsPath);
      }
    } catch (const std::exception &e) {
      std::cerr << "[cloudsyncworker] unable to rename from ->" << of.filename
                << " to->" << nf.filename << std::endl;
      continue;
    }
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      std::string path = of.path;
      std::string filename = of.filename;
      of.filename = nf.filename;
      of.path = nf.path;
      of.absPath = nf.path == "/" ? m_syncPath + "/" + nf.filename
                                  : m_syncPath + nf.path + "/" + nf.filename;
      of.hashvalue = nf.hashvalue;
      of.lastSyncedHashValue = nf.lastSyncedHashValue;
      of.versions = nf.versions;
      of.last_modified = nf.last_modified;
      of.dirID = nf.dirID;
      auto result = m_dbManager.updateFileWithTransaction(of, path, filename);
      if (!result) {
        try {
          std::cout << "[cloudsyncworker] file update failed in DB. Renaming "
                       "the file back to its origin name"
                    << std::endl;
          if (fs::exists(newAbsPath)) {
            m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Moved);
            m_syncWorker.addIgnoreEvent(oldAbsPath, WatchEvent::Moved);
            fs::rename(newAbsPath, oldAbsPath);
          }
        } catch (const std::exception &e) {
          std::cerr << "[cloudsyncworker] unable to rename" << e.what()
                    << oldAbsPath << std::endl;
        }
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
    try {
      if (fs::exists(absPath)) {
        // 2. ignoring the events created for file rename
        m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Moved);
        fs::rename(absPath, newAbsPath);
      }
      // 3.add the conflicted file into the file table ( main & queue)
      std::string dirPath = fs::path(absPath).parent_path().string();
      if (!fs::exists(dirPath)) {
        std::vector<std::string> paths = getPathComponents(dirPath);
        for (auto &p : paths) {
          auto fp = p == "/" ? m_syncPath : m_syncPath + p;
          if (!fs::exists(fp)) {
            // 4. ignore the folder creation events that would be triggered
            // while creating directory
            m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
          }
        }
        fs::create_directories(dirPath);
      }
      // 5. add an event to ignore the file that is being downloaded
      m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Added);
      auto result = m_apiClient.downloadFile(file, absPath);
      if (!result) {
        if (fs::exists(absPath)) {
          // 6. if the file download fails, remove its state entry in FS &
          // ignore the delete event
          m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
          fs::remove(absPath);
        }
        continue;
      }
      {
        // 7. lock the syncworker until the DB is updated for the file addition
        std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
        std::vector<std::string> paths = getPathComponents(file.path);
        std::vector<DirectoryMetadata> dirs;
        // 8. getting the folder  components for the file path that needs to be
        // inserted into Directory DB.
        for (auto &path : paths) {
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
        DirectoryQueueEntry conflictedDirQueue(
            getDirectoryMetadata(file.path, file.dirID));
        FileQueueEntry fq;
        fq = FileMetadata(conflictedFile);
        fq.sync_status = syncStatusToString(SyncStatus::NEW);
        conflictedDirQueue.sync_status =
            syncStatusToString(SyncStatus::FILE_LINKED);
        // 9. update the DB for the file that was downloaded
        bool result = m_dbManager.insertFileAndQueueWithDirectory(
            cloudFile, conflictedFile, fq, conflictedDirQueue, dirs);
        if (!result) {
          // 10. if the DB update fails, remove the file that was downloaded to
          // ensure integrity
          if (fs::exists(absPath)) {
            // 11. ignore the event for the file removed from FS.
            m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Deleted);
            fs::remove(absPath);
          }
          if (fs::exists(newAbsPath)) {
            m_syncWorker.addIgnoreEvent(newAbsPath, WatchEvent::Deleted);
            fs::remove(newAbsPath);
          }
          continue;
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[syncworker] exception->" << e.what() << std::endl;
    }
  }
}

void CloudSyncWorker::processFilesToUpdate(
    const std::vector<CloudFileMetadata> &filesToUpdate) {
  for (auto &file : filesToUpdate) {
    std::string absPath = file.path == "/"
                              ? m_syncPath + file.filename
                              : m_syncPath + file.path + "/" + file.filename;
    {
      std::lock_guard<std::recursive_mutex> lock(m_dbManager.getSyncMutex());
      m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);
      bool result = m_apiClient.downloadFile(file, absPath);
      if (!result) {
        // we need a way to revert the file to its previous state
        // or retry downloading the file again.
        continue;
      }
      FileMetadata f(getFileMetadata(file, absPath));
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

void CloudSyncWorker::processQueueToSyncUp() {
  auto queueFiles = m_dbManager.getAllQueueFiles();
  auto queueDirs = m_dbManager.getAllQueueDirectories();
  if (queueFiles.has_value() && queueDirs.has_value()) {
    for (auto &dq : *queueDirs) {
      if (dq.sync_status == syncStatusToString(SyncStatus::NEW)) {
        // upload file
        auto d = m_dbManager.getDirectoryByPath(dq.device, dq.folder, dq.path);
        bool isCreated = m_apiClient.createFolder(*d);
        if (!isCreated) {
          // retry;
          std::cout << "[cloudsyncworker] Error creating folder -> " << dq.path
                    << " in cloud" << std::endl;
          continue;
        }
        auto isQRemoved =
            m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
        DirectoryMetadata dir = getDirectoryMetadata(dq.path, dq.uuid);
        auto isDirUpdated = m_dbManager.updateDirectory(dir);
        if (!isQRemoved || !isDirUpdated) {
          // retry
          std::cerr << "[cloudsyncworker] directory and queue update failed"
                    << std::endl;
        }
        std::cout << "[cloudsyncworker] folder -> " << dq.path
                  << " created in cloud" << std::endl;
      }
      if (dq.sync_status == syncStatusToString(SyncStatus::RENAME)) {
        // rename file
        auto d =
            m_dbManager.getDirectoryQueueByPath(dq.device, dq.folder, dq.path);
        bool isRenamed = m_apiClient.renameFolder(*d);
        DirectoryMetadata dir = getDirectoryMetadata(dq.path, dq.uuid);
        auto isDirUpdated = m_dbManager.updateDirectory(dir);
        if (!isRenamed || isDirUpdated) {
          // retry;
          std::cout << "[cloudsyncworker] Error renaming -> " << *d->old_path
                    << " => " << d->path << "in cloud" << std::endl;
          continue;
        }
        auto isQRemoved =
            m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
        if (!isQRemoved) {
          // retry
        }
        std::cout << "[cloudsyncworker] folder =>" << *d->old_path
                  << " renamed in cloud to =>" << d->path << std::endl;
      }
      if (dq.sync_status == syncStatusToString(SyncStatus::DELETE)) {
        // delete file
        auto d = m_dbManager.getDirectoryByPath(dq.device, dq.folder, dq.path);
        bool isCreated = m_apiClient.deleteFolder(*d);
        if (!isCreated) {
          // retry;
          std::cout << "[cloudsyncworker] Error deleting folder -> " << dq.path
                    << " in cloud" << std::endl;
          continue;
        }
        auto isQRemoved =
            m_dbManager.deleteDirectoryQueue(dq.device, dq.folder, dq.path);
        if (!isQRemoved) {
          // retry
        }
        std::cout << "[cloudsyncworker] folder -> " << dq.path
                  << " deleted in cloud" << std::endl;
      }
    }
    for (auto &fq : *queueFiles) {
      if (fq.sync_status == syncStatusToString(SyncStatus::NEW)) {
        // upload file
        std::optional<std::vector<DirectoryMetadata>> dirs =
            m_dbManager.getDirsByPath(fq.path);
        bool isUploaded = m_apiClient.uploadFile(fq, *dirs);
        FileMetadata f = constructFileMetadata(fq);
        bool isFileUpdated = m_dbManager.updateFile(f);
        if (!isUploaded || !isFileUpdated) {
          // retry upload if it fails
          std::cout << "[cloudsyncworker] unable to upload file->" << fq.absPath
                    << std::endl;
          continue;
        }
        auto isQRemoved = m_dbManager.deleteFileQueue(fq.path, fq.filename);
        if (!isQRemoved) {
          // retry removing the file entry in the queuedb because the file is
          // renamed in cloud;
          continue;
        }
        std::cout << "[cloudsyncworker] file->" << fq.absPath
                  << " uploaded to cloud" << std::endl;
      }
      if (fq.sync_status == syncStatusToString(SyncStatus::RENAME)) {
        // rename file
        bool isUploaded = m_apiClient.renameFile(fq);
        FileMetadata f = constructFileMetadata(fq);
        bool isFileUpdated = m_dbManager.updateFile(f);
        if (!isUploaded || !isFileUpdated) {
          // retry upload if it fails
          std::cout << "[cloudsyncworker] unable to rename file->"
                    << *fq.old_filename << " to ->" << fq.filename << std::endl;
          continue;
        }
        auto isQRemoved = m_dbManager.deleteFileQueue(fq.path, fq.filename);
        if (!isQRemoved) {
          // retry removing the file entry in the queuedb because the file is
          // uploaded to cloud;
          continue;
        }
        std::cout << "[cloudsyncworker] file->" << *fq.old_filename
                  << " renamed to->" << fq.filename << " in cloud "
                  << std::endl;
      }
      if (fq.sync_status == syncStatusToString(SyncStatus::MODIFIED)) {
        // update file
        bool isUploaded = m_apiClient.uploadFile(fq, {}, true);
        FileMetadata f = constructFileMetadata(fq);
        bool isFileUpdated = m_dbManager.updateFile(f);
        if (!isUploaded || !isFileUpdated) {
          // retry upload if it fails
          std::cout << "[cloudsyncworker] unable to modify file->" << fq.absPath
                    << std::endl;
          continue;
        }
        auto isQRemoved = m_dbManager.deleteFileQueue(fq.path, fq.filename);
        if (!isQRemoved) {
          // retry removing the file entry in the queuedb because the file is
          // uploaded to cloud;
          continue;
        }
        std::cout << "[cloudsyncworker] file->" << fq.absPath
                  << " modified in cloud" << std::endl;
      }
      if (fq.sync_status == syncStatusToString(SyncStatus::DELETE)) {
        // delete file
        bool isUploaded = m_apiClient.deleteFile(fq);
        if (!isUploaded) {
          // retry upload if it fails
          std::cout << "[cloudsyncworker] unable to delete file->" << fq.absPath
                    << std::endl;
          continue;
        }
        auto isQRemoved = m_dbManager.deleteFileQueue(fq.path, fq.filename);
        if (!isQRemoved) {
          // retry removing the file entry in the queuedb because the file is
          // uploaded to cloud;
          continue;
        }
        std::cout << "[cloudsyncworker] file->" << fq.absPath
                  << " deleted from  cloud" << std::endl;
      }
    }
  }
};

void CloudSyncWorker::start() {
  m_stopThread = false;
  m_workerThread = std::thread(&CloudSyncWorker::run, this);
}

void CloudSyncWorker::run() {
  while (!m_stopThread) {
    bool isConnected = pollCloudToSyncToLocal();
    if (isConnected)
      processQueueToSyncUp();
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
