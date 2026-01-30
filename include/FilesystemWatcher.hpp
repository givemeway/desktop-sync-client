#pragma once
#include <efsw/efsw.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace sync_app {

/**
 * FilesystemEvent represents a change in the filesystem.
 */
enum class WatchEvent { Added, Modified, Deleted, Moved, Renamed };

/**
 * FilesystemWatcher monitors a directory for changes.
 */
class FilesystemWatcher {
public:
  using Callback = std::function<void(
      const std::string &path, const std::string &oldPath, WatchEvent event)>;

  FilesystemWatcher(const std::string &path,
                    std::unordered_map<std::string, std::string> &inodesCache,
                    Callback callback);
  ~FilesystemWatcher();

  void start();
  void stop();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::string m_path;
  std::unordered_map<std::string, std::string> m_inodesCache;
  Callback m_callback;
};

} // namespace sync_app
