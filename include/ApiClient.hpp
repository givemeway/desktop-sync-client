#pragma once

#include "types.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
namespace sync_app {

using ProgressCallBack =
    std::function<void(const std::string &key, double progress)>;
/**
 * ApiClient handles communication with the sync server.
 * Uses cpp-httplib for networking and nlohmann/json for serialization.
 */
class ApiClient {
public:
  ApiClient(const std::string &baseUrl, const std::string &userEmail);
  ~ApiClient();

  std::optional<CloudMetadataResult> getMetadata();

  std::unique_ptr<ApiClient> clone() const;
  bool downloadFile(const CloudFileMetadata &file,
                    const std::string &localAbsPath);
  // downloadFile overload to not break existing app using httplib for download
  bool downloadFile(const CloudFileMetadata &file,
                    const std::string &localAbsPath,
                    ProgressCallBack onProgress);

  bool uploadFile(const FileQueueEntry &file,
                  const std::vector<DirectoryMetadata> &pathIds =
                      std::vector<DirectoryMetadata>(),
                  bool isModified = false);
  bool uploadFile(const FileQueueEntry &file,
                  const std::vector<DirectoryMetadata> &pathIds,
                  bool isModified, ProgressCallBack onProgress);

  bool deleteFile(const FileQueueEntry &file);
  bool renameFile(const FileQueueEntry &file,
                  const std::vector<DirectoryMetadata> &dirTree);
  bool moveFile(const FileQueueEntry &file);

  // Directory operations
  bool createFolder(const DirectoryQueueEntry &dir);
  bool deleteFolder(const DirectoryQueueEntry &dir);
  bool moveFolder(const DirectoryQueueEntry &dir, bool isRename = true);

  std::optional<CloudFolderBrowseMetadata>
  getDirectoryContents(const std::string &path);

  bool downloadFileById(const std::string &id);
  bool deleteFileById(const std::string &id);

  std::optional<size_t> getQuotaUsage();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::string m_baseUrl;
  std::string m_userEmail;

  // Helper to parse path strings into device and directory parts
  struct PathParts {
    std::string device;
    std::string directory;
  };
  PathParts parsePath(const std::string &path);
  std::tuple<std::vector<CloudFileMetadata>, std::vector<CloudFolderMetadata>>
  getDirIDs(const std::vector<CloudFileMetadata> &cloudFiles,
            const std::vector<CloudFolderMetadata> &cloudDirs);
  std::vector<std::string> getPathComponents(const std::string &path);
};

} // namespace sync_app
