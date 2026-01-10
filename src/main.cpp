#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "ReconciliationService.hpp"
#include "SyncWorker.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

std::atomic<bool> running{true};
std::mutex cv_m;
std::condition_variable cv;

static void signalHandler(int sig) {
  std::cout << "[Main] Shutdown signal received (" << sig << ")" << std::endl;
  running.store(false);
  cv.notify_all();
}

namespace fs = std::filesystem;
int main() {
  // register shutdown signals

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
  std::cout << "[Main] Running. Press Ctrl+C to exit gracefully." << std::endl;
  std::cout << "Sync Client starting..." << std::endl;

  const std::string dbPath = "sync_client.db";
  const std::string syncFolder = "C:/Users/Sandeep Kumar/Desktop/sync_folder";
  const std::string apiBaseUrl = "http://localhost:3000";
  const std::string userEmail = "sand.kumar.gr@gmail.com";

  try {
    // 0. Ensure sync folder exists
    if (!fs::exists(syncFolder)) {
      std::cout << "[Main] Creating missing sync folder: " << syncFolder
                << std::endl;
      fs::create_directories(syncFolder);
    }

    // 1. Initialize Components
    sync::DatabaseManager dbManager(dbPath, syncFolder);
    if (!dbManager.open()) {
      std::cerr << "[Main] Failed to open database." << std::endl;
      return 1;
    }
    dbManager.initializeSchema();
    sync::ApiClient apiClient(apiBaseUrl, userEmail);
    sync::ReconciliationService reconciliationService(dbManager, syncFolder);
    sync::FileSystemScanner scanner(syncFolder);
    sync::SyncWorker syncworker(dbManager, scanner, syncFolder);
    std::cout << "[Main] Database initialized." << std::endl;
    std::cout << "[Main] API Client initialized." << std::endl;

    // 2. Initial Scan & Local Reconciliation
    std::cout << "[Main] Performing initial filesystem scan..." << std::endl;
    sync::ScanResult scanResult = scanner.scanSyncPath(syncFolder);
    reconciliationService.reconcileLocalState(scanResult.files,
                                              scanResult.directories);
    std::cout
        << "[Main] Initial filesystem scan and local reconciliation complete."
        << std::endl;

    // 3. Initialize Watcher
    sync::FilesystemWatcher watcher(
        syncFolder, [&reconciliationService, &syncworker](
                        const std::string &path, const std::string &oldPath,
                        sync::WatchEvent event) {
          std::string eventStr;
          switch (event) {
          case sync::WatchEvent::Added:
            eventStr = "Added";
            std::cout << "[Watcher] Event: " << eventStr << " on " << path
                      << std::endl;
            syncworker.handleAdded(path);
            break;
          case sync::WatchEvent::Modified:
            eventStr = "Modified";
            std::cout << "[Watcher] Event: " << eventStr << " on " << path
                      << std::endl;
            syncworker.handleModified(path);
            break;
          case sync::WatchEvent::Deleted:
            eventStr = "Deleted";
            std::cout << "[Watcher] Event: " << eventStr << " on " << path
                      << std::endl;
            syncworker.handleDeleted(path);
            break;
          case sync::WatchEvent::Moved:
            eventStr = "Moved";
            std::cout << "[Watcher] Event: " << eventStr << " on " << path
                      << std::endl;
            syncworker.handleRenamed(path, oldPath);
            break;
          }
        });
    watcher.start();

    // 5. Test API GetMetadata
    std::cout << "[Main] Fetching cloud metadata..." << std::endl;
    auto result = apiClient.getMetadata();
    if (result && result->success) {
      std::cout << "[Main] Found " << result->files.size() << " files and "
                << result->directories.size() << " directories in cloud."
                << std::endl;
    }

    std::cout << "[Main] Running. Monitoring: " << syncFolder << std::endl;
    std::cout << "[Main] Modify some files in the sync folder to see events."
              << std::endl;

    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait(lock, [] { return !running.load(); });
    // Keep main alive to continue to track the sync folder
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    watcher.stop();
    dbManager.close();
    std::cout << "[Main] Finished." << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "[Main] Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
