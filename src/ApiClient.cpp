#include "ApiClient.hpp"
#include "MimeTypeDetector.hpp"
#include "httplib.h"
#include <curl/curl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <tuple>
using json = nlohmann::json;

namespace sync_app {

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
  CURLM *curlMulti;

  Impl(const std::string &baseUrl) : client(baseUrl) {
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);
    client.set_follow_location(true);

    curl_global_init(CURL_GLOBAL_ALL);
    curlMulti = curl_multi_init();
  }

  ~Impl() {
    curl_easy_cleanup(curlMulti);
    curl_global_cleanup();
  }

  // ── Creates a fresh easy handle per transfer ──────────────────
  CURL *createHandle(const std::string &url) {
    CURL *handle = curl_easy_init();
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 0L);
    return handle;
  }

  // ── Returns CURLcode so caller can check it ───────────────────
  CURLcode perform(CURL *handle) {
    curl_multi_add_handle(curlMulti, handle);

    int stillRunning = 0;
    do {
      curl_multi_perform(curlMulti, &stillRunning);
      curl_multi_wait(curlMulti, nullptr, 0, 100, nullptr);
    } while (stillRunning);

    curl_multi_remove_handle(curlMulti, handle);

    // ── Get result from the completed transfer ────────────────
    CURLcode result = CURLE_OK;
    CURLMsg *msg = nullptr;
    int msgsLeft = 0;

    while ((msg = curl_multi_info_read(curlMulti, &msgsLeft))) {
      if (msg->msg == CURLMSG_DONE && msg->easy_handle == handle) {
        result = msg->data.result; // ← actual curl result
        break;
      }
    }

    return result;
  }
};

ApiClient::ApiClient(const std::string &baseUrl, const std::string &userEmail)
    : m_baseUrl(baseUrl), m_userEmail(userEmail),
      m_impl(std::make_unique<Impl>(baseUrl)) {}

std::unique_ptr<ApiClient> ApiClient::clone() const {
  return std::make_unique<ApiClient>(m_baseUrl, m_userEmail);
}

ApiClient::~ApiClient() = default;

std::vector<std::string> ApiClient::getPathComponents(const std::string &path) {
  if (path == "/" || path.empty()) {
    return {"/"};
  }

  std::vector<std::string> pathTree;
  std::string currentPath = "";
  std::stringstream ss(path);
  std::string token;

  while (std::getline(ss, token, '/')) {
    if (token.empty())
      continue;
    currentPath += "/" + token;
    pathTree.push_back(currentPath);
  }

  if (pathTree.empty()) {
    return {"/"};
  }
  return pathTree;
}

std::tuple<std::vector<CloudFileMetadata>, std::vector<CloudFolderMetadata>>

ApiClient::getDirIDs(const std::vector<CloudFileMetadata> &cloudFiles,
                     const std::vector<CloudFolderMetadata> &cloudDirs) {
  std::map<std::string, std::string> dirIDMap;
  for (auto &d : cloudDirs) {
    dirIDMap[d.path] = d.uuid;
  }
  std::vector<CloudFileMetadata> cf(cloudFiles);
  std::vector<CloudFolderMetadata> cd(cloudDirs);
  for (auto &f : cf) {
    if (f.path == "/") {
      if (!f.dirIDs)
        f.dirIDs = std::map<std::string, std::string>();
      (*f.dirIDs)["/"] = f.dirID;
      continue;
    }
    auto filePaths = getPathComponents(f.path);
    for (std::string path : filePaths) {
      auto it = dirIDMap.find(path);
      if (it != dirIDMap.end()) {
        if (!f.dirIDs) {
          f.dirIDs = std::map<std::string, std::string>();
        }
        (*f.dirIDs)[path] = it->second;
      }
    }
  }
  for (auto &d : cd) {
    if (d.path == "/") {
      if (!d.dirIDs)
        d.dirIDs = std::map<std::string, std::string>();
      (*d.dirIDs)["/"] = d.uuid;
      continue;
    }
    auto folderPaths = getPathComponents(d.path);
    for (std::string path : folderPaths) {
      auto it = dirIDMap.find(path);
      if (it != dirIDMap.end()) {
        if (!d.dirIDs) {
          d.dirIDs = std::map<std::string, std::string>();
        }
        (*d.dirIDs)[path] = it->second;
      }
    }
  }
  return {cf, cd};
}

