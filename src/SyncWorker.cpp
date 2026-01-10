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
        m_dbManager.insertDirectory(d, dq);
        // m_dbManager.insertDirectoryQueue(dq);
        f.dirID = d.uuid;
      }
      fq = FileMetadata(f);
      fq.old_filename = f.filename;
      fq.old_path = f.path;
      fq.sync_status = "new";
      f.conflictId = "";
      m_dbManager.insertFile(f, fq);
      //      m_dbManager.insertFileQueue(fq);
    } else {
      std::cout << "[syncworker] File Exists in the DB skipping";
      // do think as it could be a down sync from cloud to local
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
    //  m_dbManager.insertDirectoryQueue(dq);
    m_dbManager.insertDirectory(d, dq);
  }
};
void SyncWorker::handleDeleted(const std::string &path) {
  fs::path fp(path);
  fs::path base(m_syncPath);
  std::string relPath = "/" + fs::relative(fp, base).generic_string();
  relPath = m_scanner.normalizePathSeparators(relPath);
  pathParts p = m_dbManager.getFolderDevice(fs::path(relPath));
  auto existingDir =
      m_dbManager.getDirectoryByPath(p.device, p.folder, relPath);
  if (existingDir.has_value()) {
    DirectoryQueueEntry dq(*existingDir);
    dq.sync_status = "delete";
    dq.old_path = dq.path;
    m_dbManager.deleteFolderWithTransaction(relPath, dq);
  } else {
    std::string filePath = fs::path(relPath).parent_path().generic_string();
    std::string filename = fs::path(relPath).filename().generic_string();
    auto existingFile = m_dbManager.getFileByPath(filePath, filename);
    if (existingFile.has_value()) {
      FileQueueEntry fq(*existingFile);
      fq.old_path = fq.path;
      fq.old_filename = fq.filename;
      fq.sync_status = "delete";
      m_dbManager.deleteFile(existingFile->path, existingFile->filename, fq);
      //      m_dbManager.insertFileQueue(fq);
    }
  }
};

void SyncWorker::handleRenamed(const std::string &path,
                               const std::string &oldPath) {
  if (oldPath.empty()) {
    return;
  }
  if (fs::is_directory(path)) {
    fs::path fp(oldPath);
    fs::path base(m_syncPath);
    std::string oldRelPath = "/" + fs::relative(fp, base).generic_string();
    oldRelPath = m_scanner.normalizePathSeparators(oldRelPath);
    std::string relPath = m_scanner.toRelativePath(path);
    pathParts o = m_dbManager.getFolderDevice(fs::path(oldRelPath));
    pathParts n = m_dbManager.getFolderDevice(fs::path(relPath));
    auto existingDir =
        m_dbManager.getDirectoryByPath(o.device, o.folder, oldRelPath);
    if (existingDir.has_value()) {
      DirectoryQueueEntry dq(*existingDir);
      dq.sync_status = "rename";
      dq.old_path = oldRelPath;
      dq.path = relPath;
      dq.absPath = path;
      dq.device = n.device;
      dq.folder = n.folder;
      m_dbManager.moveDirectory(relPath, oldRelPath, dq);
    } else {
      std::cout << "[syncworker] old folder name not found in DB. It has to be "
                   "added as new folder"
                << std::endl;
      handleAdded(path);
    }
  } else {
    // file renamed
    fs::path p(path);
    fs::path op(oldPath);
    std::string relPath = m_scanner.toRelativePath(path);
    std::string oldRelPath =
        "/" + fs::relative(oldPath, m_syncPath).parent_path().generic_string();
    oldRelPath = m_scanner.normalizePathSeparators(oldRelPath);
    std::string filename = p.filename().generic_string();
    std::string oldFileName = op.filename().generic_string();
    auto file = m_dbManager.getFileByPath(oldRelPath, oldFileName);
    if (file.has_value()) {
      FileMetadata f;
      FileQueueEntry fq;
      std::ifstream fi(path, std::ios::binary);
      if (!fi.is_open()) {
        std::cout << "[syncworker] unable to read the file: " << path
                  << std::endl;
        return;
      }
      std::vector<unsigned char> hash(picosha2::k_digest_size);
      picosha2::hash256(fi, hash.begin(), hash.end());
      f.origin = file->origin;
      f.uuid = file->uuid;
      f.path = relPath;
      f.filename = filename;
      std::int64_t unixTimeStamp =
          m_scanner.getUnixTimeStamp(fs::last_write_time(path));
      f.last_modified = std::to_string(unixTimeStamp);
      f.hashvalue = picosha2::bytes_to_hex_string(hash.begin(), hash.end());
      f.size = fs::file_size(path);
      f.inode = m_scanner.getInode(path);
      f.absPath = path;
      f.versions = file->versions;
      f.lastSyncedHashValue = file->lastSyncedHashValue;
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
        auto dirCreateResult = m_dbManager.insertDirectory(d, dq);
        //        auto dirQueueCreateResult =
        //        m_dbManager.insertDirectoryQueue(dq);
        if (dirCreateResult)
          f.dirID = d.uuid;
        else
          return;
      }
      fq = FileMetadata(f);
      fq.old_filename = oldFileName;
      fq.old_path = oldRelPath;
      fq.sync_status = "rename";
      f.conflictId = "";
      m_dbManager.insertFile(f, fq);
      //      m_dbManager.insertFileQueue(fq);
    } else {
      std::cout << "[syncworker] oldFileName does not exist in the DB needs to "
                   "be added "
                   "as new file"
                << std::endl;
      handleAdded(path);
    }
  }
};

void SyncWorker::handleModified(const std::string &path) {
  if (!fs::is_directory(path)) {
    FileMetadata f;
    FileQueueEntry fq;
    std::ifstream fi(path, std::ios::binary);
    if (!fi.is_open()) {
      std::cerr << "[syncworker] Error reading file: " << path << std::endl;
      return;
    }
    f.filename = fs::path(path).filename().generic_string();
    f.path = m_scanner.toRelativePath(path);
    std::cout << "[syncworker] fileModified path " << f.path << "/"
              << f.filename << std::endl;
    auto existingFile = m_dbManager.getFileByPath(f.path, f.filename);
    if (existingFile.has_value()) {
      std::vector<unsigned char> hash(picosha2::k_digest_size);
      picosha2::hash256(fi, hash.begin(), hash.end());
      f.absPath = path;
      f.inode = m_scanner.getInode(path);
      f.hashvalue = picosha2::bytes_to_hex_string(hash.begin(), hash.end());
      f.lastSyncedHashValue = existingFile->lastSyncedHashValue;
      f.origin = existingFile->origin;
      f.uuid = UuidUtils::generate();
      f.last_modified =
          std::to_string(m_scanner.getUnixTimeStamp(fs::last_write_time(path)));
      f.versions = existingFile->versions + 1;
      f.size = fs::file_size(path);
      f.dirID = existingFile->dirID;
      fq = FileMetadata(f);
      fq.sync_status = "modified";
      fq.old_path = f.path;
      fq.old_filename = f.filename;
      f.conflictId = "";
      m_dbManager.insertFile(f, fq);
      //      m_dbManager.upsertFileQueue(fq);
    }
  }
};

} // namespace sync
