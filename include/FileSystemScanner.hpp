#ifndef FILESYSTEMSCANNER_HPP
#define FILESYSTEMSCANNER_HPP

#include "types.hpp"
#include <string>

namespace sync {
class FileSystemScanner {
public:
  FileSystemScanner(std::string syncPath);
  ~FileSystemScanner();

  ScanResult scanSyncPath(std::string path);
  std::string getInode(const std::string &absPath);

private:
  std::string m_syncPath;
  std::string calculateHash(const std::string &absPath);
  std::string toRelativePath(const std::string &absPath);
  std::string normalizePathSeparators(const std::string &path);
};

} // namespace sync

#endif // FILESYSTEMSCANNER_HPP
