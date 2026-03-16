#include "DatabaseManager.hpp"
#include "Utility.hpp"
#include "types.hpp"
#include <filesystem>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sqlite3.h>
#include <sqlite_orm/sqlite_orm.h>
using namespace sqlite_orm;
namespace sync_app {

// We define a helper function to create the storage.
// This helps us deduce the complex template type of the storage.
inline auto create_storage_impl(const std::string &path) {
  return make_storage(
      path,
      make_table<FileMetadata>(
          "File", make_column("uuid", &FileMetadata::uuid),
          make_column("path", &FileMetadata::path),
          make_column("filename", &FileMetadata::filename),
          make_column("last_modified", &FileMetadata::last_modified),
          make_column("hashvalue", &FileMetadata::hashvalue),
          make_column("size", &FileMetadata::size),
          make_column("dirID", &FileMetadata::dirID),
          make_column("inode", &FileMetadata::inode),
          make_column("absPath", &FileMetadata::absPath),
          make_column("versions", &FileMetadata::versions),
          make_column("origin", &FileMetadata::origin, unique()),
          make_column("lastSyncedHashValue",
                      &FileMetadata::lastSyncedHashValue),
          make_column("lastSynced", &FileMetadata::lastSynced),
          make_column("conflictId", &FileMetadata::conflictId),
          primary_key(&FileMetadata::path, &FileMetadata::filename),
          foreign_key(&FileMetadata::dirID)
              .references(&DirectoryMetadata::uuid)),
      make_table<DirectoryMetadata>(
          "Directory", make_column("uuid", &DirectoryMetadata::uuid, unique()),
          make_column("device", &DirectoryMetadata::device),
          make_column("folder", &DirectoryMetadata::folder),
          make_column("path", &DirectoryMetadata::path),
          make_column("created_at", &DirectoryMetadata::created_at),
          make_column("absPath", &DirectoryMetadata::absPath),
          make_column("inode", &DirectoryMetadata::inode),
          make_column("lastSynced", &DirectoryMetadata::lastSynced),
          primary_key(&DirectoryMetadata::device, &DirectoryMetadata::folder,
                      &DirectoryMetadata::path)),
      make_table<FileQueueEntry>(
          "FileQueue", make_column("uuid", &FileQueueEntry::uuid),
          make_column("path", &FileQueueEntry::path),
          make_column("filename", &FileQueueEntry::filename),
          make_column("last_modified", &FileQueueEntry::last_modified),
          make_column("hashvalue", &FileQueueEntry::hashvalue),
          make_column("size", &FileQueueEntry::size),
          make_column("dirID", &FileQueueEntry::dirID),
          make_column("sync_status", &FileQueueEntry::sync_status),
          make_column("inode", &FileQueueEntry::inode),
          make_column("versions", &FileQueueEntry::versions),
          make_column("origin", &FileQueueEntry::origin, unique()),
          make_column("absPath", &FileQueueEntry::absPath),
          make_column("old_path", &FileQueueEntry::old_path),
          make_column("old_filename", &FileQueueEntry::old_filename),
          make_column("lastSynced", &FileQueueEntry::lastSynced),
          make_column("lastSyncedHashValue",
                      &FileQueueEntry::lastSyncedHashValue),
          primary_key(&FileQueueEntry::path, &FileQueueEntry::filename),
          foreign_key(&FileQueueEntry::dirID)
              .references(&DirectoryQueueEntry::uuid)),
      make_table<DirectoryQueueEntry>(
          "DirectoryQueue",
          make_column("uuid", &DirectoryQueueEntry::uuid, unique()),
          make_column("device", &DirectoryQueueEntry::device),
          make_column("folder", &DirectoryQueueEntry::folder),
          make_column("path", &DirectoryQueueEntry::path),
          make_column("created_at", &DirectoryQueueEntry::created_at),
          make_column("sync_status", &DirectoryQueueEntry::sync_status),
          make_column("absPath", &DirectoryQueueEntry::absPath),
          make_column("old_path", &DirectoryQueueEntry::old_path),
          make_column("inode", &DirectoryQueueEntry::inode),
          make_column("lastSynced", &DirectoryQueueEntry::lastSynced),
          primary_key(&DirectoryQueueEntry::device,
                      &DirectoryQueueEntry::folder,
                      &DirectoryQueueEntry::path)));
}

// Typedef for easier access within the Impl
using Storage = decltype(create_storage_impl(""));

struct DatabaseManager::Impl {
  Storage storage;
  Impl(const std::string &path) : storage(create_storage_impl(path)) {}
};

DatabaseManager::DatabaseManager(const std::string &dbPath,
                                 const std::string &syncPath)
    : m_dbPath(dbPath), m_syncPath(syncPath),
      m_impl(std::make_unique<Impl>(dbPath)) {}

DatabaseManager::~DatabaseManager() = default;

bool DatabaseManager::open() {
  try {
    m_impl->storage.get_all<FileMetadata>(limit(1));
    std::cout << "[DB] Database connection verified: " << m_dbPath << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cout << "[DB] Note: Initial connection: " << e.what() << std::endl;
    return true;
  }
}

void DatabaseManager::close() {}

void DatabaseManager::initializeSchema() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  std::cout << "[DB] Synchronizing schema via sqlite_orm..." << std::endl;
  m_impl->storage.sync_schema();
  std::cout << "[DB] Schema synchronized successfully." << std::endl;
}

pathParts DatabaseManager::getFolderDevice(const std::filesystem::path &path) {
  pathParts parts{"/", "/"};
  if (path.empty())
    return parts;
  parts.device = path.root_name().generic_string();
  parts.folder = path.filename().generic_string();
  if (parts.folder.empty())
    parts.folder = "/";
  auto root_dir = path.root_directory();
  auto rel = path.relative_path();
  if (!rel.empty()) {
    auto first = rel.begin();
    if (first != rel.end()) {
      parts.device = first->string();
    }
  }
  if (parts.device.empty()) {
    parts.device = "/";
  }
  return parts;
}

// File operations
std::optional<std::vector<FileMetadata>> DatabaseManager::getAllFiles() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<FileMetadata>();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::vector<FileQueueEntry>> DatabaseManager::getAllQueueFiles() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<FileQueueEntry>();
  } catch (...) {
    std::cerr << "[DB] " << std::endl;
    return std::nullopt;
  }
}

