#pragma once
#include <efsw/efsw.hpp>
#include <functional>
#include <memory>
#include <string>

namespace sync {

/**
 * FilesystemEvent represents a change in the filesystem.
 */
enum class WatchEvent { Added, Modified, Deleted, RenamedOld, RenamedNew };

/**
 * FilesystemWatcher monitors a directory for changes.
 */
class FilesystemWatcher {
public:
  using Callback =
      std::function<void(const std::string &path, WatchEvent event)>;

  FilesystemWatcher(const std::string &path, Callback callback);
  ~FilesystemWatcher();

  void start();
  void stop();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::string m_path;
  Callback m_callback;
};

} // namespace sync
