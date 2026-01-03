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
          "Directory",
          make_column("uuid", &DirectoryMetadata::uuid, primary_key()),
          make_column("device", &DirectoryMetadata::device),
          make_column("folder", &DirectoryMetadata::folder),
          make_column("path", &DirectoryMetadata::path),
          make_column("created_at", &DirectoryMetadata::created_at),
          make_column("absPath", &DirectoryMetadata::absPath),
          make_column("inode", &DirectoryMetadata::inode),
          sqlite_orm::unique(&DirectoryMetadata::device,
                             &DirectoryMetadata::folder,
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
          make_column("uuid", &DirectoryQueueEntry::uuid, primary_key()),
          make_column("device", &DirectoryQueueEntry::device),
          make_column("folder", &DirectoryQueueEntry::folder),
          make_column("path", &DirectoryQueueEntry::path),
          make_column("created_at", &DirectoryQueueEntry::created_at),
          make_column("sync_status", &DirectoryQueueEntry::sync_status),
          make_column("absPath", &DirectoryQueueEntry::absPath),
          make_column("old_path", &DirectoryQueueEntry::old_path),
          make_column("inode", &DirectoryQueueEntry::inode),
          sqlite_orm::unique(&DirectoryQueueEntry::device,
                             &DirectoryQueueEntry::folder,
                             &DirectoryQueueEntry::path)));
}

// Typedef for easier access within the Impl
using Storage = decltype(create_storage_impl(""));

struct DatabaseManager::Impl {
  Storage storage;
  Impl(const std::string &path) : storage(create_storage_impl(path)) {}
};

DatabaseManager::DatabaseManager(const std::string &dbPath)
    : m_dbPath(dbPath), m_impl(std::make_unique<Impl>(dbPath)) {}

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
std::vector<FileMetadata> DatabaseManager::getAllFiles() {
  return m_impl->storage.get_all<FileMetadata>();
}

std::optional<std::vector<FileQueueEntry>> DatabaseManager::getAllQueueFiles() {
  try {
    return m_impl->storage.get_all<FileQueueEntry>();
  } catch (std::exception &e) {
    std::cerr << "[DB] " << std::endl;
  }
}

std::optional<FileMetadata>
DatabaseManager::getFileByOrigin(const std::string &origin) {
  auto files = m_impl->storage.get_all<FileMetadata>(
      where(c(&FileMetadata::origin) == origin));
  if (files.empty())
    return std::nullopt;
  return files[0];
}

std::optional<FileMetadata>
DatabaseManager::getFileByPath(const std::string &path,
                               const std::string &filename) {
  return m_impl->storage.get_optional<FileMetadata>(path, filename);
}

