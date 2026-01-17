#pragma once
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include <string>
#ifndef SYNC_WORKER_HPP
#define SYNC_WORKER_HPP

namespace sync_app {

class SyncWorker {
public:
  SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
             const std::string &syncPath);
  ~SyncWorker();
  void handleAdded(const std::string &path);
  void handleDeleted(const std::string &path);
  void handleRenamed(const std::string &path, const std::string &oldPath);
  void handleModified(const std::string &path);
  void start();
  void stop();
  void enqueueEvent(WatchEvent event, const std::string &path,
                    const std::string &oldPath);
  void addIgnoreEvent(const std::string &path, WatchEvent event);
  void workerLoop();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sync_app
#endif
