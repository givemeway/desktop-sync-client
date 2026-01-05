#include "FileSystemScanner.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

// Include picosha2 for hashing
#include <picosha2.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace sync {

FileSystemScanner::FileSystemScanner(std::string syncPath)
    : m_syncPath(syncPath) {}

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

  std::vector<unsigned char> hash(picosha2::k_digest_size);
  picosha2::hash256(f, hash.begin(), hash.end());
  return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
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

    for (const auto &entry : fs::recursive_directory_iterator(path, opts)) {
      try {
        if (entry.is_regular_file()) {
          ScannedFile file;
          file.absPath = entry.path().string();
          file.path = toRelativePath(file.absPath);
          file.filename = entry.path().filename().string();
          file.size = entry.file_size();
          file.mtime = getUnixTimeStamp(fs::last_write_time(file.absPath));
          file.inode = getInode(file.absPath);
          file.hash = calculateHash(file.absPath);
          result.files.push_back(file);

        } else if (entry.is_directory()) {
          ScannedDirectory dir;
          dir.absPath = entry.path().string();
          dir.path = toRelativePath(dir.absPath);
          dir.name = entry.path().filename().string();
          dir.inode = getInode(dir.absPath);
          dir.mtime = getUnixTimeStamp(fs::last_write_time(dir.absPath));
          result.directories.push_back(dir);
        }
      } catch (const std::exception &e) {
        std::cerr << "Error scanning item: " << entry.path() << " - "
                  << e.what() << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "FileSystem Error: " << e.what() << std::endl;
  }

  return result;
}

} // namespace sync