std::optional<FileMetadata>
DatabaseManager::getFileByOrigin(const std::string &origin) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    auto results = m_impl->storage.get_all<FileMetadata>(
        where(c(&FileMetadata::origin) == origin));
    if (results.empty())
      return std::nullopt;
    return results[0];
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Fetching FileByOrigin :" << e.what() << "\n";
    return std::nullopt;
  }
}

std::optional<FileMetadata>
DatabaseManager::getFileByPath(const std::string &path,
                               const std::string &filename) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get<FileMetadata>(path, filename);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<FileQueueEntry>
DatabaseManager::getFileQueueByPath(const std::string &path,
                                    const std::string &filename) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    auto results = m_impl->storage.get_all<FileQueueEntry>(
        where(c(&FileQueueEntry::path) == path &&
              c(&FileQueueEntry::filename) == filename));
    if (results.empty())
      return std::nullopt;
    return results[0];
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Fetching FileQueue : " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool DatabaseManager::deleteAllFilesInQueue() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.remove_all<FileQueueEntry>();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Emptying File Queue <<" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteAllDirsInQueue() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.remove_all<DirectoryQueueEntry>();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Emptying Directory Queue <<" << e.what()
              << std::endl;
    return false;
  }
}

bool DatabaseManager::insertFile(const FileMetadata &file,
                                 const FileQueueEntry &fileQueue) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      DirectoryQueueEntry dq;
      pathParts p = getFolderDevice(std::filesystem::path(file.path));
      try {
        auto dir = m_impl->storage.get<DirectoryMetadata>(p.device, p.folder,
                                                          file.path);
        dq =
            Utility::constructDirectoryQueueEntry(dir, SyncStatus::FILE_LINKED);
      } catch (const std::exception &e) {
        std::cerr << "[DB] " << e.what() << " device: " << p.device
                  << " path: " << file.path << std::endl;
        return false;
      }
      m_impl->storage.replace<DirectoryQueueEntry>(dq);
      m_impl->storage.replace<FileMetadata>(file);
      m_impl->storage.replace<FileQueueEntry>(fileQueue);
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Inserting ->" << file.absPath
              << " into File Table =>" << e.what() << std::endl;
    return false;
  }
}
bool DatabaseManager::renameFile(const FileMetadata &file,
                                 const FileQueueEntry &fileQueue) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      DirectoryQueueEntry dq;
      pathParts p = getFolderDevice(std::filesystem::path(file.path));
      try {
        auto dir = m_impl->storage.get<DirectoryMetadata>(p.device, p.folder,
                                                          file.path);
        dq =
            Utility::constructDirectoryQueueEntry(dir, SyncStatus::FILE_LINKED);
      } catch (const std::exception &e) {
        std::cerr << "[DB] " << e.what() << " device: " << p.device
                  << " path: " << file.path << std::endl;
        return false;
      }
      m_impl->storage.replace<DirectoryQueueEntry>(dq);
      m_impl->storage.replace<FileMetadata>(file);
      m_impl->storage.replace<FileQueueEntry>(fileQueue);
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Inserting ->" << file.absPath
              << " into File Table =>" << e.what() << std::endl;
    return false;
  }
}
bool DatabaseManager::updateFile(const FileMetadata &file) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    /*    auto existingFile = m_impl->storage.count<FileMetadata>(
            where(c(&FileMetadata::path) == file.path &&
                  c(&FileMetadata::filename) == file.filename));

        if (existingFile) {
          m_impl->storage.update<FileMetadata>(file);
        }
        */
    m_impl->storage.replace<FileMetadata>(file);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << file.absPath << " in File Table =>"
              << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::updateFileWithTransaction(const FileMetadata &f,
                                                const std::string &path,
                                                const std::string &filename) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&] {
      m_impl->storage.remove<FileMetadata>(path, filename);
      m_impl->storage.replace<FileMetadata>(f);
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Exception in updating file." << e.what() << " " << f.path
              << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteFile(const std::string &path,
                                 const std::string &filename,
                                 const FileQueueEntry &fq) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&] {
      pathParts p = getFolderDevice(std::filesystem::path(path));
      DirectoryQueueEntry dq;
      try {
        auto dir =
            m_impl->storage.get<DirectoryMetadata>(p.device, p.folder, path);
        dq = Utility::constructDirectoryQueueEntry(dir);
        dq.old_path = fq.old_path;
        dq.sync_status = syncStatusToString(SyncStatus::FILE_LINKED);
      } catch (const std::exception &) {
        return false;
      }
      m_impl->storage.remove<FileMetadata>(path, filename);
      m_impl->storage.replace<DirectoryQueueEntry>(dq);
      m_impl->storage.replace<FileQueueEntry>(fq);
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Deleting ->" << path << "/" << filename
              << " from File Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteFileByPath(const std::string &path,
                                       const std::string &filename) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.remove<FileMetadata>(path, filename);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Exception : " << e.what() << " " << path << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertFile(const FileMetadata &file) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.replace<FileMetadata>(file);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Upserting ->" << file.absPath
              << " into File Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteFilesByPath(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    m_impl->storage.remove_all<FileMetadata>(
        where(c(&FileMetadata::path) == path ||
              like(&FileMetadata::path, path + "/%")));
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Deleting Files By Path " << e.what() << std::endl;
    return false;
  }
}

