#include "ActivityStore.hpp"
#include "ApiClient.hpp"
#include "CloudBackupManager.hpp"
#include "CloudSyncWorker.hpp"
#include "DatabaseManager.hpp"
#include "FileSystemScanner.hpp"
#include "FilesystemWatcher.hpp"
#include "ReconciliationService.hpp"
#include "SyncController.hpp"
#include "SyncTree.hpp"
#include "SyncWorker.hpp"
#include "ThreadPool.hpp"
#include <QGuiApplication>
#include <QString>
#include <QUrl>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

#include <atomic>
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

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);
  QQmlApplicationEngine engine;

  // register shutdown signals
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  std::cout << "[Main] Application starting..." << std::endl;

  try {
    // 0. Ensure sync folder exists
    if (!fs::exists(syncFolder)) {
      std::cout << "[Main] Creating missing sync folder: " << syncFolder
                << std::endl;
      fs::create_directories(syncFolder);
    }

    // 1. Initialize Components
    sync_app::DatabaseManager dbManager(dbPath, syncFolder);
    sync_app::ThreadPool hashPool(8);
    sync_app::ThreadPool scanHashPool(8);
    sync_app::ThreadPool uploadPool(4);
    sync_app::ThreadPool downloadPool(4);
    sync_app::SyncTree syncTree(syncFolder);
    sync_app::ApiClient apiClient(apiBaseUrl, userEmail);
    sync_app::FileSystemScanner scanner(scanHashPool, syncFolder, syncTree);
    sync_app::ReconciliationService reconciliationService(dbManager, scanner,
                                                          hashPool, syncFolder);
    sync_app::ActivityStore activityStore;

    sync_app::SyncWorker syncworker(dbManager, scanner, activityStore, hashPool,
                                    syncFolder);

    sync_app::CloudBackupManager cloudBackup(apiClient);
    sync_app::CloudSyncWorker cloudSync(
        dbManager, apiClient, reconciliationService, scanner, syncworker,
        activityStore, uploadPool, downloadPool, syncFolder, userEmail);

    sync_app::SyncController::initialize(&cloudSync, &syncworker, &cloudBackup);

    engine.rootContext()->setContextProperty(
        "syncController", sync_app::SyncController::instance());

    engine.addImportPath("qrc:/");
    const QUrl url(QStringLiteral("qrc:/main.qml"));

    engine.load(url);

    if (engine.rootObjects().isEmpty())
      return -1;

    // 2. Start Sync Engine in Background Thread
    std::thread engineThread([&]() {
      try {
        std::cout << "[Main] Database initializing in background..."
                  << std::endl;
        if (!dbManager.open()) {
          std::cerr << "[Main] Failed to open database." << std::endl;
          return;
        }
        dbManager.initializeSchema();

        std::cout
            << "[Main] Performing initial filesystem scan in background..."
            << std::endl;
        sync_app::ScanResult scanResult = scanner.scanSyncPath(syncFolder);

        bool isLocalDBSynced = reconciliationService.reconcileLocalState(
            scanResult.files, scanResult.directories);
        if (!isLocalDBSynced) {
          std::cout << "[Main] Local DB Reconciliation Failed. Sync cannot be "
                       "initialized!"
                    << std::endl;
          std::unique_lock<std::mutex> lock(cv_m);
          cv.wait(lock, [&]() { return !running.load(); });
        }
        std::cout << "[Main] Initial scan and reconciliation complete."
                  << std::endl;

        syncworker.start();

        // The watcher needs to stay alive
        sync_app::FilesystemWatcher watcher(
            syncFolder, syncTree,
            [&syncworker](const std::string &path, const std::string &oldPath,
                          sync_app::WatchEvent event) {
              syncworker.enqueueEvent(event, path, oldPath);
            });
        watcher.start();
        cloudSync.start();

        std::cout << "[Main] Sync engine active and monitoring: " << syncFolder
                  << std::endl;

        // Keep thread alive until application shutdown
        std::unique_lock<std::mutex> lock(cv_m);
        cv.wait(lock, [&]() { return !running.load(); });

        std::cout << "[Main] Shutting down sync workers..." << std::endl;
        syncworker.stop();
        cloudSync.stop();
        watcher.stop();
        dbManager.close();
        std::cout << "[Main] Sync engine shut down." << std::endl;

      } catch (const std::exception &e) {
        std::cerr << "[Main] Engine Thread Error: " << e.what() << std::endl;
      }
    });

    int result = app.exec();

    // Signal thread to stop
    running.store(false);
    if (engineThread.joinable()) {
      engineThread.join();
    }

    return result;

  } catch (const std::exception &e) {
    std::cerr << "[Main] Critical Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