std::optional<CloudMetadataResult> ApiClient::getMetadata() {
  std::string path =
      "/app/sync/getSyncItems?username=" + urlEncode(m_userEmail);
  auto res = m_impl->client.Get(path.c_str());
  std::cout << "[API] url ->" << path << "\n";
  if (res && res->status == 200) {
    auto data = json::parse(res->body);
    std::cout << "[API] Get request Successful!! Parsing Body..." << "\n";
    CloudMetadataResult result;
    if (data.is_object() && data.contains("items") &&
        data["items"].is_array()) {
      result.success = true;
      for (const auto &item : data["items"]) {
        std::string type = item.value("type", "");
        if (type == "file") {
          result.files.push_back(item.get<CloudFileMetadata>());
        } else if (type == "folder") {
          result.directories.push_back(item.get<CloudFolderMetadata>());
        }
      }
      auto [cloudFiles, cloudDirs] =
          getDirIDs(result.files, result.directories);
      result.files = cloudFiles;
      result.directories = cloudDirs;
      return result;
    } else {
      std::cout << "[API] Parsing Error -> Assertion Failed" << "\n";
      return std::nullopt;
    }

  } else {
    std::cerr << "[API] Request failed -> " << res.error() << "\n";
    return std::nullopt;
  }
}

bool ApiClient::downloadFile(const CloudFileMetadata &file,
                             const std::string &localAbsPath) {

  std::cout << "[API] Starting download for: " << file.filename << " -> \n";
  auto parts = parsePath(file.path);
  std::string mtime;

  std::string query =
      "/app/sync/syncDownFile?file=" + urlEncode(file.filename) +
      "&dir=" + urlEncode(parts.directory) +
      "&device=" + urlEncode(parts.device) + "&uuid=" + urlEncode(file.uuid) +
      "&db=file&username=" + urlEncode(m_userEmail);

  std::string filePath(
      std::filesystem::path(localAbsPath).parent_path().generic_string());

  std::ofstream ofs(localAbsPath, std::ios::binary);

  if (!ofs) {
    std::cerr << "[API] exception " << ofs.exceptions()
              << " opening the file -> " << localAbsPath << "\n";
    return false;
  }

  auto res = m_impl->client.Get(
      query.c_str(),
      [&mtime](const httplib::Response &response) {
        if (response.status != 200) {
          if (response.status == 404) {
            std::cerr << "[API] file not found in cloud. Status Code: "
                      << response.status << std::endl;
            return true;
          }
          std::cerr << "[API] Response failed. Status Code: " << response.status
                    << "\n";
          return false;
        }
        mtime = response.get_header_value("mtime");
        if (mtime.empty()) {
          std::cerr << "[API] Missing mtime header" << "\n";
          return false;
        }
        return true;
      },
      [&](const char *data, size_t data_length) {
        ofs.write(data, data_length);
        return true;
      });

  ofs.close();

  bool success = res && res->status == 200;
  if (success) {
    std::cout << "[API] Download successful for: " << file.filename << "\n";
  } else {
    std::cerr << "[API] Download failed for: " << file.filename << "\n";
  }
  return success;
}

bool ApiClient::downloadFile(const CloudFileMetadata &file,
                             const std::string &localAbsPath,
                             ProgressCallBack onProgress) {
  auto parts = parsePath(file.path);

  std::string url =
      m_baseUrl + "/app/sync/syncDownFile?file=" + urlEncode(file.filename) +
      "&dir=" + urlEncode(parts.directory) +
      "&device=" + urlEncode(parts.device) + "&uuid=" + urlEncode(file.uuid) +
      "&db=file&username=" + urlEncode(m_userEmail);

  std::ofstream ofs(localAbsPath, std::ios::binary);
  if (!ofs) {
    std::cerr << "[API] Failed to open file: " << localAbsPath << "\n";
    return false;
  }

  // ── Progress state ────────────────────────────────────────────
  struct ProgressData {
    ProgressCallBack callback;
    std::string key;
  };
  ProgressData progressData{onProgress, file.uuid};

  // ── Write callback ────────────────────────────────────────────
  auto writeCallback = [](void *ptr, size_t size, size_t nmemb,
                          void *userdata) -> size_t {
    auto *ofs = static_cast<std::ofstream *>(userdata);
    ofs->write(static_cast<const char *>(ptr), size * nmemb);
    return size * nmemb;
  };

  // ── Progress callback ─────────────────────────────────────────
  auto progressCallback = [](void *userdata, curl_off_t dltotal,
                             curl_off_t dlnow, curl_off_t, curl_off_t) -> int {
    if (dltotal <= 0)
      return 0;
    auto *pd = static_cast<ProgressData *>(userdata);
    double progress = (double)dlnow / (double)dltotal * 100.0;
    if (pd->callback)
      pd->callback(pd->key, progress);
    return 0;
  };

  // ── One easy handle per transfer — added to multi ─────────────
  CURL *handle = m_impl->createHandle(url);

  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, +writeCallback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &ofs);
  curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, +progressCallback);
  curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progressData);
  curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);

  CURLcode curlResult = m_impl->perform(handle); // runs transfer

  long httpCode = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_easy_cleanup(handle); // clean up this transfer's handle
  ofs.close();

  // ── Check both curl result AND http status ────────────────────
  if (curlResult != CURLE_OK) {
    std::cerr << "[API] Curl error: " << curl_easy_strerror(curlResult) << "\n";
    return false;
  }

  if (httpCode != 200) {
    std::cerr << "[API] HTTP error: " << httpCode << "\n";
    return false;
  }

  return true;
}

