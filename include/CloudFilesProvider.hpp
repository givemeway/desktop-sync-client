#pragma once

// Windows Cloud Files API (CF API) — requires Windows 10 1709+
// Link: cldapi.lib
// CF API is Windows-only — the entire class is conditionally compiled.

#ifdef _WIN32

#include "types.hpp"
#include <atomic>
#include <cfapi.h>
#include <string>
#include <thread>
#include <windows.h>

namespace sync_app {

class ApiClient;
class DatabaseManager;
class SyncWorker;
class ActivityStore;

// ─────────────────────────────────────────────────────────────────────────────
// CloudFilesProvider
//
// Wraps the Windows Cloud Files API so your sync engine can offer
// "Files On-Demand" — files appear in Explorer as ghosts (placeholders)
// and are downloaded transparently only when a user or application opens them.
//
// Lifecycle:
//   1. registerSyncRoot()  — once, at install time (or first run)
//   2. start()             — connects callbacks, begins serving hydration
//   requests
//   3. stop()              — disconnects callbacks, frees CF connection
//
// Integration with CloudSyncWorker:
//   - Call createFilePlaceholder()  instead of downloading in
//   processFilesToDownload()
//   - Call createDirPlaceholder()   instead of fs::create_directories() in
//   processFoldersToCreate()
//   - Call dehydrateFile()          after a file is confirmed synced to cloud
//   - Call markInSync() / markNotInSync() to keep overlay icons correct
//
// The FETCH_DATA callback calls ApiClient::downloadFile() — the same function
// your existing download path uses, so progress reporting via ActivityStore
// works identically.
// ─────────────────────────────────────────────────────────────────────────────

class CloudFilesProvider {
public:
  CloudFilesProvider(ApiClient &apiClient, DatabaseManager &dbManager,
                     SyncWorker &syncWorker, ActivityStore &activityStore,
                     const std::string &syncPath, const std::string &userEmail,
                     const std::string &providerName = "IDriveSync");

  ~CloudFilesProvider();

  // ── Registration (call once on install / first run) ───────────────────
  // Writes the sync root registration into the Windows registry so Explorer
  // knows this folder is managed by a cloud provider.
  bool registerSyncRoot();

  // Removes the sync root registration (call on uninstall).
  bool unregisterSyncRoot();

  // ── Runtime connection ────────────────────────────────────────────────
  // Connects CF callbacks and starts serving hydration requests.
  bool start();

  // Disconnects callbacks. Safe to call from any thread.
  void stop();

  // ── Placeholder management ────────────────────────────────────────────

  // Create a ghost file placeholder from cloud metadata.
  // Call this instead of downloading in processFilesToDownload().
  // The file will appear in Explorer with size/dates but no local bytes.
  bool createFilePlaceholder(const CloudFileMetadata &file);

  // Create a ghost directory placeholder.
  // Call this instead of fs::create_directories() in processFoldersToCreate().
  bool createDirPlaceholder(const LocalFolderCreateMetadata &dir);

  // Mark a file/directory as fully in-sync with the cloud.
  // This shows the green checkmark overlay icon in Explorer.
  bool markInSync(const std::wstring &absPath);

  // Mark a file/directory as out-of-sync (pending upload/download).
  bool markNotInSync(const std::wstring &absPath);

  // Dehydrate a file — remove local bytes, keep the placeholder ghost.
  // Call this after a file has been successfully uploaded and confirmed
  // in the cloud, if you want to free local disk space.
  bool dehydrateFile(const std::wstring &absPath);

  // Hydrate a file synchronously (blocking) — useful for conflict handling.
  bool hydrateFile(const std::wstring &absPath);

  // ── Helpers ───────────────────────────────────────────────────────────
  bool isSyncRootRegistered() const;

  // Convert between std::string (UTF-8) and std::wstring (UTF-16) paths.
  static std::wstring toWide(const std::string &s);
  static std::string toNarrow(const std::wstring &w);

private:
  // ── CF callback implementations ───────────────────────────────────────
  // These are static because CF API requires plain function pointers.
  // The CF_CALLBACK_INFO::CallbackContext field carries `this`.

  static void CALLBACK onFetchData(const CF_CALLBACK_INFO *info,
                                   const CF_CALLBACK_PARAMETERS *params);

  static void CALLBACK onCancelFetchData(const CF_CALLBACK_INFO *info,
                                         const CF_CALLBACK_PARAMETERS *params);

  static void CALLBACK onFetchPlaceholders(
      const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params);

  static void CALLBACK onNotifyFileOpened(const CF_CALLBACK_INFO *info,
                                          const CF_CALLBACK_PARAMETERS *params);

  static void CALLBACK onNotifyFileClosed(const CF_CALLBACK_INFO *info,
                                          const CF_CALLBACK_PARAMETERS *params);

  // ── Internal helpers ──────────────────────────────────────────────────

  // Transfer a byte range to the CF kernel as part of FETCH_DATA.
  // Called from onFetchData after downloading the required range.
  bool transferData(CF_CONNECTION_KEY connKey, CF_TRANSFER_KEY transferKey,
                    const void *buffer, LARGE_INTEGER offset,
                    LARGE_INTEGER length, HRESULT result = S_OK);

  // Build a CF_PLACEHOLDER_CREATE_INFO from a CloudFileMetadata.
  CF_PLACEHOLDER_CREATE_INFO
  buildFilePlaceholderInfo(const CloudFileMetadata &file) const;

  // Build a CF_PLACEHOLDER_CREATE_INFO for a directory.
  CF_PLACEHOLDER_CREATE_INFO
  buildDirPlaceholderInfo(const std::string &relPath, const std::string &uuid,
                          const std::string &created_at) const;

  // Convert a Unix timestamp string (seconds) to a FILETIME.
  static FILETIME unixStringToFileTime(const std::string &unixSeconds);

  // Convert a Unix epoch (seconds) to LARGE_INTEGER FILETIME ticks.
  static LARGE_INTEGER unixToLargeIntFileTime(int64_t unixSeconds);

  // ── Members ───────────────────────────────────────────────────────────
  ApiClient &m_apiClient;
  DatabaseManager &m_dbManager;
  SyncWorker &m_syncWorker;
  ActivityStore &m_activityStore;

  std::string m_syncPath;   // absolute path to sync root, UTF-8
  std::wstring m_syncPathW; // same, UTF-16 for Win32 calls
  std::string m_userEmail;
  std::string m_providerName;

  CF_CONNECTION_KEY m_connectionKey{};
  std::atomic<bool> m_connected{false};

  // Stable GUID for this sync provider — must be the same across sessions.
  // Generated from provider name; stored so registerSyncRoot() is idempotent.
  GUID m_providerGuid{};

  static GUID makeGuidFromName(const std::string &name);
};

} // namespace sync_app

#endif // _WIN32
