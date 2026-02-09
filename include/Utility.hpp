#pragma once
#include "DatabaseManager.hpp"
#include "types.hpp"
#include <filesystem>
namespace sync_app {

class Utility {
public:
  FileQueueEntry static constructFileQueueEntry(
      const FileMetadata &f, SyncStatus status = SyncStatus::NEW,
      std::string &&old_path = "", std::string &&old_filename = "");
  DirectoryQueueEntry static constructDirectoryQueueEntry(
      const DirectoryMetadata &d, SyncStatus status = SyncStatus::NEW,
      std::string &&old_path = "");
  FileMetadata static constructFileMetadata(const FileQueueEntry &fq);
  DirectoryMetadata static constructDirectoryMetadata(
      const DirectoryQueueEntry &dq);
  std::int64_t static getUnixTimeStamp(
      const std::filesystem::file_time_type &ftime);
  std::string static getInode(const std::string &absPath);
  DirectoryMetadata static createDirectoryMetadata(const std::string &path,
                                                   const std::string &syncPath);
  pathParts static getFolderDevice(const std::filesystem::path &path);
};

} // namespace sync_app