// DirectorSELECT * FROM 'file' where path = "/sync-renamed/sandeep/New
// folder/New folder"y operations
std::optional<std::vector<DirectoryMetadata>>
DatabaseManager::getAllDirectories() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<DirectoryMetadata>();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::vector<DirectoryMetadata>>
DatabaseManager::getAllDirsInPath(const std::string &path) {
  try {
    return m_impl->storage.get_all<DirectoryMetadata>(
        where(c(&DirectoryMetadata::path) == path) ||
        like(&DirectoryMetadata::path, path + "/%"));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<DirectoryMetadata>
DatabaseManager::getDirectoryByPath(const std::string &device,
                                    const std::string &folder,
                                    const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    auto results = m_impl->storage.get_all<DirectoryMetadata>(
        where(c(&DirectoryMetadata::device) == device &&
              c(&DirectoryMetadata::folder) == folder &&
              c(&DirectoryMetadata::path) == path));
    if (results.empty())
      return std::nullopt;
    return results[0];
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Fetching DirectoryByPath ->" << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::optional<DirectoryQueueEntry>
DatabaseManager::getDirectoryQueueByPath(const std::string &device,
                                         const std::string &folder,
                                         const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    auto results = m_impl->storage.get_all<DirectoryQueueEntry>(
        where(c(&DirectoryQueueEntry::device) == device &&
              c(&DirectoryQueueEntry::folder) == folder &&
              c(&DirectoryQueueEntry::path) == path));
    if (results.empty())
      return std::nullopt;
    return results[0];
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Fetching DirectoryByPath ->" << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::optional<std::vector<FileMetadata>>
DatabaseManager::getAllFilesInDirectory(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    return m_impl->storage.get_all<FileMetadata>(
        where(c(&FileMetadata::path) == path ||
              like(&FileMetadata::path, path + "/%")));
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error fetching from ->" << path << " " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::vector<FileMetadata>
DatabaseManager::getFilesInDirectory(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    return m_impl->storage.get_all<FileMetadata>(
        where(c(&FileMetadata::path) == path));
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error fetching from ->" << path << " " << e.what()
              << std::endl;
    return {};
  }
}

std::optional<std::vector<FileQueueEntry>>
DatabaseManager::getFilesInDirQ(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    return m_impl->storage.get_all<FileQueueEntry>(
        where(c(&FileQueueEntry::path) == path) ||
        like(&FileQueueEntry::path, path + "%"));
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error fetching from ->" << path << " " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::optional<std::vector<FileQueueEntry>>
DatabaseManager::getFileQueueByInode(const std::string &inode) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<FileQueueEntry>(
        where(c(&FileQueueEntry::inode) == inode));
  } catch (const std::exception &e) {
    std::cerr << "[DB] Unable to find the file by Inode ->" << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::optional<
    std::tuple<std::vector<FileQueueEntry>, std::vector<DirectoryQueueEntry>>>

DatabaseManager::getDirQueueByInode(const std::string &inode) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    auto dirQ = m_impl->storage.get_all<DirectoryQueueEntry>(
        where(c(&DirectoryQueueEntry::inode) == inode));
    if (!dirQ.empty() && dirQ.size() == 1) {
      auto d = dirQ[0];
      std::vector<DirectoryQueueEntry> dirsQ =
          m_impl->storage.get_all<DirectoryQueueEntry>(
              where(c(&DirectoryQueueEntry::path) == d.path ||
                    like(&DirectoryQueueEntry::path, d.path + "/%")));
      std::vector<FileQueueEntry> filesQ =
          m_impl->storage.get_all<FileQueueEntry>(
              where(c(&FileQueueEntry::path) == d.path ||
                    like(&FileQueueEntry::path, d.path + "/%")));
      return std::make_tuple(filesQ, dirsQ);
    } else {
      return std::nullopt;
    }
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error fetching By Inode ->" << e.what() << std::endl;
    return std::nullopt;
  }
}

bool DatabaseManager::insertDirectory(const DirectoryMetadata &dir,
                                      const DirectoryQueueEntry &dirQueue) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&] {
      m_impl->storage.replace<DirectoryMetadata>(dir);
      m_impl->storage.replace<DirectoryQueueEntry>(dirQueue);
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error inserting ->" << dir.path
              << " into Directory Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::reconcileLocalState(
    const std::vector<FileMetadata> &filesToAdd,
    const std::vector<FileQueueEntry> &filesToDelete,
    const std::vector<DirectoryMetadata> &dirsToAdd,
    const std::vector<DirectoryQueueEntry> &dirsToDelete,
    const std::vector<FileMetadata> &filesToModify,
    const std::vector<FileQueueEntry> &filesToMove,
    const std::vector<DirectoryQueueEntry> &dirsToMove) {
  try {
    return m_impl->storage.transaction([&]() {
      std::map<std::string, DirectoryMetadata> dirsInMainMap;
      std::map<std::string, DirectoryQueueEntry> dirsInQMap;

      std::map<std::string, std::string> dirIDsMap;

      std::vector<DirectoryQueueEntry> dirsInQ;
      std::vector<DirectoryMetadata> dirsInMain;

      std::vector<FileMetadata> filesInMain;
      std::vector<FileQueueEntry> filesInQ;

      std::set<std::string> filesToDeleteSet;

      std::vector<DirectoryQueueEntry> dirsToDeleteWithStatus;

      // *************************************************************************************
      // 1. Add Dirs
      // index the dir paths
      std::for_each(
          dirsToAdd.begin(), dirsToAdd.end(),
          [&](const DirectoryMetadata &d) { dirsInMainMap[d.path] = d; });
      // reserve the size
      dirsInMain.reserve(dirsInMainMap.size());
      // get the dirs to add in main
      std::transform(dirsInMainMap.begin(), dirsInMainMap.end(),
                     std::back_inserter(dirsInMain),
                     [&](const auto &pair) { return pair.second; });
      // reserve the size
      dirsInQ.reserve(dirsInMain.size());
      // dirs to add in queue
      std::transform(
          dirsInMain.begin(), dirsInMain.end(), std::back_inserter(dirsInQ),
          [&](const DirectoryMetadata &d) {
            DirectoryQueueEntry dq;
            dq = Utility::constructDirectoryQueueEntry(d, SyncStatus::NEW);
            //            dq.sync_status = syncStatusToString(SyncStatus::NEW);
            return dq;
          });

      /*
      m_impl->storage.replace_range<DirectoryMetadata>(dirsInMain.begin(),

                                                       dirsInMain.end());
      m_impl->storage.replace_range<DirectoryQueueEntry>(dirsInQ.begin(),
                                                         dirsInQ.end());
      */
      // insert dirs into main table
      batchedReplace<DirectoryMetadata>(m_impl->storage, dirsInMain);
      // insert dirs into queue table
      batchedReplace<DirectoryQueueEntry>(m_impl->storage, dirsInQ);
      // clear queue dirs
      dirsInQ.clear();
      dirsInQMap.clear();
      dirsInMainMap.clear();
      dirsInMain.clear();

      // *******************************************************************************************
      // 2. Add Files

      filesInMain.reserve(filesToAdd.size());
      filesInQ.reserve(filesToAdd.size());

      // map dirID to files
      std::transform(filesToAdd.begin(), filesToAdd.end(),
                     std::back_inserter(filesInMain),
                     [&](const FileMetadata &f) {
                       std::string dirID = "";
                       auto it = dirIDsMap.find(f.path);
                       if (it != dirIDsMap.end()) {
                         dirID = it->second;
                       } else {
                         auto dirs = m_impl->storage.get_all<DirectoryMetadata>(
                             where(c(&DirectoryMetadata::path) == f.path));
                         if (!dirs.empty()) {
                           dirID = dirs[0].uuid;
                           dirIDsMap[f.path] = dirID;
                         }
                       }
                       FileMetadata file{f};
                       file.dirID = dirID;
                       return file;
                     });

      // map the fileQ and corresponding DirQ to be added
      std::transform(filesInMain.begin(), filesInMain.end(),
                     std::back_inserter(filesInQ), [&](const FileMetadata &f) {
                       FileQueueEntry fq;
                       fq =
                           Utility::constructFileQueueEntry(f, SyncStatus::NEW);
                       pathParts p =
                           getFolderDevice(std::filesystem::path(f.path));
                       auto dir = m_impl->storage.get_all<DirectoryMetadata>(
                           where(c(&DirectoryMetadata::device) == p.device &&
                                 c(&DirectoryMetadata::folder) == p.folder &&
                                 c(&DirectoryMetadata::path) == f.path));
                       if (!dir.empty()) {
                         DirectoryQueueEntry dq;
                         dq = Utility::constructDirectoryQueueEntry(
                             dir[0], SyncStatus::FILE_LINKED);
                         dirsInQMap[f.path] = dq;
                       }
                       return fq;
                     });

      // reserve size for dirs to be added [ corresponding to files in Q]
      dirsInQ.reserve(dirsInQMap.size());
      filesInQ.reserve(filesToAdd.size());

      // dirsQ to be added before inserting fileQs
      std::transform(dirsInQMap.begin(), dirsInQMap.end(),
                     std::back_inserter(dirsInQ),
                     [&](const auto &pair) { return pair.second; });
      /*
        m_impl->storage.replace_range<FileMetadata>(filesInMain.begin(),
                                                    filesInMain.end());
        m_impl->storage.replace_range<DirectoryQueueEntry>(dirsInQ.begin(),
                                                           dirsInQ.end());
        m_impl->storage.replace_range<FileQueueEntry>(filesInQ.begin(),
                                                      filesInQ.end());
         */
      // adding files into main table
      batchedReplace<FileMetadata>(m_impl->storage, filesInMain);
      // adding dirQ into Queue table
      batchedReplace<DirectoryQueueEntry>(m_impl->storage, dirsInQ);
      // adding filesQ into Queue table
      batchedReplace<FileQueueEntry>(m_impl->storage, filesInQ);

      dirsInQ.clear();
      dirsInQMap.clear();
      filesInQ.clear();
      filesInMain.clear();
      dirIDsMap.clear();

      // ****************************************************************************
      // 3. modify files

      filesInMain.reserve(filesToModify.size());
      filesInQ.reserve(filesToModify.size());

      // map modified files with dirID
      std::transform(filesToModify.begin(), filesToModify.end(),
                     std::back_inserter(filesInMain),
                     [&](const FileMetadata &f) {
                       std::string dirID = "";
                       auto it = dirIDsMap.find(f.path);
                       if (it != dirIDsMap.end()) {
                         dirID = it->second;
                       } else {
                         auto dirs = m_impl->storage.get_all<DirectoryMetadata>(
                             where(c(&DirectoryMetadata::path) == f.path));
                         if (!dirs.empty()) {
                           dirID = dirs[0].uuid;
                           dirIDsMap[f.path] = dirID;
                         }
                       }
                       FileMetadata file{f};
                       file.dirID = dirID;
                       return file;
                     });

      // map modified filesQ with dirID & corresponding dirs to be added in
      // Queue
      std::transform(filesInMain.begin(), filesInMain.end(),
                     std::back_inserter(filesInQ), [&](const FileMetadata &f) {
                       FileQueueEntry fq;
                       fq = Utility::constructFileQueueEntry(
                           f, SyncStatus::MODIFIED);
                       auto dir = m_impl->storage.get_all<DirectoryMetadata>(
                           where(c(&DirectoryMetadata::path) == f.path));
                       if (!dir.empty()) {
                         DirectoryQueueEntry dq;
                         dq = Utility::constructDirectoryQueueEntry(
                             dir[0], SyncStatus::FILE_LINKED);
                         auto it = dirsInQMap.find(dq.path);
                         if (it == dirsInQMap.end()) {
                           dirsInQMap[dq.path] = dq;
                         }
                       }
                       return fq;
                     });

      dirsInQ.reserve(dirsInQMap.size());

      // map dirQ to be inserted corresponding to filesQ
      std::transform(dirsInQMap.begin(), dirsInQMap.end(),
                     std::back_inserter(dirsInQ),
                     [&](const auto &pair) { return pair.second; });

      /*
      m_impl->storage.replace_range<FileMetadata>(filesInMain.begin(),
                                                  filesInMain.end());
      //
      m_impl->storage.replace_range<DirectoryQueueEntry>(dirsInQ.begin(),
                                                         dirsInQ.end());
      m_impl->storage.replace_range<FileQueueEntry>(filesInQ.begin(),
                                                    filesInQ.end());
      */

      // adding modified files to main
      batchedReplace<FileMetadata>(m_impl->storage, filesInMain);
      // insert dirQ into queue
      batchedReplace<DirectoryQueueEntry>(m_impl->storage, dirsInQ);
      // adding modified files to Queue
      batchedReplace<FileQueueEntry>(m_impl->storage, filesInQ);

      filesInMain.clear();
      filesInQ.clear();
      dirsInQMap.clear();
      dirsInQ.clear();

      //****************************************************************************
      // 4. remove files
      // map files to be inserted as deleted in queue
      std::transform(filesToDelete.begin(), filesToDelete.end(),
                     std::back_inserter(filesInQ), [&](auto &f) {
                       FileQueueEntry fq{f};
                       fq.sync_status = syncStatusToString(SyncStatus::DELETE);
                       auto it = dirsInQMap.find(f.path);
                       if (it == dirsInQMap.end()) {
                         auto dir = m_impl->storage.get_all<DirectoryMetadata>(
                             where(c(&DirectoryMetadata::path) == f.path));
                         if (!dir.empty()) {
                           DirectoryQueueEntry dq;
                           dq = Utility::constructDirectoryQueueEntry(
                               dir[0], SyncStatus::FILE_LINKED);
                           dirsInQMap[f.path] = dq;
                         }
                       }
                       return fq;
                     });

      filesInQ.reserve(filesToDelete.size());

      // delete files from main table
      std::for_each(filesToDelete.begin(), filesToDelete.end(),
                    [&](const auto &f) {
                      m_impl->storage.remove_all<FileMetadata>(
                          where(c(&FileMetadata::path) == f.path &&
                                c(&FileMetadata::filename) == f.filename));
                    });

      dirsInQ.reserve(dirsInQMap.size());

      // map dirs to be inserted into directory queue
      std::transform(dirsInQMap.begin(), dirsInQMap.end(),
                     std::back_inserter(dirsInQ),
                     [&](const auto &pair) { return pair.second; });

      // insert sync status for the files to be deleted in queue table
      /*
            m_impl->storage.replace_range<DirectoryQueueEntry>(dirsInQ.begin(),
                                                               dirsInQ.end());
            m_impl->storage.replace_range<FileQueueEntry>(filesInQ.begin(),
                                                          filesInQ.end());
            */
      batchedReplace<DirectoryQueueEntry>(m_impl->storage, dirsInQ);
      batchedReplace<FileQueueEntry>(m_impl->storage, filesInQ);

      filesInQ.clear();
      dirsInQ.clear();
      dirsInQMap.clear();
      // **************************************************************************
      // 5. delete dirs
      //
      dirsInQ.reserve(dirsToDelete.size());

      // map the dirs to be inserted in queue
      std::transform(dirsToDelete.begin(), dirsToDelete.end(),
                     std::back_inserter(dirsInQ), [&](const auto &d) {
                       DirectoryQueueEntry dq{d};
                       dq.sync_status = syncStatusToString(SyncStatus::DELETE);
                       return dq;
                     });

      // mapping files to be deleted by path
      std::for_each(dirsToDelete.begin(), dirsToDelete.end(),
                    [&](auto &dq) { filesToDeleteSet.insert(dq.path); });

      // delete files by path in main table
      std::for_each(filesToDeleteSet.begin(), filesToDeleteSet.end(),
                    [&](const std::string &path) {
                      m_impl->storage.remove_all<FileMetadata>(
                          where(c(&FileMetadata::path) == path));
                    });

      // remove dirs in main table
      std::for_each(dirsToDelete.begin(), dirsToDelete.end(),
                    [&](const auto &dq) {
                      m_impl->storage.remove_all<DirectoryMetadata>(
                          where(c(&DirectoryMetadata::path) == dq.path));
                    });

      // insert removed dirs in the queue
      // m_impl->storage.replace_range<DirectoryQueueEntry>(dirsInQ.begin(),
      //                                                  dirsInQ.end());
      batchedReplace<DirectoryQueueEntry>(m_impl->storage, dirsInQ);

      dirsInQ.clear();
      dirsInMain.clear();
      //***************************************************************************
      // 6. move/rename Dirs
      // TODO:
      std::map<std::string, DirectoryQueueEntry> movedDirsMap;

      dirsInMain.reserve(dirsToMove.size());

      std::transform(dirsToMove.begin(), dirsToMove.end(),
                     std::back_inserter(dirsInMain), [&](const auto &dq) {
                       movedDirsMap[dq.path] = dq;
                       return Utility::constructDirectoryMetadata(dq);
                     });
      // m_impl->storage.replace_range<DirectoryMetadata>(dirsInMain.begin(),
      //                                                 dirsInMain.end());
      batchedReplace<DirectoryMetadata>(m_impl->storage, dirsInMain);
      //      m_impl->storage.replace_range(dirsToMove.begin(),
      //      dirsToMove.end());
      // **************************************************************************
      // 7. move/rename files
      //
      // TODO:
      dirsInMainMap.clear();
      dirsInMain.clear();
      dirsInQ.clear();
      dirsInQMap.clear();
      filesInMain.clear();
      filesInQ.clear();
      filesInMain.reserve(filesToMove.size());

      //*******************************************************************
      // removing redundant fileQ entries which are already part of the dir
      // moves

      std::vector<FileQueueEntry> movedFiles;
      movedFiles =
          Utility::removeRedundantMovedFiles<std::vector<FileQueueEntry>>(
              dirsToMove, filesToMove);
      filesInQ.reserve(movedFiles.size());

      //*********************************************************************
      // map the corresponding moved Files's dirs to be inserted into Queue
      std::vector<DirectoryQueueEntry> reducedDirQ;
      reducedDirQ =
          Utility::reduceDirs<std::vector<DirectoryQueueEntry>>(dirsToMove);

      std::transform(
          filesToMove.begin(), filesToMove.end(),
          std::back_inserter(filesInMain), [&](const auto &f) {
            FileQueueEntry fq{f};
            auto it = dirsInMainMap.find(fq.path);
            if (it == dirsInMainMap.end()) {
              pathParts p = getFolderDevice(std::filesystem::path(fq.path));
              auto dir = m_impl->storage.get_all<DirectoryMetadata>(
                  where(c(&DirectoryMetadata::device) == p.device &&
                        c(&DirectoryMetadata::folder) == p.folder &&
                        c(&DirectoryMetadata::path) == fq.path));
              if (!dir.empty()) {
                dirsInMainMap[fq.path] = dir[0];
                fq.dirID = dir[0].uuid;
              } else {
                DirectoryMetadata d =
                    Utility::createDirectoryMetadata(fq.path, m_syncPath);
                dirsInMainMap[fq.path] = d;
                fq.dirID = d.uuid;
              }
            } else {
              fq.dirID = it->second.uuid;
            }
            auto file = Utility::constructFileMetadata(fq);
            return file;
          });

      // map filtered File Queue with their corresponding dirs to be inserted
      // into queue
      //
      std::transform(
          movedFiles.begin(), movedFiles.end(), std::back_inserter(filesInQ),
          [&](const auto &f) {
            FileQueueEntry fq{f};
            auto it = dirsInQMap.find(fq.path);
            if (it == dirsInQMap.end()) {
              pathParts p = getFolderDevice(std::filesystem::path(fq.path));
              auto dir = m_impl->storage.get_all<DirectoryMetadata>(
                  where(c(&DirectoryMetadata::device) == p.device &&
                        c(&DirectoryMetadata::folder) == p.folder &&
                        c(&DirectoryMetadata::path) == fq.path));
              if (!dir.empty()) {
                dirsInQMap[fq.path] = Utility::constructDirectoryQueueEntry(
                    dir[0], SyncStatus::FILE_LINKED);
                fq.dirID = dir[0].uuid;
              } else {
                DirectoryMetadata d{
                    Utility::createDirectoryMetadata(fq.path, m_syncPath)};
                dirsInQMap[fq.path] = Utility::constructDirectoryQueueEntry(
                    d, SyncStatus::FILE_LINKED);
                fq.dirID = dirsInQMap[fq.path].uuid;
              }
            } else {
              fq.dirID = it->second.uuid;
            }
            return fq;
          });

      dirsInQ.reserve(dirsInQMap.size());
      dirsInMain.reserve(dirsInMainMap.size());

      // extract dirs to be inserted into Queue from the map
      std::transform(dirsInQMap.begin(), dirsInQMap.end(),
                     std::back_inserter(dirsInQ),
                     [&](const auto &pair) { return pair.second; });
      // add the actual folder that was moved
      for (auto &d : reducedDirQ) {
        auto it = dirsInQMap.find(d.path);
        if (it == dirsInQMap.end())
          dirsInQ.push_back(d);
      }
      // extract the dirs to be inserted into main from the map
      std::transform(dirsInMainMap.begin(), dirsInMainMap.end(),
                     std::back_inserter(dirsInMain),
                     [&](const auto &pair) { return pair.second; });

      batchedReplace<DirectoryMetadata>(m_impl->storage, dirsInMain);
      batchedReplace<FileMetadata>(m_impl->storage, filesInMain);
      batchedReplace<DirectoryQueueEntry>(m_impl->storage, dirsInQ);
      batchedReplace<FileQueueEntry>(m_impl->storage, filesInQ);
      /*
            m_impl->storage.replace_range<DirectoryMetadata>(dirsInMain.begin(),
                                                             dirsInMain.end());
            m_impl->storage.replace_range<FileMetadata>(filesInMain.begin(),
                                                        filesInMain.end());
            m_impl->storage.replace_range<DirectoryQueueEntry>(dirsInQ.begin(),
                                                               dirsInQ.end());

            m_impl->storage.replace_range<FileQueueEntry>(filesInQ.begin(),
                                                          filesInQ.end());
      */
      return true;
      //****************************************************************************
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] exception : " << e.what() << std::endl;
    return false;
  }
}
bool DatabaseManager::moveFile(const FileMetadata &f,
                               const FileQueueEntry &fq) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      m_impl->storage.remove<FileMetadata>(fq.old_path, fq.old_filename);
      m_impl->storage.replace<FileMetadata>(f);
      m_impl->storage.replace<FileQueueEntry>(fq);
      return true;
    });

  } catch (const std::exception &e) {
    std::cout << "[DB] unable to move the file ->" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::moveFiles(
    const std::map<std::string, std::vector<FileQueueEntry>> &movedFiles) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      std::set<std::string> oldPathSet;
      for (auto &[path, files] : movedFiles) {
        for (auto &f : files) {
          oldPathSet.insert(*f.old_path);
        }
      }
      for (auto &path : oldPathSet) {
        m_impl->storage.remove_all<FileMetadata>(
            where(c(&FileMetadata::path) == path));
      }
      for (auto &[path, qFiles] : movedFiles) {

        std::vector<FileMetadata> files;
        files.reserve(qFiles.size());
        for (const auto &fq : qFiles) {
          //          files.push_back(static_cast<const FileMetadata &>(fq));
        }
        for (auto &f : files) {
          std::cout << "[DB_f] " << f.path << " | " << f.dirID << std::endl;
          m_impl->storage.replace<FileMetadata>(FileMetadata{f});
        }
        for (auto &fq : qFiles) {
          std::cout << "[DB_fq] " << fq.path << " | " << fq.dirID << std::endl;
          //    m_impl->storage.replace<FileQueueEntry>(FileQueueEntry{fq});
        }
        // m_impl->storage.insert_range<FileMetadata>(files.begin(),
        // files.end());
        // m_impl->storage.insert_range<FileQueueEntry>(qFiles.begin(),
        // qFiles.end());
      }
      return true;
    });
  } catch (const std::exception &e) {
    std::cout << "[DB] unable to move the files ->" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::insertFileWithDirectory(
    FileMetadata &f, const std::vector<DirectoryMetadata> &dirs) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([this, &dirs, &f]() {
      for (auto &dir : dirs) {
        m_impl->storage.replace<DirectoryMetadata>(dir);
      }
      m_impl->storage.replace<FileMetadata>(f);
      return true;
    });

  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Creating File & its Directory ->" << e.what()
              << std::endl;
    return false;
  }
}

