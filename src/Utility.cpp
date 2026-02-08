#include "Utility.hpp"
#include "types.hpp"
namespace sync_app {
FileQueueEntry Utility::constructFileQueueEntry(const FileMetadata &f,
                                                SyncStatus status,
                                                std::string &&old_path,
                                                std::string &&old_filename) {

  FileQueueEntry fq;
  fq.filename = f.filename;
  fq.path = f.path;
  fq.inode = f.inode;
  fq.size = f.size;
  fq.last_modified = f.last_modified;
  fq.hashvalue = f.hashvalue;
  fq.lastSyncedHashValue = f.lastSyncedHashValue;
  fq.versions = f.versions;
  fq.origin = f.origin;
  fq.dirID = f.dirID;
  fq.uuid = f.uuid;
  fq.absPath = f.absPath;
  fq.old_path = std::move(old_path);
  fq.old_filename = std::move(old_filename);
  fq.sync_status = syncStatusToString(status);
  return fq;
}

DirectoryQueueEntry Utility::constructDirectoryQueueEntry(
    const DirectoryMetadata &d, SyncStatus status, std::string &&old_path) {

  DirectoryQueueEntry dq;
  dq.uuid = d.uuid;
  dq.device = d.device;
  dq.folder = d.folder;
  dq.path = d.path;
  dq.absPath = d.absPath;
  dq.created_at = d.created_at;
  dq.inode = d.inode;
  dq.lastSynced = d.lastSynced;
  dq.old_path = std::move(old_path);
  dq.sync_status = syncStatusToString(status);
  return dq;
}
FileMetadata Utility::constructFileMetadata(const FileQueueEntry &f) {

  FileMetadata fq;
  fq.filename = f.filename;
  fq.path = f.path;
  fq.inode = f.inode;
  fq.size = f.size;
  fq.last_modified = f.last_modified;
  fq.hashvalue = f.hashvalue;
  fq.lastSyncedHashValue = f.lastSyncedHashValue;
  fq.versions = f.versions;
  fq.origin = f.origin;
  fq.dirID = f.dirID;
  fq.uuid = f.uuid;
  fq.absPath = f.absPath;
  return fq;
}
DirectoryMetadata
Utility::constructDirectoryMetadata(const DirectoryQueueEntry &d) {

  DirectoryMetadata dq;
  dq.uuid = d.uuid;
  dq.device = d.device;
  dq.folder = d.folder;
  dq.path = d.path;
  dq.absPath = d.absPath;
  dq.created_at = d.created_at;
  dq.inode = d.inode;
  dq.lastSynced = d.lastSynced;
  return dq;
}
} // namespace sync_app
