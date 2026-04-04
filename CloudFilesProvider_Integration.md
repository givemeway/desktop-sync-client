# CloudFilesProvider — Integration Guide

## Files to add to your project
- `CloudFilesProvider.hpp`
- `CloudFilesProvider.cpp`

## CMakeLists.txt changes

```cmake
# Add the new source file
target_sources(your_target PRIVATE
    CloudFilesProvider.cpp
)

# Link the CF API library (Windows only)
if(WIN32)
    target_link_libraries(your_target PRIVATE cldapi)
endif()
```

---

## main.cpp changes

Construct `CloudFilesProvider` alongside your other components and pass it
to `CloudSyncWorker`. Registration and start happen before the sync engine
starts polling.

```cpp
// After constructing apiClient, dbManager, syncworker, activityStore:

sync_app::CloudFilesProvider cfProvider(
    apiClient,
    dbManager,
    syncworker,
    activityStore,
    syncFolder,
    userEmail,
    "IDriveSync"          // your provider display name in Explorer
);

// Register once (idempotent — safe to call every launch)
cfProvider.registerSyncRoot();

// Connect CF callbacks before starting the sync engine
cfProvider.start();

// Then start your existing workers as before
syncworker.start();
cloudSync.start();

// On shutdown (before dbManager.close()):
cfProvider.stop();
```

---

## CloudSyncWorker.hpp changes

Add a pointer to `CloudFilesProvider`:

```cpp
// Forward declare at top of header (inside #ifdef _WIN32)
#ifdef _WIN32
class CloudFilesProvider;
#endif

// Add to constructor parameters:
explicit CloudSyncWorker(
    DatabaseManager     &dbManager,
    ApiClient           &apiClient,
    ReconciliationService &reconcile,
    FileSystemScanner   &scanner,
    SyncWorker          &syncWorker,
    ActivityStore       &activityStore,
    ThreadPool          &uploadThreadPool,
    ThreadPool          &downloadThreadPool,
    const std::string   &syncPath,
    const std::string   &userEmail,
#ifdef _WIN32
    CloudFilesProvider  *cfProvider = nullptr,   // optional — null = legacy mode
#endif
    QObject             *parent = nullptr
);

// Add private member:
private:
#ifdef _WIN32
    CloudFilesProvider *m_cfProvider = nullptr;
#endif
```

---

## CloudSyncWorker.cpp changes

### Constructor — store the provider

```cpp
CloudSyncWorker::CloudSyncWorker(
    ...
#ifdef _WIN32
    CloudFilesProvider *cfProvider,
#endif
    QObject *parent)
    : ... // existing initialisers
#ifdef _WIN32
    , m_cfProvider(cfProvider)
#endif
{}
```

---

### processFilesToDownload — replace download with placeholder creation

**Before (existing code):**
```cpp
void CloudSyncWorker::processFilesToDownload(
    const std::vector<CloudFileMetadata> &filesToDownload)
{
    // ... existing download logic using m_downloadThreadPool ...
}
```

**After:**
```cpp
void CloudSyncWorker::processFilesToDownload(
    const std::vector<CloudFileMetadata> &filesToDownload)
{
#ifdef _WIN32
    if (m_cfProvider) {
        // CF API mode — create placeholders instead of downloading.
        // The file appears in Explorer immediately with correct size/date.
        // Actual bytes are fetched on-demand by CloudFilesProvider::onFetchData.
        for (const auto &file : filesToDownload) {
            auto syncItem = m_activityStore.getActivity(file.uuid);
            if (syncItem.has_value()) {
                syncItem->isActive = true;
                syncItem->inQueue  = false;
                updateActivity(file.uuid, syncItem.value());
            }

            bool ok = m_cfProvider->createFilePlaceholder(file);

            if (ok) {
                // Insert into DB so the file is tracked (same as after a real download)
                std::string absPath = file.path == "/"
                    ? m_syncPath + "/" + file.filename
                    : m_syncPath + file.path + "/" + file.filename;

                FileMetadata f = getFileMetadata(file, absPath);

                std::vector<DirectoryMetadata> dirs;
                auto paths = getPathComponents(file.path);
                for (auto &p : paths) {
                    std::string uuid = "";
                    if (file.dirIDs && file.dirIDs->count(p))
                        uuid = file.dirIDs->at(p);
                    else if (p == file.path)
                        uuid = file.dirID;
                    if (!uuid.empty())
                        dirs.push_back(getDirectoryMetadata(p, uuid));
                }

                {
                    std::lock_guard<std::recursive_mutex> lock(
                        m_dbManager.getSyncMutex());
                    m_dbManager.insertFileWithDirectory(f, dirs);
                }

                if (syncItem.has_value()) {
                    syncItem->isActive = false;
                    syncItem->isDone   = true;
                    updateActivity(file.uuid, syncItem.value());
                }
            } else {
                if (syncItem.has_value()) {
                    syncItem->isActive = false;
                    syncItem->isError  = true;
                    updateActivity(file.uuid, syncItem.value());
                }
            }
        }
        return;  // ← skip the legacy download path below
    }
#endif

    // ── Legacy download path (non-Windows or CF API not enabled) ──────────
    // ... your existing processFilesToDownload implementation unchanged ...
}
```

