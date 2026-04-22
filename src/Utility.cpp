#define STB_IMAGE_IMPLEMENTATION
#include "Utility.hpp"
#include "UuidUtils.hpp"
#include "stb_image.h"
#include "types.hpp"

#ifdef _WIN32
#include <windows.h>
#undef DELETE
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

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
std::int64_t Utility::getUnixTimeStamp(const fs::file_time_type &ftime) {
  auto now_file = fs::file_time_type::clock::now();
  auto now_sys = std::chrono::system_clock::now();
  auto file_duration = ftime - now_file;
  auto sys_time =
      now_sys + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    file_duration);
  return std::chrono::duration_cast<std::chrono::seconds>(
             sys_time.time_since_epoch())
      .count();
}

std::string Utility::getInode(const std::string &absPath) {

#ifdef _WIN32

  HANDLE hFile =
      CreateFileW(std::wstring(absPath.begin(), absPath.end()).c_str(), 0,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

  if (hFile == INVALID_HANDLE_VALUE)
    return "";

  BY_HANDLE_FILE_INFORMATION fileInfo;
  std::string inodeStr = "";
  if (GetFileInformationByHandle(hFile, &fileInfo)) {
    inodeStr = std::to_string(fileInfo.nFileIndexHigh) + "-" +
               std::to_string(fileInfo.nFileIndexLow);
  }
  CloseHandle(hFile);
  return inodeStr;
#else
  struct stat st;
  if (stat(absPath.c_str(), &st) == 0) {
    return std::to_string(st.st_ino);
  }
  return "";
#endif
}
pathParts Utility::getFolderDevice(const fs::path &path) {
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

DirectoryMetadata
Utility::createDirectoryMetadata(const std::string &path,
                                 const std::string &syncPath) {
  DirectoryMetadata d;
  pathParts p = getFolderDevice(fs::path(path));
  d.device = p.device;
  d.path = path;
  d.folder = p.folder;
  d.absPath = d.path == "/" ? syncPath : syncPath + d.path;
  d.uuid = UuidUtils::generate();
  try {
    d.inode = getInode(d.absPath);
    d.created_at =
        std::to_string(getUnixTimeStamp(fs::last_write_time(d.absPath)));

  } catch (const std::exception &) {
    d.inode = "";
    d.created_at = "";
  }
  return d;
}

QString Utility::toQ(const std::string &s) { return QString::fromStdString(s); }

} // namespace sync_app
