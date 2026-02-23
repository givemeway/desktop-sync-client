#include "ApiClient.hpp"
#include "CloudSyncWorker.hpp"
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "ReconciliationService.hpp"
#include "SyncTree.hpp"
#include "SyncWorker.hpp"
#include "ThreadPool.hpp"
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>

std::atomic<bool> running{true};
std::mutex cv_m;
std::condition_variable cv;
const std::string dbPath = "sync_client.db";
#ifdef _WIN32
const std::string syncFolder = "C:/Users/Sandeep Kumar/Desktop/sync_folder";
#else
const std::string syncFolder = "/users/sandeep/Desktop/sync-folder";
#endif

const std::string apiBaseUrl = "localhost:3001";
const std::string userEmail = "sand.kumar.gr@gmail.com";

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
  try {
    // 0. Ensure sync folder exists
    if (!fs::exists(syncFolder)) {
      std::cout << "[Main] Creating missing sync folder: " << syncFolder
                << std::endl;
      fs::create_directories(syncFolder);
    }

    // 1. Initialize Components
    sync_app::DatabaseManager dbManager(dbPath, syncFolder);
    if (!dbManager.open()) {
      std::cerr << "[Main] Failed to open database." << std::endl;
      return 1;
    }

    dbManager.initializeSchema();

    sync_app::ThreadPool hashPool(4);
    sync_app::ThreadPool uploadPool(4);
    sync_app::ThreadPool downloadPool(4);
    sync_app::SyncTree syncTree(syncFolder);
    sync_app::ApiClient apiClient(apiBaseUrl, userEmail);
    sync_app::FileSystemScanner scanner(hashPool, syncFolder, syncTree);
    sync_app::ReconciliationService reconciliationService(dbManager, scanner,
                                                          hashPool, syncFolder);
    sync_app::SyncWorker syncworker(dbManager, scanner, hashPool, syncFolder);
    sync_app::CloudSyncWorker cloudSync(
        dbManager, apiClient, reconciliationService, scanner, syncworker,
        uploadPool, downloadPool, syncFolder, userEmail);

    std::cout << "[Main] Database initialized." << std::endl;
    std::cout << "[Main] API Client initialized." << std::endl;

    // 2. Initial Scan & Local Reconciliation
    std::cout << "[Main] Performing initial filesystem scan..." << std::endl;
    sync_app::ScanResult scanResult = scanner.scanSyncPath(syncFolder);
    reconciliationService.reconcileLocalState(scanResult.files,
                                              scanResult.directories);
    std::cout
        << "[Main] Initial filesystem scan and local reconciliation complete."
        << std::endl;
    //    syncTree.print();
    // 3. Initialize SyncWorker Background Thread
    syncworker.start();

    // 4. Initialize Watcher
    sync_app::FilesystemWatcher watcher(
        syncFolder, scanResult.inodesCache, syncTree,
        [&syncworker](const std::string &path, const std::string &oldPath,
                      sync_app::WatchEvent event) {
          syncworker.enqueueEvent(event, path, oldPath);
        });
    watcher.start();

    // 4. Start Cloud Sync Worker
    cloudSync.start();

    std::cout << "[Main] Running. Monitoring: " << syncFolder << std::endl;
    std::cout << "[Main] Modify some files in the sync folder to see events."
              << std::endl;

    try {
      std::unique_lock<std::mutex> lock(cv_m);
      cv.wait(lock, [] { return !running.load(); });
      // Keep main alive to continue to track the sync folder
    } catch (...) {
      std::cout << "[Main] Exception in main thread" << std::endl;
    }
    std::cout << "[Main] Shutting down..." << std::endl;
    syncworker.stop();
    cloudSync.stop();
    watcher.stop();
    dbManager.close();
    std::cout << "[Main] Finished." << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "[Main] Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