---

### processFoldersToCreate — replace fs::create_directories with placeholder

**After (wrap existing logic):**
```cpp
void CloudSyncWorker::processFoldersToCreate(
    const std::vector<LocalFolderCreateMetadata> &foldersToCreateLocal)
{
    for (auto &folder : foldersToCreateLocal) {
        auto syncItem = m_activityStore.getActivity(folder.uuid);
        if (syncItem.has_value()) {
            syncItem->isActive = true;
            updateActivity(folder.uuid, syncItem.value());
        }

#ifdef _WIN32
        if (m_cfProvider) {
            bool ok = m_cfProvider->createDirPlaceholder(folder);
            if (!ok) {
                if (syncItem.has_value()) {
                    syncItem->isActive = false;
                    syncItem->isError  = true;
                    updateActivity(folder.uuid, syncItem.value());
                }
                continue;
            }
            // Register in DB
            auto folderPaths = getPathComponents(folder.path);
            std::vector<DirectoryMetadata> dirs;
            for (auto &path : folderPaths) {
                std::string uuid = "";
                if (folder.dirIDs && folder.dirIDs->count(path))
                    uuid = folder.dirIDs->at(path);
                else if (path == folder.path)
                    uuid = folder.uuid;
                if (!uuid.empty())
                    dirs.push_back(getDirectoryMetadata(path, uuid));
            }
            {
                std::lock_guard<std::recursive_mutex> lock(
                    m_dbManager.getSyncMutex());
                m_dbManager.createDirectoryPaths(dirs);
            }
            if (syncItem.has_value()) {
                syncItem->isActive = false;
                syncItem->isDone   = true;
                updateActivity(folder.uuid, syncItem.value());
            }
            continue;
        }
#endif
        // ... existing fs::create_directories logic unchanged below ...
    }
}
```

---

### processFilesToUpdate — dehydrate then re-create placeholder

After a successful update from cloud, dehydrate the old local copy so
Explorer re-fetches the new version on next open:

```cpp
// After m_dbManager.updateFileWithTransaction() succeeds:
#ifdef _WIN32
if (m_cfProvider) {
    std::wstring absPathW = CloudFilesProvider::toWide(absPath);
    m_cfProvider->markNotInSync(absPathW);   // show sync arrows while dehydrating
    m_cfProvider->dehydrateFile(absPathW);   // remove bytes, keep ghost
    m_cfProvider->markInSync(absPathW);      // back to green checkmark
}
#endif
```

---

### uploadFile / uploadModifiedFile — mark in-sync after successful upload

After `m_dbManager.deleteFileQueue()` succeeds:

```cpp
#ifdef _WIN32
if (m_cfProvider) {
    std::wstring absPathW = CloudFilesProvider::toWide(fq.absPath);
    m_cfProvider->markInSync(absPathW);
}
#endif
```

---

## How the full flow works end-to-end

```
Cloud has new file
        │
        ▼
ReconciliationService.reconcile()
  → filesToDownload = [CloudFileMetadata]
        │
        ▼
CloudSyncWorker::processFilesToDownload()
  → cfProvider->createFilePlaceholder(file)
  → Ghost appears in Explorer (0 bytes on disk, correct size shown)
  → File inserted into DB with lastSynced timestamp
        │
        │  (user double-clicks file in Explorer)
        ▼
Windows CF kernel calls onFetchData()
  → ApiClient::downloadFile() downloads to temp file
  → CfExecute(TransferData) feeds bytes to CF kernel
  → CF kernel writes bytes to the real file
  → markInSync() → green checkmark overlay appears
  → Temp file deleted
  → ActivityStore updated (progress → 100%)
        │
        │  (user edits the file)
        ▼
FilesystemWatcher detects Modified event
  → SyncWorker::handleModified()
  → FileQueueEntry with SyncStatus::MODIFIED pushed to priority queue
  → cfProvider->markNotInSync() → sync arrows overlay
        │
        ▼
CloudSyncWorker::uploadModifiedFile()
  → ApiClient::uploadFile()
  → cfProvider->markInSync() → green checkmark
  → cfProvider->dehydrateFile() (optional — frees disk space)
```

---

## Notes on thread safety

- `CfCreatePlaceholders` and `CfExecute` are thread-safe per Microsoft docs.
- `CfConnectSyncRoot` callbacks fire on a CF-managed thread pool — `onFetchData`
  runs on a different thread from your sync workers. The mutex guards in
  `DatabaseManager::getSyncMutex()` protect DB access correctly.
- The `PlaceholderIdentity` structs in `onFetchPlaceholders` are stack-allocated
  per-call and outlive `CfExecute` within the same callback scope.
- The `static PlaceholderIdentity` in `buildFilePlaceholderInfo` is NOT
  thread-safe if you call `createFilePlaceholder` concurrently — use the
  per-call local identity copy in the public method instead (which is what
  the implementation does).
