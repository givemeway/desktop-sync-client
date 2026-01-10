#ifndef FILESYSTEMSCANNER_HPP
#define FILESYSTEMSCANNER_HPP

#include "types.hpp"
#include <filesystem>
#include <string>
namespace sync {
class FileSystemScanner {
public:
  FileSystemScanner(std::string syncPath);
  ~FileSystemScanner();

  ScanResult scanSyncPath(std::string path);
  std::string getInode(const std::string &absPath);
  std::string toRelativePath(const std::string &absPath);
  std::int64_t getUnixTimeStamp(const std::filesystem::file_time_type &ftime);
  std::string normalizePathSeparators(const std::string &path);

private:
  std::string m_syncPath;
  std::string calculateHash(const std::string &absPath);
};

} // namespace sync

#endif // FILESYSTEMSCANNER_HPP
