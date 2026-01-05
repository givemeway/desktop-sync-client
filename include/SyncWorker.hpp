#pragma once
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include <string>
#ifndef SYNC_WORKER_HPP
#define SYNC_WORKER_HPP

namespace sync {

class SyncWorker {
public:
  SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
             const std::string &syncPath);
  ~SyncWorker();
  void handleAdded(const std::string &path);
  void handleDeleted(const std::string &path);
  void handleRenamed(const std::string &path);
  void handleModified(const std::string &path);

private:
  DatabaseManager &m_dbManager;
  FileSystemScanner &m_scanner;
  std::string m_syncPath;
};

} // namespace sync
#endif
