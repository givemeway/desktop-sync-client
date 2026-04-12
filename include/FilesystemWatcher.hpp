#pragma once
#include <efsw/efsw.hpp>
#include <functional>
#include <memory>
#include <string>

namespace sync_app {

/**
 * FilesystemEvent represents a change in the filesystem.
 */

enum class WatchEvent { Added, Modified, Deleted, Moved, Renamed };
class SyncTree;
/**
 * FilesystemWatcher monitors a directory for changes.
 */
class FilesystemWatcher {
public:
  using Callback = std::function<void(
      const std::string &path, const std::string &oldPath, WatchEvent event)>;

  FilesystemWatcher(const std::string &path, SyncTree &syncTree,
                    Callback callback);
  ~FilesystemWatcher();

  void start();
  void stop();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::string m_path;
};

} // namespace sync_app
