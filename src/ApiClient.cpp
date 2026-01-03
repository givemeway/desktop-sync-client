#include "ApiClient.hpp"
#include "httplib.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace sync {

// Helper for URL encoding
std::string urlEncode(const std::string &value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (auto i = value.begin(), n = value.end(); i != n; ++i) {
    std::string::value_type c = (*i);
    // Keep alphanumeric and other safe characters
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
      continue;
    }
    // Any other characters are percent-encoded
    escaped << std::uppercase;
    escaped << '%' << std::setw(2) << int((unsigned char)c);
    escaped << std::nouppercase;
  }

  return escaped.str();
}

struct ApiClient::Impl {
  httplib::Client client;
  Impl(const std::string &baseUrl) : client(baseUrl) {
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);
    client.set_follow_location(true);
  }
};

ApiClient::ApiClient(const std::string &baseUrl, const std::string &userEmail)
    : m_baseUrl(baseUrl), m_userEmail(userEmail),
      m_impl(std::make_unique<Impl>(baseUrl)) {}

ApiClient::~ApiClient() = default;

std::optional<CloudMetadataResult> ApiClient::getMetadata() {
  std::string path = "/getSyncItems?username=" + urlEncode(m_userEmail);
  auto res = m_impl->client.Get(path.c_str());

  if (res && res->status == 200) {
    try {
      auto data = json::parse(res->body);
      CloudMetadataResult result;
      result.success = true;

      for (const auto &item : data["items"]) {
        if (item["type"] == "file") {
          CloudFileMetadata file;
          file.uuid = item["uuid"];
          file.filename = item["filename"];
          file.path = "/"; // Fallback
          if (item.contains("device") && item.contains("directory")) {
            std::string device = item["device"];
            std::string directory = item["directory"];
            if (device == "/")
              file.path = "/";
            else if (directory == "/")
              file.path = "/" + device;
            else
              file.path = "/" + device + "/" + directory;
          }
          file.origin = item["origin"];
          file.hashvalue = item["checksum"];
          file.size = item["size"];
          file.last_modified = item["mtime"];
          file.versions = item["version"];
          if (item.contains("conflictId") && !item["conflictId"].is_null()) {
            file.conflictId = item["conflictId"].get<std::string>();
          }
          result.files.push_back(file);
        } else {
          CloudFolderMetadata folder;
          folder.uuid = item["uuid"];
          folder.device = item["device"];
          folder.folder = item["folder"];
          folder.path = item["path"];
          if (item.contains("created_at"))
            folder.created_at = item["created_at"];
          result.directories.push_back(folder);
        }
      }
      return result;
    } catch (const std::exception &e) {
      std::cerr << "[API] JSON Parse Error: " << e.what() << std::endl;
    }
  } else {
    std::cerr << "[API] Request failed with status: "
              << (res ? res->status : -1) << std::endl;
  }
  return std::nullopt;
}

bool ApiClient::downloadFile(const CloudFileMetadata &file,
                             const std::string &localAbsPath) {
  auto parts = parsePath(file.path);

  std::string query = "/syncDownFile?file=" + urlEncode(file.filename) +
                      "&dir=" + urlEncode(parts.directory) +
                      "&device=" + urlEncode(parts.device) +
                      "&uuid=" + urlEncode(file.uuid) +
                      "&db=file&username=" + urlEncode(m_userEmail);

  std::ofstream ofs(localAbsPath, std::ios::binary);
  if (!ofs)
    return false;

  auto res = m_impl->client.Get(query.c_str(),
                                [&](const char *data, size_t data_length) {
                                  ofs.write(data, data_length);
                                  return true;
                                });

  return res && res->status == 200;
}

