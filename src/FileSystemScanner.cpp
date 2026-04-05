#include "FileSystemScanner.hpp"
#include "SyncTree.hpp"
#include "ThreadPool.hpp"
#include "types.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <unordered_map>
#include <vector>

// Include picosha2 for hashing
#include <picosha2.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace sync_app {

struct FileTask {
  ScannedFile file;
  std::future<std::string> hashFuture;
};

FileSystemScanner::FileSystemScanner(ThreadPool &threadPool,
                                     std::string syncPath, SyncTree &syncTree)
    : m_syncPath(syncPath), m_threadPool(threadPool), m_syncTree(syncTree) {}

FileSystemScanner::~FileSystemScanner() = default;

std::string
FileSystemScanner::normalizePathSeparators(const std::string &path) {
  std::string result = path;
#ifdef _WIN32
  std::replace(result.begin(), result.end(), '\\', '/');
#endif
  return result;
}

std::string FileSystemScanner::toRelativePath(const std::string &absPath) {
  fs::path base{m_syncPath};
  fs::path full{absPath};
  if (fs::is_directory(full)) {
    auto relativePath = "/" + fs::relative(full, base).generic_string();
    return normalizePathSeparators(relativePath);

  } else {
    auto relativePath =
        "/" + fs::relative(full, base).parent_path().generic_string();
    return normalizePathSeparators(relativePath);
  }
}

std::string FileSystemScanner::getInode(const std::string &absPath) {
#ifdef _WIN32
  HANDLE hFile = CreateFileW(
      std::wstring(absPath.begin(), absPath.end())
          .c_str(), // Basic conversion, assuming ASCII/UTF8 overlap for now
      0,            // No access rights needed for attributes
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

std::string FileSystemScanner::calculateHash(const std::string &absPath) {
  std::ifstream f(absPath, std::ios::binary);
  if (!f.is_open())
    return "";

  // ── OpenSSL EVP context ───────────────────────────────────────
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

  // ── Read and hash in chunks ───────────────────────────────────
  constexpr size_t CHUNK = 65536; // 64KB chunks
  std::vector<char> buffer(CHUNK);

  while (f.read(buffer.data(), CHUNK) || f.gcount() > 0) {
    EVP_DigestUpdate(ctx, buffer.data(), f.gcount());
  }

  // ── Finalize ──────────────────────────────────────────────────
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashLen = 0;
  EVP_DigestFinal_ex(ctx, hash, &hashLen);
  EVP_MD_CTX_free(ctx);

  // ── Convert to hex string ─────────────────────────────────────
  std::ostringstream oss;
  for (unsigned int i = 0; i < hashLen; i++) {
    oss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(hash[i]);
  }
  return oss.str();
  /*  std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(f, hash.begin(), hash.end());
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
  */
}

std::int64_t
FileSystemScanner::getUnixTimeStamp(const fs::file_time_type &ftime) {
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

ScanResult FileSystemScanner::scanSyncPath(std::string path) {
  ScanResult result;
  fs::directory_options opts = fs::directory_options::skip_permission_denied;

  try {
    if (!fs::exists(path))
      return result;

    std::vector<FileTask> fileTasks;
    std::unordered_map<std::string, InodeCacheInfo> inodesCache;

    for (const auto &entry : fs::recursive_directory_iterator(path, opts)) {
      std::cout << "[scanner] processing..." << entry.path() << std::endl;
      try {
        if (entry.is_regular_file()) {
          ScannedFile file;
          file.absPath = entry.path().generic_string();
          file.path = toRelativePath(file.absPath);
          file.filename = entry.path().filename().generic_string();
          file.size = entry.file_size();
          file.mtime = getUnixTimeStamp(fs::last_write_time(file.absPath));
          file.inode = getInode(file.absPath);
          inodesCache[file.absPath] = InodeCacheInfo({file.inode, false});
          std::string path = file.path == "/" ? "/" + file.filename
                                              : file.path + "/" + file.filename;
          m_syncTree.insertPath(file.absPath, file.inode, false);
          std::string filePath{file.absPath};
          auto func = [this, filePath]() {
            return this->calculateHash(filePath);
          };
          auto future = m_threadPool.enqueue(func);
          // file.hash = calculateHash(file.absPath);
          //          result.files.push_back(file);
          fileTasks.push_back({file, std::move(future)});
        } else if (entry.is_directory()) {
          ScannedDirectory dir;
          dir.absPath = entry.path().generic_string();
          dir.path = toRelativePath(dir.absPath);
          dir.name = entry.path().filename().generic_string();
          dir.inode = getInode(dir.absPath);
          inodesCache[dir.absPath] = InodeCacheInfo({dir.inode, true});
          m_syncTree.insertPath(dir.absPath, dir.inode, true);
          dir.mtime = getUnixTimeStamp(fs::last_write_time(dir.absPath));
          result.directories.push_back(dir);
        }
      } catch (const std::exception &e) {
        std::cerr << "Error scanning item: " << entry.path() << " - "
                  << e.what() << std::endl;
      }
    }
    result.files.reserve(fileTasks.size());
    for (auto &task : fileTasks) {
      task.file.hash = task.hashFuture.get();
      result.files.push_back(std::move(task.file));
    }
  } catch (const std::exception &e) {
    std::cerr << "FileSystem Error: " << e.what() << std::endl;
  }
  return result;
}

} // namespace sync_app
