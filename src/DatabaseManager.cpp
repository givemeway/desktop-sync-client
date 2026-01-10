#include "DatabaseManager.hpp"
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <sqlite_orm/sqlite_orm.h>
using namespace sqlite_orm;

namespace sync {

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
  std::cout << "[DB] Synchronizing schema via sqlite_orm..." << std::endl;
  m_impl->storage.sync_schema();
  std::cout << "[DB] Schema synchronized successfully." << std::endl;
}

pathParts DatabaseManager::getFolderDevice(const std::filesystem::path &path) {
  pathParts parts{"", ""};
  if (path.empty())
    return parts;
  parts.device = path.root_name().string();
  parts.folder = path.filename().string();
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
  try {
    return m_impl->storage.get_all<FileMetadata>();
  } catch (const std::exception &e) {
    return std::nullopt;
  }
}

std::optional<std::vector<FileQueueEntry>> DatabaseManager::getAllQueueFiles() {
  try {
    return m_impl->storage.get_all<FileQueueEntry>();
  } catch (std::exception &e) {
    std::cerr << "[DB] " << std::endl;
    return std::nullopt;
  }
}

std::optional<FileMetadata>
DatabaseManager::getFileByOrigin(const std::string &origin) {
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
  try {
    return m_impl->storage.get<FileMetadata>(path, filename);
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Fetching FileByPath :" << e.what() << "\n";
    return std::nullopt;
  }
}

std::optional<FileQueueEntry>
DatabaseManager::getFileQueueByPath(const std::string &path,
                                    const std::string &filename) {
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

bool DatabaseManager::insertFile(const FileMetadata &file,
                                 const FileQueueEntry &fileQueue) {
  try {
    return m_impl->storage.transaction([&]() {
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
  try {
    auto existingFile = m_impl->storage.count<FileMetadata>(
        where(c(&FileMetadata::path) == file.path &&
              c(&FileMetadata::filename) == file.filename));

    if (existingFile) {
      m_impl->storage.update<FileMetadata>(file);
    }
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << file.absPath << " in File Table =>"
              << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteFile(const std::string &path,
                                 const std::string &filename,
                                 const FileQueueEntry &fq) {
  try {
    return m_impl->storage.transaction([&] {
      m_impl->storage.remove<FileMetadata>(path, filename);
      m_impl->storage.replace<FileQueueEntry>(fq);
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Deleting ->" << path << "/" << filename
              << " from File Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertFile(const FileMetadata &file) {
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
  try {
    return m_impl->storage.get_all<DirectoryMetadata>();
  } catch (const std::exception &e) {
    return std::nullopt;
  }
}

std::optional<DirectoryMetadata>
DatabaseManager::getDirectoryByPath(const std::string &device,
                                    const std::string &folder,
                                    const std::string &path) {
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

std::optional<std::vector<FileMetadata>>
DatabaseManager::getAllFilesInDirectory(const std::string &path) {
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

bool DatabaseManager::insertDirectory(const DirectoryMetadata &dir,
                                      const DirectoryQueueEntry &dirQueue) {
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

bool DatabaseManager::updateDirectory(const DirectoryMetadata &dir) {
  try {
    m_impl->storage.update<DirectoryMetadata>(dir);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << dir.path
              << " into Directory Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteDirectory(const std::string &path) {
  try {
    m_impl->storage.remove_all<DirectoryMetadata>(
        where(c(&DirectoryMetadata::path) == path ||
              like(&DirectoryMetadata::path, path + "/%")));
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting ->" << path << " from Directory Table =>"
              << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteFolderWithTransaction(
    const std::string &path, const DirectoryQueueEntry &dq) {
  try {
    return m_impl->storage.transaction([&]() {
      m_impl->storage.remove_all<FileMetadata>(
          where(c(&FileMetadata::path) == path ||
                like(&FileMetadata::path, path + "/%")));
      m_impl->storage.remove_all<DirectoryMetadata>(
          where(c(&DirectoryMetadata::path) == path ||
                like(&DirectoryMetadata::path, path + "/%")));
      std::vector<DirectoryQueueEntry> queueDirs =
          m_impl->storage.get_all<DirectoryQueueEntry>(
              where(c(&DirectoryQueueEntry::path) == path ||
                    like(&DirectoryQueueEntry::path, path + "/%")));
      if (queueDirs.size() > 0) {
        m_impl->storage.remove_all<FileQueueEntry>(
            where(c(&FileQueueEntry::path) == path ||
                  like(&FileQueueEntry::path, path + "/%")));
        m_impl->storage.remove_all<DirectoryQueueEntry>(
            where(c(&DirectoryQueueEntry::path) == path ||
                  like(&DirectoryQueueEntry::path, path + "/%")));
      }
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
                                    const DirectoryQueueEntry &dq) {
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
      return true;
    });
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error Moving Directory ->" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertDirectory(const DirectoryMetadata &dir) {
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
  try {
    return m_impl->storage.get_all<FileQueueEntry>();
  } catch (const std::exception &e) {
    return std::nullopt;
  }
}

bool DatabaseManager::insertFileQueue(const FileQueueEntry &entry) {
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
  try {
    return m_impl->storage.get_all<DirectoryQueueEntry>();
  } catch (const std::exception &e) {
    std::cerr << "[DB] " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool DatabaseManager::deleteFileQueue(const std::string &path,
                                      const std::string &filename) {
  try {
    m_impl->storage.remove<FileQueueEntry>(path, filename);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting ->" << path << "/" << filename
              << " from FileQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

// Directory Queue operations
std::optional<std::vector<DirectoryQueueEntry>>
DatabaseManager::getDirectoryQueue() {
  try {

    return m_impl->storage.get_all<DirectoryQueueEntry>();

  } catch (const std::exception &e) {

    std::cerr << "[DB] Error fetching Directory Queue " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

bool DatabaseManager::insertDirectoryQueue(const DirectoryQueueEntry &entry) {
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
  try {
    m_impl->storage.update<DirectoryQueueEntry>(entry);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error updating ->" << entry.absPath
              << " from DirectoryQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

bool DatabaseManager::deleteDirectoryQueue(const std::string &uuid) {
  try {
    m_impl->storage.remove<DirectoryQueueEntry>(
        where(c(&DirectoryQueueEntry::uuid) == uuid));
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] Error deleting ->" << uuid
              << " from DirectoryQueue Table =>" << e.what() << std::endl;
    return false;
  }
}

} // namespace sync