bool DatabaseManager::updateMovedFile(const FileQueueEntry &fq,
                                      const FileMetadata &f) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    return m_impl->storage.transaction([&]() {
      try {
        m_impl->storage.remove<FileQueueEntry>(
            where(c(&FileQueueEntry::path) == fq.old_path &&
                  c(&FileQueueEntry::filename) == fq.old_filename));
      } catch (...) {
        return false;
      }
      m_impl->storage.replace<FileQueueEntry>(fq);
      m_impl->storage.replace<FileMetadata>(f);

      return true;
    });

  } catch (...) {

    return false;
  }
};

bool DatabaseManager::insertFileAndQueueWithDirectory(
    FileMetadata &cloudFile, FileMetadata &conflictedFile,
    FileQueueEntry &conflictedFileQueue,
    DirectoryQueueEntry &conflictedDirQueue,
    const std::vector<DirectoryMetadata> &dirs) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      for (auto &dir : dirs) {
        m_impl->storage.replace<DirectoryMetadata>(dir);
      }
      m_impl->storage.replace<FileMetadata>(cloudFile);
      m_impl->storage.replace<FileMetadata>(conflictedFile);
      m_impl->storage.replace<DirectoryQueueEntry>(conflictedDirQueue);
      try {
        auto fileInQueue = m_impl->storage.get<FileQueueEntry>(
            cloudFile.path, cloudFile.filename);
        m_impl->storage.remove<FileQueueEntry>(cloudFile.path,
                                               cloudFile.filename);
      } catch (...) {
        std::cout << "[DB] modified file not in queue..do nothing"
                  << cloudFile.absPath << std::endl;
      }
      m_impl->storage.replace<FileQueueEntry>(conflictedFileQueue);
      return true;
    });

  } catch (const std::exception &e) {
    std::cerr << "[DB] Error inserting file & filequeue with directory ->"
              << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::updateDirectory(const DirectoryMetadata &dir) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&] {
      m_impl->storage.replace<DirectoryMetadata>(dir);
      return true;
    });

  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << dir.path
              << " into Directory Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteDirectory(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      m_impl->storage.remove_all<FileMetadata>(
          where(c(&FileMetadata::path) == path ||
                like(&FileMetadata::path, path + "/%")));
      m_impl->storage.remove_all<DirectoryMetadata>(
          where(c(&DirectoryMetadata::path) == path ||
                like(&DirectoryMetadata::path, path + "/%")));
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting path ->" << path << " | " << e.what()
              << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteFolderWithTransaction(
    const std::string &path, const DirectoryQueueEntry &dq) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      m_impl->storage.remove_all<FileMetadata>(
          where(c(&FileMetadata::path) == path ||
                like(&FileMetadata::path, path + "/%")));
      m_impl->storage.remove_all<DirectoryMetadata>(
          where(c(&DirectoryMetadata::path) == path ||
                like(&DirectoryMetadata::path, path + "/%")));

      m_impl->storage.remove_all<FileQueueEntry>(
          where(c(&FileQueueEntry::path) == path ||
                like(&FileQueueEntry::path, path + "/%")));
      m_impl->storage.remove_all<DirectoryQueueEntry>(
          where(c(&DirectoryQueueEntry::path) == path ||
                like(&DirectoryQueueEntry::path, path + "/%")));

      m_impl->storage.replace<DirectoryQueueEntry>(dq);
      return true; // Commit
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Deleting Folder Transaction ->" << path << " "
              << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::moveDirectory(const std::string &path,
                                    const std::string &oldPath,
                                    const DirectoryQueueEntry &dq,
                                    bool isLocalMove) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      auto subDirs = m_impl->storage.get_all<DirectoryMetadata>(
          where(c(&DirectoryMetadata::path) == oldPath ||
                like(&DirectoryMetadata::path, oldPath + "/%")));
      for (auto &dir : subDirs) {
        auto dirFiles = m_impl->storage.get_all<FileMetadata>(
            where(c(&FileMetadata::dirID) == dir.uuid));
        std::string movedSegment(
            std::filesystem::relative(dir.path, oldPath).generic_string());
        DirectoryMetadata d(dir);
        std::string newPath;
        if (movedSegment != ".") {
          newPath = path + "/" + movedSegment;
        } else {
          newPath = path;
        }
        pathParts p = getFolderDevice(newPath);
        std::string absPath = m_syncPath + newPath;
        d.absPath = absPath;
        d.path = newPath;
        d.folder = p.folder;
        d.device = p.device;
        if (dirFiles.size() > 0) {
          // update the files
          for (auto &file : dirFiles) {
            FileMetadata f(file);
            f.absPath = absPath == "/" ? "/" + file.filename
                                       : absPath + "/" + file.filename;
            f.path = newPath;
            m_impl->storage.replace<FileMetadata>(f);
          }
        }
        // update directory
        m_impl->storage.replace<DirectoryMetadata>(d);
      }
      if (isLocalMove) {
        std::vector<DirectoryQueueEntry> queueDirs =
            m_impl->storage.get_all<DirectoryQueueEntry>(
                where(c(&DirectoryQueueEntry::path) == oldPath ||
                      like(&DirectoryQueueEntry::path, oldPath + "/%")));
        if (queueDirs.size() > 0) {
          m_impl->storage.remove_all<FileQueueEntry>(
              where(c(&FileQueueEntry::path) == oldPath ||
                    like(&FileQueueEntry::path, oldPath + "/%")));
          m_impl->storage.remove_all<DirectoryQueueEntry>(
              where(c(&DirectoryQueueEntry::path) == oldPath ||
                    like(&DirectoryQueueEntry::path, oldPath + "/%")));
        }
        m_impl->storage.replace<DirectoryQueueEntry>(dq);
      }
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Moving Directory ->" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertDirectory(const DirectoryMetadata &dir) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    // Optimized check by (device, folder, path) using where clause
    m_impl->storage.replace<DirectoryMetadata>(dir);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] upsertDirectory Error: " << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertFileQueue(const FileQueueEntry &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.replace<FileQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] upsertFileQueue Error: " << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[DB] upsertFileQueue Unknown Error" << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertDirectoryQueue(const DirectoryQueueEntry &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    // Optimized check by (device, folder, path) using where clause
    m_impl->storage.replace<DirectoryQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] upsertDirectoryQueue Error: " << e.what() << std::endl;
    return false;
  }
}
bool DatabaseManager::moveDirectoryQueue(const std::string &path,
                                         const std::string &oldPath) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      auto subDirs = m_impl->storage.get_all<DirectoryMetadata>(
          where(c(&DirectoryMetadata::path) == oldPath ||
                like(&DirectoryMetadata::path, oldPath + "/%")));
      for (auto &dir : subDirs) {
        auto dirFiles = m_impl->storage.get_all<FileMetadata>(
            where(c(&FileMetadata::dirID) == dir.uuid));
        std::string movedSegment(
            std::filesystem::relative(dir.path, oldPath).generic_string());
        DirectoryMetadata d(dir);
        std::string newPath;
        if (movedSegment != ".") {
          newPath = path + "/" + movedSegment;
        } else {
          newPath = path;
        }
        pathParts p = getFolderDevice(newPath);
        std::string absPath = m_syncPath + newPath;
        d.absPath = absPath;
        d.path = newPath;
        d.folder = p.folder;
        d.device = p.device;
        if (dirFiles.size() > 0) {
          // update the files
          for (auto &file : dirFiles) {
            FileMetadata f(file);
            f.absPath = absPath == "/" ? "/" + file.filename
                                       : absPath + "/" + file.filename;
            f.path = newPath;
            m_impl->storage.update<FileMetadata>(f);
          }
        }
        // update directory
        m_impl->storage.update<DirectoryMetadata>(d);
      }
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Moving Directory ->" << e.what() << std::endl;
    return false;
  }
}