bool ApiClient::uploadFile(const FileQueueEntry &file,
                           const std::vector<DirectoryMetadata> &pathIds,
                           bool isModified) {
  try {
    std::ifstream ifs(file.absPath, std::ios::binary | std::ios::ate);
    if (!ifs) {
      std::cerr << "[API] Exception Opening a file: " << file.absPath << "\n";
      return false;
    }
    std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    // Limit memory usage for very large files if needed,
    // but for now let's at least avoid unnecessary reallocations
    std::string content;
    content.reserve(fileSize);
    content.assign((std::istreambuf_iterator<char>(ifs)),
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
    filestat["isModified"] = isModified;
    if (!isModified)
      filestat["pathids"] = pathIds;
    // Detect MIME type on-the-fly using libmagic
    MimeTypeDetector detector;
    std::string detectedMime = detector.getMimeType(file.absPath);
    magic_t magicCookie = *detector.getMagicCookie();
    if (!detectedMime.empty()) {
      filestat["type"] = detectedMime;
      if (detectedMime.starts_with("image/")) {
        auto maybeDims = detector.getImageDims(file.absPath, magicCookie);
        if (maybeDims.has_value()) {
          filestat["height"] = maybeDims->height;
          filestat["width"] = maybeDims->width;
        } else {
          filestat["height"] = 0;
          filestat["width"] = 0;
        }
      }
    } else {
      filestat["type"] = "file";
    }
    httplib::UploadFormDataItems items = {
        {"file", content, file.filename, "application/octet-stream"}};

    httplib::Headers headers = {{"filestat", filestat.dump()}};

    auto res = m_impl->client.Post("/app/sync/syncUpFile", headers, items);

    return res && res->status == 200;
  } catch (const std::exception &e) {
    std::cerr << "[API] " << e.what() << "\n";
    return false;
  }
}

bool ApiClient::uploadFile(const FileQueueEntry &file,
                           const std::vector<DirectoryMetadata> &pathIds,
                           bool isModified, ProgressCallBack onProgress) {
  try {
    // ── Don't read file into memory — let curl handle it ──────
    // just check it exists and get size
    std::ifstream testFile(file.absPath, std::ios::binary | std::ios::ate);
    if (!testFile) {
      std::cerr << "[API] Failed to open: " << file.absPath << "\n";
      return false;
    }
    std::streamsize fileSize = testFile.tellg();
    testFile.close(); // close immediately — curl will open it

    // ── Build filestat ────────────────────────────────────────
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
    filestat["isModified"] = isModified;
    if (!isModified)
      filestat["pathids"] = pathIds;

    MimeTypeDetector detector;
    std::string detectedMime = detector.getMimeType(file.absPath);
    magic_t magicCookie = *detector.getMagicCookie();
    if (!detectedMime.empty()) {
      filestat["type"] = detectedMime;
      if (detectedMime.starts_with("image/")) {
        auto maybeDims = detector.getImageDims(file.absPath, magicCookie);
        if (maybeDims.has_value()) {
          filestat["height"] = maybeDims->height;
          filestat["width"] = maybeDims->width;
        } else {
          filestat["height"] = 0;
          filestat["width"] = 0;
        }
      }
    } else {
      filestat["type"] = "file";
    }

    // ── Progress state ────────────────────────────────────────
    struct ProgressData {
      ProgressCallBack callback;
      std::string key;
    };

    ProgressData progressData{onProgress, file.uuid};

    auto progressCallback = [](void *userdata, curl_off_t, curl_off_t,
                               curl_off_t ultotal, curl_off_t ulnow) -> int {
      if (ultotal <= 0)
        return 0;
      auto *pd = static_cast<ProgressData *>(userdata);
      if (pd->callback)
        pd->callback(pd->key, (double)ulnow / (double)ultotal * 100.0);
      return 0;
    };

    auto discardResponseBody = [](void *ptr, size_t size, size_t nmemb,
                                  void *userdata) {
      return size * nmemb; // discard
    };

    // ── Build multipart form ──────────────────────────────────
    std::string url = m_baseUrl + "/app/sync/syncUpFile";
    CURL *handle = m_impl->createHandle(url);

    curl_mime *mime = curl_mime_init(handle);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");

    // read file in chunks
    curl_mime_filedata(part, file.absPath.c_str());
    curl_mime_filename(part, file.filename.c_str());
    curl_mime_type(part, "application/octet-stream");

    // ── Headers ───────────────────────────────────────────────
    struct curl_slist *headers = nullptr;
    std::string filestatHeader = "filestat: " + filestat.dump();
    headers = curl_slist_append(headers, filestatHeader.c_str());

    // ── Set options ───────────────────────────────────────────
    curl_easy_setopt(handle, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, +progressCallback);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progressData);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION,
                     +discardResponseBody); // ← add
                                            //
    // ── Run transfer ──────────────────────────────────────────
    CURLcode curlResult = m_impl->perform(handle);

    long httpCode = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);

    if (curlResult != CURLE_OK) {
      std::cerr << "[API] Curl error: " << curl_easy_strerror(curlResult)
                << "\n";
      return false;
    }

    if (httpCode != 200) {
      std::cerr << "[API] HTTP error: " << httpCode << "\n";
      return false;
    }

    std::cout << "[API] Upload complete: " << file.filename << "\n";
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[API] " << e.what() << "\n";
    return false;
  }
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

  auto res = m_impl->client.Delete("/app/sync/deleteFiles", data.dump(),
                                   "application/json");
  return res && res->status == 200;
}

