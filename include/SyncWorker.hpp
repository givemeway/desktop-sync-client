#pragma once
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "types.hpp"
#include <string>
#ifndef SYNC_WORKER_HPP
#define SYNC_WORKER_HPP

namespace sync_app {

class ThreadPool;

class SyncWorker {
public:
  SyncWorker(DatabaseManager &dbManager, FileSystemScanner &scanner,
             ThreadPool &threadPool, const std::string &syncPath);
  ~SyncWorker();
  void handleAdded(const std::string &path);
  void handleDeleted(const std::string &path);
  void handleRenamed(const std::string &path, const std::string &oldPath,
                     bool isMoved = false);
  void handleModified(const std::string &path);
  void start();
  void stop();
  void enqueueEvent(WatchEvent event, const std::string &path,
                    const std::string &oldPath);
  void addIgnoreEvent(const std::string &path, WatchEvent event);
  void removeIgnoreEvent(const std::string &path, WatchEvent event);
  void removeEventMap(const std::string &inode, const std::string &path = "");
  void workerLoop();
  FileMetadata constructFileMetadata(const ScannedFile &scannedFile,
                                     const FileQueueEntry &f,
                                     const std::string &dirID = "");
  DirectoryMetadata
  constructDirectoryMetadata(const ScannedDirectory &scannedDir,
                             const DirectoryQueueEntry &d);

  bool isHighPriorityLocalTaskQueued() const;
  void popFileEntry();
  void pushFileEntry(const FileQueueEntry &fq);
  void pushDirEntry(const DirectoryQueueEntry &dq);
  void popDirEntry();
  bool fileIsEmpty();
  bool dirIsEmpty();
  FileQueueEntry topFileEntry() const;
  DirectoryQueueEntry topDirEntry() const;

private:
  std::function<void()> m_uploadCallback;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sync_app
#endif