// File Queue operations
std::optional<std::vector<FileQueueEntry>> DatabaseManager::getFileQueue() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<FileQueueEntry>();
  } catch (...) {
    return std::nullopt;
  }
}

bool DatabaseManager::insertFileQueue(const FileQueueEntry &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.replace<FileQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error inserting ->" << entry.absPath
              << " into FileQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::updateFileQueue(const FileQueueEntry &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.update<FileQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << entry.absPath
              << " in FileQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

std::optional<std::vector<DirectoryQueueEntry>>
DatabaseManager::getAllQueueDirectories() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<DirectoryQueueEntry>();
  } catch (const std::exception &e) {
    std::cerr << "[DB] " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool DatabaseManager::deleteFileQueue(const std::string &path,
                                      const std::string &filename) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.remove<FileQueueEntry>(path, filename);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting ->" << path << "/" << filename
              << " from FileQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteOrphanItemsInQueue(const std::string &path,
                                               const std::string &oldPath) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      std::vector<FileQueueEntry> filesInQ{};
      try {
        filesInQ = m_impl->storage.get_all<FileQueueEntry>(
            where(c(&FileQueueEntry::sync_status) ==
                      syncStatusToString(SyncStatus::RENAME) ||
                  c(&FileQueueEntry::sync_status) ==
                      syncStatusToString(SyncStatus::MODIFIED)));
      } catch (...) {
      }
      m_impl->storage.remove_all<FileQueueEntry>(
          where(c(&FileQueueEntry::path) == oldPath ||
                like(&FileQueueEntry::path, oldPath + "/%")));
      m_impl->storage.remove_all<DirectoryQueueEntry>(
          where(c(&DirectoryQueueEntry::path) == oldPath ||
                like(&DirectoryQueueEntry::path, oldPath + "/%")));
      m_impl->storage.remove_all<FileQueueEntry>(
          where(c(&FileQueueEntry::path) == path ||
                like(&FileQueueEntry::path, path + "/%")));
      m_impl->storage.remove_all<DirectoryQueueEntry>(
          where(c(&DirectoryQueueEntry::path) == path ||
                like(&DirectoryQueueEntry::path, path + "/%")));
      if (!filesInQ.empty()) {
        for (auto &file : filesInQ) {
          m_impl->storage.insert<FileQueueEntry>(file);
        }
      }
      return true;
    });

  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting ->" << path << " from File & Dir Queue =>"
              << e.what() << std::endl;
    return false;
  }
}