bool ApiClient::renameFile(const FileQueueEntry &file,
                           const std::vector<DirectoryMetadata> &dirTree) {
  auto op = parsePath(file.old_path.value());
  auto np = parsePath(file.path);
  json innerData;
  innerData["type"] = "fi";
  innerData["dir"] = np.directory;
  innerData["device"] = np.device;
  innerData["filename"] = file.filename;
  innerData["old_filename"] = file.old_filename.value_or("");
  innerData["origin"] = file.origin;
  innerData["old_dir"] = op.directory;
  innerData["old_device"] = op.device;
  innerData["username"] = m_userEmail;
  innerData["id"] = file.dirID;
  innerData["pathIds"] = dirTree;

  json outerData;
  outerData["data"] = innerData;

  auto res = m_impl->client.Post("/app/sync/renameFile", outerData.dump(),
                                 "application/json");
  return res && res->status == 200;
}

bool ApiClient::createFolder(const DirectoryQueueEntry &dir) {
  std::string query = "/app/sync/createFolder?path=" + urlEncode(dir.path) +
                      "&device=" + urlEncode(dir.device) +
                      "&username=" + urlEncode(m_userEmail) +
                      "&uuid=" + urlEncode(dir.uuid) +
                      "&folder=" + urlEncode(dir.folder) +
                      "&created_at=" + urlEncode(dir.created_at);

  auto res = m_impl->client.Post(query.c_str());
  std::cerr << "[API] ERROR: " << res.error()
            << " | value returned: " << (res && res->status) << "\n";
  return res && res->status == 200;
}

bool ApiClient::deleteFolder(const DirectoryQueueEntry &dir) {
  auto parts = parsePath(dir.path);
  std::string query = "/app/sync/deleteFolder?path=" + urlEncode(dir.path) +
                      "&folder=" + urlEncode(dir.folder) +
                      "&directory=" + urlEncode(parts.directory) +
                      "&username=" + urlEncode(m_userEmail) +
                      "&device=" + urlEncode(dir.device);
  auto res = m_impl->client.Delete(query.c_str());
  return res && res->status == 200;
}

bool ApiClient::moveFolder(const DirectoryQueueEntry &dir, bool isRename) {
  json data;
  data["oldPath"] = dir.old_path.value_or("");
  data["newPath"] = dir.path;
  data["username"] = m_userEmail;

  using apiReturnType = decltype(m_impl->client.Post("/"));
  apiReturnType res;

  if (isRename) {
    res = m_impl->client.Post("/app/sync/renameFolder", data.dump(),
                              "application/json");
  } else {
    res = m_impl->client.Post("/app/sync/moveFolder", data.dump(),
                              "application/json");
  }

  return res && (res->status == 200 || res->status == 409);
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
  if (device == "/")
    return {"/", "/"};
  std::string directory = "";

  if (parts.size() > 1) {
    for (size_t i = 1; i < parts.size(); ++i) {
      directory += parts[i] + (i == parts.size() - 1 ? "" : "/");
    }
  }

  return {device, directory == "" ? "/" : directory};
}

} // namespace sync_app