bool DatabaseManager::insertFile(const FileMetadata &file) {
  try {
    m_impl->storage.insert(file);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::updateFile(const FileMetadata &file) {
  try {
    m_impl->storage.update(file);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::deleteFile(const std::string &origin) {
  try {
    auto allFiles = m_impl->storage.get_all<FileMetadata>();
    for (const auto &f : allFiles) {
      if (f.origin == origin) {
        m_impl->storage.remove<FileMetadata>(f.path, f.filename);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::upsertFile(const FileMetadata &file) {
  try {
    m_impl->storage.replace(file);
    return true;
  } catch (...) {
    return false;
  }
}

// Directory operations
std::vector<DirectoryMetadata> DatabaseManager::getAllDirectories() {
  return m_impl->storage.get_all<DirectoryMetadata>();
}

std::optional<DirectoryMetadata>
DatabaseManager::getDirectoryByPath(const std::string &device,
                                    const std::string &folder,
                                    const std::string &path) {
  auto allDirs = m_impl->storage.get_all<DirectoryMetadata>();
  auto it = std::find_if(
      allDirs.begin(), allDirs.end(), [&](const DirectoryMetadata &d) {
        return d.device == device && d.folder == folder && d.path == path;
      });
  if (it == allDirs.end())
    return std::nullopt;
  return *it;
}

bool DatabaseManager::insertDirectory(const DirectoryMetadata &dir) {
  try {
    m_impl->storage.replace(dir);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::updateDirectory(const DirectoryMetadata &dir) {
  try {
    m_impl->storage.update(dir);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::deleteDirectory(const std::string &uuid) {
  try {
    m_impl->storage.remove<DirectoryMetadata>(uuid);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::upsertDirectory(const DirectoryMetadata &dir) {
  try {
    // Check if directory exists by (device, folder, path)
    auto allDirs = m_impl->storage.get_all<DirectoryMetadata>();
    auto it = std::find_if(
        allDirs.begin(), allDirs.end(), [&](const DirectoryMetadata &e) {
          return e.device == dir.device && e.folder == dir.folder &&
                 e.path == dir.path;
        });

    if (it != allDirs.end()) {
      auto updatedDir = dir;
      updatedDir.uuid = it->uuid; // Preserve UUID
      m_impl->storage.update(updatedDir);
    } else {
      m_impl->storage.replace(dir);
    }
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] upsertDirectory Error: " << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[DB] upsertDirectory Unknown Error" << std::endl;
    return false;
  }
}

bool DatabaseManager::upsertFileQueue(const FileQueueEntry &entry) {
  try {
    m_impl->storage.replace(entry);
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
    // Check if directory queue entry exists by (device, folder, path)
    auto allEntries = m_impl->storage.get_all<DirectoryQueueEntry>();
    auto it = std::find_if(allEntries.begin(), allEntries.end(),
                           [&](const DirectoryQueueEntry &e) {
                             return e.device == entry.device &&
                                    e.folder == entry.folder &&
                                    e.path == entry.path;
                           });

    if (it != allEntries.end()) {
      auto updatedEntry = entry;
      updatedEntry.uuid = it->uuid; // Preserve UUID
      m_impl->storage.update(updatedEntry);
    } else {
      m_impl->storage.replace(entry);
    }
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[DB] upsertDirectoryQueue Error: " << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[DB] upsertDirectoryQueue Unknown Error" << std::endl;
    return false;
  }
}

// File Queue operations
std::vector<FileQueueEntry> DatabaseManager::getFileQueue() {
  return m_impl->storage.get_all<FileQueueEntry>();
}

bool DatabaseManager::insertFileQueue(const FileQueueEntry &entry) {
  try {
    m_impl->storage.replace(entry);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::updateFileQueue(const FileQueueEntry &entry) {
  try {
    m_impl->storage.update(entry);
    return true;
  } catch (...) {
    return false;
  }
}

std::optional<std::vector<DirectoryQueueEntry>>
DatabaseManager::getAllQueueDirectories() {
  try {
    return m_impl->storage.get_all<DirectoryQueueEntry>();
  } catch (const std::exception &e) {
    std::cerr << "[DB] " << e.what() << std::endl;
  }
}

bool DatabaseManager::deleteFileQueue(const std::string &origin) {
  try {
    auto allEntries = m_impl->storage.get_all<FileQueueEntry>();
    for (const auto &e : allEntries) {
      if (e.origin == origin) {
        m_impl->storage.remove<FileQueueEntry>(e.path, e.filename);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Directory Queue operations
std::vector<DirectoryQueueEntry> DatabaseManager::getDirectoryQueue() {
  return m_impl->storage.get_all<DirectoryQueueEntry>();
}

bool DatabaseManager::insertDirectoryQueue(const DirectoryQueueEntry &entry) {
  try {
    m_impl->storage.replace(entry);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::updateDirectoryQueue(const DirectoryQueueEntry &entry) {
  try {
    m_impl->storage.update(entry);
    return true;
  } catch (...) {
    return false;
  }
}

bool DatabaseManager::deleteDirectoryQueue(const std::string &uuid) {
  try {
    m_impl->storage.remove<DirectoryQueueEntry>(uuid);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace sync
