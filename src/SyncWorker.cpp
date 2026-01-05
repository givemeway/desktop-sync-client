#include "SyncWorker.hpp"
#include "UuidUtils.hpp"
#include "picosha2.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs = std::filesystem;

namespace sync {

SyncWorker::SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
                       const std::string &syncPath)
    : m_dbManager(dbManager), m_scanner(scanner), m_syncPath(syncPath) {}
SyncWorker::~SyncWorker() = default;

void SyncWorker::handleAdded(const std::string &path) {
  std::string type;
  if (std::filesystem::is_directory(path))
    type = "folder";
  else
    type = "file";
  if (type == "file") {
    std::filesystem::path p(path);
    std::string relPath = m_scanner.toRelativePath(path);
    std::string filename = p.filename().generic_string();
    auto file = m_dbManager.getFileByPath(relPath, filename);
    if (!file.has_value()) {
      FileMetadata f;
      FileQueueEntry fq;
      std::cout << "[syncworker] file not found in DB. Adding..." << path
                << std::endl;
      std::ifstream fi(path, std::ios::binary);
      if (!fi.is_open()) {
        std::cout << "[syncworker] unable to read the file: " << filename
                  << std::endl;
        return;
      }

      std::vector<unsigned char> hash(picosha2::k_digest_size);
      picosha2::hash256(fi, hash.begin(), hash.end());
      f.origin = f.uuid = UuidUtils::generate();
      f.path = relPath;
      f.filename = filename;
      std::int64_t unixTimeStamp =
          m_scanner.getUnixTimeStamp(fs::last_write_time(path));
      f.last_modified = std::to_string(unixTimeStamp);
      f.hashvalue = picosha2::bytes_to_hex_string(hash.begin(), hash.end());
      f.size = fs::file_size(path);
      f.inode = m_scanner.getInode(path);
      f.absPath = path;
      f.versions = 1;
      f.lastSyncedHashValue = f.hashvalue;
      pathParts part = m_dbManager.getFolderDevice(fs::path(relPath));
      auto dir =
          m_dbManager.getDirectoryByPath(part.device, part.folder, f.path);
      if (dir.has_value()) {
        f.dirID = dir->uuid;
      } else {
        DirectoryMetadata d;
        DirectoryQueueEntry dq;
        d.absPath = p.parent_path().generic_string();
        d.path = f.path;
        std::int64_t unixTimeStamp =
            m_scanner.getUnixTimeStamp(fs::last_write_time(path));
        d.created_at = std::to_string(unixTimeStamp);
        d.device = part.device;
        d.folder = part.folder;
        d.uuid = UuidUtils::generate();
        d.inode = m_scanner.getInode(path);
        dq = DirectoryMetadata(d);
        dq.old_path = d.path;
        dq.sync_status = "FILE_LINKED";
        m_dbManager.insertDirectory(d);
        m_dbManager.insertDirectoryQueue(dq);
        f.dirID = d.uuid;
      }
      fq = FileMetadata(f);
      fq.old_filename = f.filename;
      fq.old_path = f.path;
      fq.sync_status = "new";
      f.conflictId = "";
      m_dbManager.insertFile(f);
      m_dbManager.insertFileQueue(fq);
    } else {
      std::cout << "[syncworker] File Exists in the DB skipping";
      // do thing as it could be a down sync from cloud to local
      return;
    }
  }
  if (type == "folder") {
    DirectoryMetadata d;
    DirectoryQueueEntry dq;
    d.path = m_scanner.toRelativePath(path);
    pathParts part = m_dbManager.getFolderDevice(fs::path(d.path));
    d.device = part.device;
    d.folder = part.folder;
    d.absPath = path;
    d.inode = m_scanner.getInode(path);
    auto ftime = fs::last_write_time(path);
    d.created_at = std::to_string(m_scanner.getUnixTimeStamp(ftime));
    auto existingDir =
        m_dbManager.getDirectoryByPath(part.device, part.folder, path);
    if (existingDir.has_value()) {
      d.uuid = existingDir->uuid;
    } else {
      d.uuid = UuidUtils::generate();
    }
    dq = DirectoryMetadata(d);
    dq.sync_status = "new";
    dq.old_path = d.path;
    m_dbManager.insertDirectoryQueue(dq);
    m_dbManager.insertDirectory(d);
  }
};
void SyncWorker::handleDeleted(const std::string &path) {};
void SyncWorker::handleRenamed(const std::string &path) {};
void SyncWorker::handleModified(const std::string &path) {};

} // namespace sync
