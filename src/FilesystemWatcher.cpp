#include "FilesystemWatcher.hpp"
#include <iostream>

namespace sync {

    class WatcherListener : public efsw::FileWatchListener {
    public:
        WatcherListener(FilesystemWatcher::Callback callback) : m_callback(callback) {}

        void handleFileAction(efsw::WatchID watchid, const std::string& dir,
                               const std::string& filename, efsw::Action action,
                               std::string oldFilename) override {
            WatchEvent e;
            switch (action) {
                case efsw::Actions::Add: e = WatchEvent::Added; break;
                case efsw::Actions::Delete: e = WatchEvent::Deleted; break;
                case efsw::Actions::Modified: e = WatchEvent::Modified; break;
                case efsw::Actions::Moved: 
                    if (!oldFilename.empty()) {
                        std::string fullOldPath = dir + oldFilename;
                        if (dir.back() != '/' && dir.back() != '\\') fullOldPath = dir + "/" + oldFilename;
                        m_callback(fullOldPath, WatchEvent::RenamedOld);
                    }
                    e = WatchEvent::RenamedNew; 
                    break;
                default: return;
            }
            if (m_callback) {
                std::string fullPath = dir + filename;
                if (dir.back() != '/' && dir.back() != '\\') fullPath = dir + "/" + filename;
                m_callback(fullPath, e);
            }
        }

    private:
        FilesystemWatcher::Callback m_callback;
    };

    struct FilesystemWatcher::Impl {
        efsw::FileWatcher watcher;
        std::unique_ptr<WatcherListener> listener;
        efsw::WatchID watchId = 0;
        bool running = false;
    };

    FilesystemWatcher::FilesystemWatcher(const std::string& path, Callback callback)
        : m_path(path), m_callback(callback), m_impl(std::make_unique<Impl>()) {
        m_impl->listener = std::make_unique<WatcherListener>(m_callback);
    }

    FilesystemWatcher::~FilesystemWatcher() {
        stop();
    }

    void FilesystemWatcher::start() {
        if (m_impl->running) return;

        try {
            m_impl->watchId = m_impl->watcher.addWatch(m_path, m_impl->listener.get(), true);
            if (m_impl->watchId < 0) {
                 std::cerr << "[Watcher] Error starting watcher: " << efsw::Errors::Log::getLastErrorLog() << std::endl;
                 return;
            }
            m_impl->watcher.watch();
            m_impl->running = true;
            std::cout << "[Watcher] Started monitoring: " << m_path << " (efsw)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Watcher] Error starting watcher: " << e.what() << std::endl;
        }
    }

    void FilesystemWatcher::stop() {
        if (!m_impl->running) return;
        m_impl->watcher.removeWatch(m_impl->watchId);
        m_impl->running = false;
        std::cout << "[Watcher] Stopped monitoring: " << m_path << std::endl;
    }

} // namespace sync
