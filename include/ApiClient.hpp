#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include "types.hpp"

namespace sync {

    /**
     * ApiClient handles communication with the sync server.
     * Uses cpp-httplib for networking and nlohmann/json for serialization.
     */
    class ApiClient {
    public:
        ApiClient(const std::string& baseUrl, const std::string& userEmail);
        ~ApiClient();

        // Metadata fetching
        std::optional<CloudMetadataResult> getMetadata();

        // File operations
        bool downloadFile(const CloudFileMetadata& file, const std::string& localAbsPath);
        std::optional<std::string> uploadFile(const FileQueueEntry& file, const std::vector<std::string>& pathIds);
        bool deleteFile(const FileQueueEntry& file);
        bool renameFile(const FileQueueEntry& file);

        // Directory operations
        bool createFolder(const DirectoryMetadata& dir);
        bool deleteFolder(const DirectoryMetadata& dir);
        bool renameFolder(const DirectoryQueueEntry& dir);

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
        PathParts parsePath(const std::string& path);
    };

} // namespace sync
