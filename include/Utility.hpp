#pragma once
#include "types.hpp"

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
};

} // namespace sync_app
