#ifndef FILESYSTEMSCANNER_HPP
#define FILESYSTEMSCANNER_HPP

#include "types.hpp"
#include <filesystem>
#include <string>
namespace sync_app {
struct PlaceholderMeta {
  int64_t size = 0;
  int64_t mtime = 0; // Unix seconds
  bool valid = false;
};
class ThreadPool;
class SyncTree;
class FileSystemScanner {
public:
  FileSystemScanner(ThreadPool &threadPool, std::string syncPath,
                    SyncTree &syncTree);
  ~FileSystemScanner();

  ScanResult scanSyncPath(std::string path);
  std::string getInode(const std::string &absPath);
  std::string toRelativePath(const std::string &absPath);
  std::int64_t getUnixTimeStamp(const std::filesystem::file_time_type &ftime);
  std::string normalizePathSeparators(const std::string &path);
  std::string calculateHash(const std::string &absPath);
  bool isCloudPlaceholder(const std::string &absPath);
  PlaceholderMeta getPlaceholderMeta(const std::string &absPath);

private:
  std::string m_syncPath;
  ThreadPool &m_threadPool;
  SyncTree &m_syncTree;
};

} // namespace sync_app

#endif // FILESYSTEMSCANNER_HPP
