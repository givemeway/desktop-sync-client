#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "FilesystemWatcher.hpp"
#include "ReconciliationService.hpp"
#include "FileSystemScanner.hpp"
#include <chrono>

namespace fs = std::filesystem;
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main() {
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
    sync::DatabaseManager dbManager(dbPath);
    if (!dbManager.open()) {
      std::cerr << "[Main] Failed to open database." << std::endl;
      return 1;
    }
    dbManager.initializeSchema();
    sync::ApiClient apiClient(apiBaseUrl, userEmail); // Reverted to original constructor
    sync::ReconciliationService reconciliationService(dbManager, syncFolder);
    sync::FileSystemScanner scanner(syncFolder);

    std::cout << "[Main] Database initialized." << std::endl;
    std::cout << "[Main] API Client initialized." << std::endl;

    // 2. Initial Scan & Local Reconciliation
    std::cout << "[Main] Performing initial filesystem scan..." << std::endl;
    sync::ScanResult scanResult = scanner.scanSyncPath(syncFolder);
    reconciliationService.reconcileLocalState(scanResult.files, scanResult.directories);
    std::cout << "[Main] Initial filesystem scan and local reconciliation complete." << std::endl;


    // 3. Initialize Watcher
    sync::FilesystemWatcher watcher(
        syncFolder, [&reconciliationService](const std::string &path, sync::WatchEvent event) {
          std::string eventStr;
          switch (event) {
          case sync::WatchEvent::Added:
            eventStr = "Added";
            break;
          case sync::WatchEvent::Modified:
            eventStr = "Modified";
            break;
          case sync::WatchEvent::Deleted:
            eventStr = "Deleted";
            break;
          case sync::WatchEvent::RenamedOld:
            eventStr = "RenamedOld";
            break;
          case sync::WatchEvent::RenamedNew:
            eventStr = "RenamedNew";
            break;
          }
          std::cout << "[Watcher] Event: " << eventStr << " on " << path
                    << std::endl;
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

    // Keep main alive for a bit to see watcher events
    for (int i = 0; i < 60; ++i) {
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