// Directory Queue operations
std::optional<std::vector<DirectoryQueueEntry>>
DatabaseManager::getDirectoryQueue() {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {

    return m_impl->storage.get_all<DirectoryQueueEntry>();

  } catch (const std::exception &e) {

    std::cerr << "[DB] Error fetching Directory Queue " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::optional<std::vector<DirectoryMetadata>>
DatabaseManager::getDirsByPath(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.get_all<DirectoryMetadata>(
        where(c(&DirectoryMetadata::path) == path ||
              like(&DirectoryMetadata::path, path + "/%")));

  } catch (const std::exception &e) {
    std::cerr << "[DB] Exception->" << e.what() << std::endl;
    return std::nullopt;
  }
}

bool DatabaseManager::createDirectoryPaths(
    const std::vector<DirectoryMetadata> &dirs) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      for (auto dir : dirs) {
        m_impl->storage.replace<DirectoryMetadata>(dir);
      }
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error directory paths : " << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::insertDirectoryQueue(const DirectoryQueueEntry &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.replace<DirectoryQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error inserting ->" << entry.absPath
              << " into DirectoryQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::updateDirectoryQueue(const DirectoryQueueEntry &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    m_impl->storage.update<DirectoryQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << entry.absPath
              << " from DirectoryQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::insertFilesAndDirectories(const DirectoryMetadata &dir,
                                                const DirectoryQueueEntry &dirQ,
                                                const std::string &path,
                                                const std::string &oldPath) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      m_impl->storage.update_all(
          set(c(&DirectoryMetadata::path) = dir.path,
              c(&DirectoryMetadata::device) = dir.device,
              c(&DirectoryMetadata::folder) = dir.folder,
              c(&DirectoryMetadata::absPath) = dir.absPath),
          where(c(&DirectoryMetadata::uuid) == dir.uuid));

      m_impl->storage.update_all(set(c(&FileMetadata::path) = path),
                                 where(c(&FileMetadata::path) == oldPath));

      m_impl->storage.remove_all<FileQueueEntry>(
          where(c(&FileQueueEntry::path) == oldPath));

      m_impl->storage.remove_all<DirectoryQueueEntry>(
          where(c(&DirectoryQueueEntry::path) == oldPath));

      m_impl->storage.replace<DirectoryQueueEntry>(dirQ);

      return true;
    });

  } catch (...) {
    return false;
  }
}

bool DatabaseManager::deleteDirectoryQueue(const std::string &device,
                                           const std::string &folder,
                                           const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_syncMutex);
  try {
    return m_impl->storage.transaction([&]() {
      auto qFiles = m_impl->storage.get_all<FileQueueEntry>(
          where(c(&FileQueueEntry::path) == path ||
                like(&FileQueueEntry::path, path + "/%")));
      if (qFiles.size() == 0) {
        m_impl->storage.remove<DirectoryQueueEntry>(device, folder, path);
      } else {
        m_impl->storage.update_all(
            set(c(&DirectoryQueueEntry::sync_status) =
                    syncStatusToString(SyncStatus::FILE_LINKED)),
            where(c(&DirectoryQueueEntry::path) == path &&
                  c(&DirectoryQueueEntry::device) == device &&
                  c(&DirectoryQueueEntry::folder) == folder));
      }
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting ->" << path
              << " from DirectoryQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

std::recursive_mutex &DatabaseManager::getSyncMutex() { return m_syncMutex; }

} // namespace sync_app