std::optional<std::string>
ApiClient::uploadFile(const FileQueueEntry &file,
                      const std::vector<std::string> &pathIds) {
  std::ifstream ifs(file.absPath, std::ios::binary);
  if (!ifs)
    return std::nullopt;

  std::string content((std::istreambuf_iterator<char>(ifs)),
                      (std::istreambuf_iterator<char>()));

  auto parts = parsePath(file.path);
  json filestat;
  filestat["filename"] = file.filename;
  filestat["directory"] = parts.directory;
  filestat["device"] = parts.device;
  filestat["uuid"] = file.uuid;
  filestat["origin"] = file.origin;
  filestat["checksum"] = file.hashvalue;
  filestat["size"] = file.size;
  filestat["mtime"] = file.last_modified;
  filestat["username"] = m_userEmail;
  filestat["version"] = file.versions;
  filestat["isModified"] = (file.sync_status == "modified");
  filestat["pathids"] = pathIds;
  filestat["type"] = file.filename.substr(file.filename.find_last_of(".") + 1);

  httplib::UploadFormDataItems items = {
      {"file", content, file.filename, "application/octet-stream"},
      {"filestat", filestat.dump(), "", "application/json"}};

  auto res = m_impl->client.Post("/syncUpFile", items);
  if (res && res->status == 200) {
    auto resJson = json::parse(res->body);
    return resJson["id"].get<std::string>();
  }

  return std::nullopt;
}

bool ApiClient::deleteFile(const FileQueueEntry &file) {
  auto parts = parsePath(file.path);
  json data;
  data["username"] = m_userEmail;
  data["directories"] = json::array();

  json fileId;
  fileId["id"] = file.uuid;
  fileId["origin"] = file.uuid;
  fileId["dir"] = parts.directory;
  fileId["versions"] = 1;

  std::string pathInfo = "device=" + urlEncode(parts.device) +
                         "&dir=" + urlEncode(parts.directory) +
                         "&file=" + urlEncode(file.filename);
  fileId["path"] = pathInfo;

  data["fileIds"] = json::array({fileId});

  auto res =
      m_impl->client.Delete("/deleteFiles", data.dump(), "application/json");
  return res && res->status == 200;
}

bool ApiClient::renameFile(const FileQueueEntry &file) {
  auto parts = parsePath(file.path);
  json innerData;
  innerData["type"] = "fi";
  innerData["dir"] = parts.directory;
  innerData["device"] = parts.device;
  innerData["filename"] = file.old_filename.value_or("");
  innerData["to"] = file.filename;
  innerData["origin"] = file.origin;
  innerData["username"] = m_userEmail;

  json outerData;
  outerData["data"] = innerData;

  auto res =
      m_impl->client.Post("/renameFile", outerData.dump(), "application/json");
  return res && res->status == 200;
}

bool ApiClient::createFolder(const DirectoryMetadata &dir) {
  std::string query = "/createFolder?path=" + urlEncode(dir.path) +
                      "&device=" + urlEncode(dir.device) +
                      "&username=" + urlEncode(m_userEmail) +
                      "&uuid=" + urlEncode(dir.uuid) +
                      "&folder=" + urlEncode(dir.folder);

  auto res = m_impl->client.Post(query.c_str());
  return res && res->status == 200;
}

bool ApiClient::deleteFolder(const DirectoryMetadata &dir) {
  auto parts = parsePath(dir.path);
  std::string query = "/deleteFolder?path=" + urlEncode(dir.path) +
                      "&folder=" + urlEncode(dir.folder) +
                      "&directory=" + urlEncode(parts.directory) +
                      "&username=" + urlEncode(m_userEmail) +
                      "&device=" + urlEncode(dir.device);

  auto res = m_impl->client.Delete(query.c_str());
  return res && res->status == 200;
}

bool ApiClient::renameFolder(const DirectoryQueueEntry &dir) {
  json data;
  data["oldPath"] = dir.old_path.value_or("");
  data["newPath"] = dir.path;
  data["username"] = m_userEmail;

  auto res =
      m_impl->client.Post("/renameFolder", data.dump(), "application/json");
  return res && res->status == 200;
}

ApiClient::PathParts ApiClient::parsePath(const std::string &path) {
  if (path.empty() || path == "/")
    return {"/", "/"};

  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string item;
  while (std::getline(ss, item, '/')) {
    if (!item.empty())
      parts.push_back(item);
  }

  if (parts.empty())
    return {"/", "/"};

  std::string device = parts[0];
  std::string directory = "/";
  if (parts.size() > 1) {
    for (size_t i = 1; i < parts.size(); ++i) {
      directory += parts[i] + (i == parts.size() - 1 ? "" : "/");
    }
  }

  return {device, directory};
}

} // namespace sync
