// CloudFilesProvider.hpp includes windows.h + cfapi.h first,
// then undefs conflicting Windows macros (DELETE, ERROR, UNKNOWN etc.)
// before pulling in types.hpp. All other project headers come after
// so they see the clean namespace, not the polluted one.
#include "CloudFilesProvider.hpp"
#include "Utility.hpp"
#include "types.hpp"
#include <cfapi.h>
#include <combaseapi.h>
#include <cstddef>
#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <memory>
#include <minwindef.h>
#include <string.h>
#include <string>
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>
#include <winreg.h>
#include <winrt/base.h>

#ifdef _WIN32
#include "ActivityStore.hpp"
#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "FilesystemWatcher.hpp"
#include "SyncWorker.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sddl.h>
#include <shlobj.h>
#include <vector>
#include <winrt/Windows.Foundation.h> // <--- ADD THIS
#include <winrt/Windows.Storage.Provider.h>
#include <winrt/Windows.Storage.h> // Required for StorageFolder operations
                                   //
#pragma comment(lib, "cldapi.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace sync_app {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

CloudFilesProvider::CloudFilesProvider(
    ApiClient &apiClient, DatabaseManager &dbManager, SyncWorker &syncWorker,
    ActivityStore &activityStore, const std::string &syncPath,
    const std::string &userEmail, const std::string &providerName)
    : m_apiClient(apiClient), m_dbManager(dbManager), m_syncWorker(syncWorker),
      m_activityStore(activityStore), m_syncPath(syncPath),
      m_syncPathW(toWide(syncPath)), m_userEmail(userEmail),
      m_providerName(providerName),
      m_providerGuid(makeGuidFromName(providerName)) {}

CloudFilesProvider::~CloudFilesProvider() { stop(); }

std::unique_ptr<CloudFilesProvider> CloudFilesProvider::clone() const {
  return std::make_unique<CloudFilesProvider>(
      m_apiClient, m_dbManager, m_syncWorker, m_activityStore, m_syncPath,
      m_userEmail, m_providerName);
}

static std::wstring getCurrentUserSid() {
  HANDLE hToken = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    return L"";
  DWORD size = 0;
  GetTokenInformation(hToken, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> buf(size);
  if (!GetTokenInformation(hToken, TokenUser, buf.data(), size, &size)) {
    CloseHandle(hToken);
    return L"";
  }
  CloseHandle(hToken);
  auto *tu = reinterpret_cast<TOKEN_USER *>(buf.data());
  LPWSTR sidStr = nullptr;
  if (!ConvertSidToStringSidW(tu->User.Sid, &sidStr))
    return L"";
  std::wstring result(sidStr);
  LocalFree(sidStr);
  return result;
}

static std::wstring getSyncRootId(const std::string &providerName,
                                  const std::string &userEmail) {
  std::wstring userSid = getCurrentUserSid();
  std::wstring guidStr = CloudFilesProvider::toWide(providerName);
  std::wstring accountId = CloudFilesProvider::toWide(userEmail);
  std::wstring syncRootId = guidStr + L"!" + userSid + L"!" + accountId;
  return syncRootId;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync root registration
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::registerSyncRoot() {
  // CF_SYNC_REGISTRATION — tells Windows about our provider
  //
  std::wstring unifiedId = getSyncRootId(m_providerName, m_userEmail);

  // 2. WinRT Registration (The "UI" and Icons)
  std::wstring syncpathW = fs::path(m_syncPathW).make_preferred().wstring();
  /*
  try {
    winrt::init_apartment();
    winrt::Windows::Storage::Provider::StorageProviderSyncRootManager::
        Unregister(unifiedId);

  } catch (winrt::hresult_error const &ex) {
    // THIS IS THE SMOKING GUN
    std::wcerr << L"FAILED TO UNREGISTER: " << ex.message().c_str() << L" (0x"
               << std::hex << ex.to_abi() << L")" << std::endl;
  }

  try {
    winrt::init_apartment();
    winrt::Windows::Storage::Provider::StorageProviderSyncRootInfo info;
    info.Id(unifiedId); // This MUST match the kernel identity
    info.DisplayNameResource(toWide(m_providerName));
    std::wcout << "[CFProvider] m_syncPathW : " << syncpathW << std::endl;
    auto asyncOp =
        winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
            syncpathW);
    auto folder = asyncOp.get();
    if (folder != nullptr) {
      info.Path(folder);
      info.HydrationPolicy(winrt::Windows::Storage::Provider::
                               StorageProviderHydrationPolicy::Full);
      info.HydrationPolicyModifier(
          winrt::Windows::Storage::Provider::
              StorageProviderHydrationPolicyModifier::None);
      info.PopulationPolicy(winrt::Windows::Storage::Provider::
                                StorageProviderPopulationPolicy::Full);
      info.InSyncPolicy(winrt::Windows::Storage::Provider::
                            StorageProviderInSyncPolicy::FileCreationTime |
                        winrt::Windows::Storage::Provider::
                            StorageProviderInSyncPolicy::DirectoryCreationTime);
      // Use your deterministic GUID
      info.ProviderId(m_providerGuid);
      info.Version(L"1.0");
      info.ShowSiblingsAsGroup(false);
      info.HardlinkPolicy(winrt::Windows::Storage::Provider::
                              StorageProviderHardlinkPolicy::None);
      // This replaces all your manual RegCreateKeyExW code
      winrt::Windows::Storage::Provider::StorageProviderSyncRootManager::
          Register(info);
      std::cout << "SUCCESS: Registered with Windows Shell." << std::endl;
    }

  } catch (winrt::hresult_error const &ex) {
    // THIS IS THE SMOKING GUN
    std::wcerr << L"FAILED: " << ex.message().c_str() << L" (0x" << std::hex
               << ex.to_abi() << L")" << std::endl;
  }
  */
  CF_SYNC_REGISTRATION reg = {};
  reg.StructSize = sizeof(reg);

  std::wstring providerNameW = toWide(m_providerName);
  std::wstring providerVer = L"1.0";

  reg.ProviderName = providerNameW.c_str();
  reg.ProviderVersion = providerVer.c_str();
  reg.ProviderId = m_providerGuid;
  reg.StructSize = sizeof(reg);
  std::string narrowId = toNarrow(unifiedId);
  reg.SyncRootIdentity = (LPCVOID)narrowId.c_str();
  reg.SyncRootIdentityLength = (DWORD)narrowId.size();
  // CF_SYNC_POLICIES — controls hydration and population behaviour
  CF_SYNC_POLICIES policies = {};
  policies.StructSize = sizeof(policies);

  // FULL hydration: when the user opens a file, Windows calls FETCH_DATA
  // and waits for us to deliver all bytes before handing the handle to the app.
  policies.Hydration.Primary = CF_HYDRATION_POLICY_FULL;
  policies.Hydration.Modifier = CF_HYDRATION_POLICY_MODIFIER_NONE;

  // PARTIAL population: we create placeholders for items we know about,
  // but we do NOT have to enumerate the entire cloud upfront.
  //  policies.Population.Primary = CF_POPULATION_POLICY_PARTIAL;
  policies.Population.Primary = CF_POPULATION_POLICY_FULL;
  policies.Population.Modifier = CF_POPULATION_POLICY_MODIFIER_NONE;

  // Track all in-sync states so overlay icons work
  policies.InSync = CF_INSYNC_POLICY_TRACK_FILE_CREATION_TIME |
                    CF_INSYNC_POLICY_TRACK_DIRECTORY_CREATION_TIME;

  policies.HardLink = CF_HARDLINK_POLICY_NONE;
  // DEFAULT management policy — deletion behavior when offline is controlled
  // by the placeholder pin/unpin state, not by this policy.
  // Placeholders marked CF_PIN_STATE_UNSPECIFIED (the default) can be
  // deleted offline. Only pinned files ("Always keep on device") are
  // protected from offline deletion.
  policies.PlaceholderManagement = CF_PLACEHOLDER_MANAGEMENT_POLICY_DEFAULT;

  HRESULT hr = CfRegisterSyncRoot(
      syncpathW.c_str(), &reg, &policies,
      CF_REGISTER_FLAG_UPDATE |
          CF_REGISTER_FLAG_DISABLE_ON_DEMAND_POPULATION_ON_ROOT);

  if (FAILED(hr)) {
    std::cerr << "[CFProvider] CfRegisterSyncRoot failed: 0x" << std::hex << hr
              << "\n";
    return false;
  }

  std::cout << "[CFProvider] Sync root registered: " << m_syncPath << "\n";
  return true;
}

bool CloudFilesProvider::unregisterSyncRoot() {
  HRESULT hr = CfUnregisterSyncRoot(m_syncPathW.c_str());
  if (FAILED(hr)) {
    std::cerr << "[CFProvider] CfUnregisterSyncRoot failed: 0x" << std::hex
              << hr << "\n";
    return false;
  }
  std::cout << "[CFProvider] Sync root unregistered.\n";
  return true;
}

bool CloudFilesProvider::isSyncRootRegistered() const {
  // Try to query the sync root info — succeeds only if registered
  CF_SYNC_ROOT_BASIC_INFO info = {};
  DWORD returned = 0;
  HRESULT hr =
      CfGetSyncRootInfoByPath(m_syncPathW.c_str(), CF_SYNC_ROOT_INFO_BASIC,
                              &info, sizeof(info), &returned);
  return SUCCEEDED(hr);
}

bool CloudFilesProvider::registerShellIcons() {

  std::wstring userSid = getCurrentUserSid();
  std::wstring syncRootId = getSyncRootId(m_providerName, m_userEmail);

  // Base registry key
  std::wstring baseKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                         L"Explorer\\SyncRootManager\\" +
                         syncRootId;
  // ── Write main key ────────────────────────────────────────────────────────
  HKEY hKey = nullptr;
  LONG res = RegCreateKeyExW(
      HKEY_LOCAL_MACHINE, baseKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
      KEY_SET_VALUE | KEY_CREATE_SUB_KEY, nullptr, &hKey, nullptr);
  if (res != ERROR_SUCCESS) {
    std::cerr << "[CFProvider] registerShellIcons: RegCreateKeyEx failed: "
              << res << "\n";
    return false;
  }

  // DisplayNameResource — shown in Explorer nav pane
  std::wstring displayName = CloudFilesProvider::toWide(m_providerName);
  RegSetValueExW(
      hKey, L"DisplayNameResource", 0, REG_SZ,
      reinterpret_cast<const BYTE *>(displayName.c_str()),
      static_cast<DWORD>((displayName.size() + 1) * sizeof(wchar_t)));

  // IconResource — icon for status column overlays, use exe icon index 0
  wchar_t exePath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  std::wstring iconRes = std::wstring(exePath) + L",0";
  RegSetValueExW(hKey, L"IconResource", 0, REG_SZ,
                 reinterpret_cast<const BYTE *>(iconRes.c_str()),
                 static_cast<DWORD>((iconRes.size() + 1) * sizeof(wchar_t)));

  // ADD THE NEW FLAGS VALUE HERE
  //  DWORD flagsValue = 34;
  DWORD flagsValue = 0x00000020 | 0x00000002;
  RegSetValueExW(hKey, L"Flags", 0, REG_DWORD,

                 reinterpret_cast<const BYTE *>(&flagsValue), sizeof(DWORD));

  DWORD symlinkVal = 0;
  RegSetValueExW(hKey, L"SymlinkHandling", 0, REG_DWORD,
                 reinterpret_cast<const BYTE *>(&symlinkVal), sizeof(DWORD));

  RegCloseKey(hKey);

  // ── Write UserSyncRoots sub-key ───────────────────────────────────────────
  std::wstring userRootsKey = baseKey + L"\\UserSyncRoots";
  HKEY hRootsKey = nullptr;
  res = RegCreateKeyExW(HKEY_LOCAL_MACHINE, userRootsKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &hRootsKey, nullptr);
  if (res == ERROR_SUCCESS) {
    RegSetValueExW(
        hRootsKey, userSid.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE *>(
            fs::path(m_syncPathW).make_preferred().wstring().c_str()),
        static_cast<DWORD>(
            (fs::path(m_syncPathW).make_preferred().wstring().size() + 1) *
            sizeof(wchar_t)));
    RegCloseKey(hRootsKey);
  }

  // ── Re-register sync root with matching SyncRootIdentity ─────────────────
  // CF API links kernel registration to shell entry via SyncRootIdentity.
  // Must match the registry key name we just wrote.
  /*
  {
    CF_SYNC_REGISTRATION reg = {};
    reg.StructSize = sizeof(reg);
    std::wstring nameW = CloudFilesProvider::toWide(m_providerName);
    std::wstring verW = L"1.0";
    reg.ProviderName = nameW.c_str();
    reg.ProviderVersion = verW.c_str();
    reg.ProviderId = m_providerGuid;
    std::string idUtf8(syncRootId.begin(), syncRootId.end());
    // reg.SyncRootIdentity = idUtf8.data();
    reg.SyncRootIdentity = (LPCVOID)idUtf8.c_str();
    //    reg.SyncRootIdentityLength = static_cast<DWORD>(idUtf8.size());
    reg.SyncRootIdentityLength = (DWORD)idUtf8.size();

    CF_SYNC_POLICIES policies = {};
    policies.StructSize = sizeof(policies);
    policies.Hydration.Primary = CF_HYDRATION_POLICY_PARTIAL;
    policies.Hydration.Modifier = CF_HYDRATION_POLICY_MODIFIER_NONE;
    policies.Population.Primary = CF_POPULATION_POLICY_ALWAYS_FULL;
    policies.Population.Modifier = CF_POPULATION_POLICY_MODIFIER_NONE;
    policies.InSync = CF_INSYNC_POLICY_TRACK_ALL;
    policies.HardLink = CF_HARDLINK_POLICY_NONE;
    policies.PlaceholderManagement = CF_PLACEHOLDER_MANAGEMENT_POLICY_DEFAULT;

    CfRegisterSyncRoot(m_syncPathW.c_str(), &reg, &policies,
                       CF_REGISTER_FLAG_UPDATE);
  }
  */
  // Tell Explorer to refresh shell overlays immediately
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

  std::cout << "[CFProvider] Shell icons registered.\n";
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime connection
// ─────────────────────────────────────────────────────────────────────────────

void CloudFilesProvider::run() {

  m_stopThread = false;
  std::cout << "[CFProvider] starting a new Thread..." << std::endl;
  m_thread = std::thread(&CloudFilesProvider::start, this);
}

bool CloudFilesProvider::start() {
  if (m_connected.load())
    return true;

  // Register the sync root if not already done
  if (!isSyncRootRegistered()) {
    if (!registerSyncRoot())
      return false;
  }
  registerShellIcons();

  // CF_CALLBACK_REGISTRATION array — terminated by
  // CF_CALLBACK_REGISTRATION_END We pass `this` as CallbackContext so
  // static callbacks can reach our members.
  CF_CALLBACK_REGISTRATION callbacks[] = {
      {CF_CALLBACK_TYPE_FETCH_DATA, onFetchData},
      {CF_CALLBACK_TYPE_NOTIFY_DEHYDRATE, onDehydrateFile},
      {CF_CALLBACK_TYPE_CANCEL_FETCH_DATA, onCancelFetchData},
      {CF_CALLBACK_TYPE_FETCH_PLACEHOLDERS, onFetchPlaceholders},
      {CF_CALLBACK_TYPE_NOTIFY_DELETE, onNotifyFileDelete},
      {CF_CALLBACK_TYPE_NOTIFY_FILE_OPEN_COMPLETION, onNotifyFileOpened},
      {CF_CALLBACK_TYPE_NOTIFY_FILE_CLOSE_COMPLETION, onNotifyFileClosed},
      CF_CALLBACK_REGISTRATION_END};

  HRESULT hr = CfConnectSyncRoot(
      m_syncPathW.c_str(), callbacks,
      this, // CallbackContext — passed back in CF_CALLBACK_INFO
      CF_CONNECT_FLAG_REQUIRE_PROCESS_INFO |
          CF_CONNECT_FLAG_REQUIRE_FULL_FILE_PATH,
      &m_connectionKey);

  if (FAILED(hr)) {
    std::cerr << "[CFProvider] CfConnectSyncRoot failed: 0x" << std::hex << hr
              << "\n";
    return false;
  }

  m_connected.store(true);
  std::cout << "[CFProvider] Connected to sync root.\n";
  return true;
}

void CloudFilesProvider::stop() {
  m_stopThread = true;
  if (m_thread.joinable()) {
    if (!m_connected.exchange(false))
      return;

    HRESULT hr = CfDisconnectSyncRoot(m_connectionKey);
    if (FAILED(hr)) {
      std::cerr << "[CFProvider] CfDisconnectSyncRoot failed: 0x" << std::hex
                << hr << "\n";
    } else {
      std::cout << "[CFProvider] Disconnected from sync root.\n";
    }
    m_thread.join();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Placeholder creation helpers
// ─────────────────────────────────────────────────────────────────────────────

// CF API stores an opaque "file identity" blob on each placeholder.
// We store the file's UUID (std::string) so FETCH_DATA can identify what
// to download without touching the filesystem or database.
struct PlaceholderIdentity {
  char uuid[128]; // null-terminated UTF-8 UUID string
};

CF_PLACEHOLDER_CREATE_INFO CloudFilesProvider::buildFilePlaceholderInfo(
    const CloudFileMetadata &file) const {
  CF_PLACEHOLDER_CREATE_INFO info = {};
  info.RelativeFileName =
      toWide(file.filename).c_str(); // NOTE: see comment below

  // Metadata — shown in Explorer without downloading
  info.FsMetadata.FileSize.QuadPart = file.size;

  // Convert timestamps
  if (!file.last_modified.empty()) {
    try {
      int64_t ts = std::stoll(file.last_modified);
      LARGE_INTEGER li = unixToLargeIntFileTime(ts);
      info.FsMetadata.BasicInfo.LastWriteTime = li;
      info.FsMetadata.BasicInfo.CreationTime = li;
      info.FsMetadata.BasicInfo.LastAccessTime = li;
      info.FsMetadata.BasicInfo.ChangeTime = li;
    } catch (...) {
    }
  }

  info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
  info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;

  // Store UUID as identity blob so FETCH_DATA knows what to download
  static PlaceholderIdentity
      identity; // NOTE: not thread-safe for concurrent creates — see below
  memset(&identity, 0, sizeof(identity));
  strncpy_s(identity.uuid, file.uuid.c_str(), sizeof(identity.uuid) - 1);
  info.FileIdentity = &identity;
  info.FileIdentityLength = sizeof(identity);

  return info;
}

CF_PLACEHOLDER_CREATE_INFO CloudFilesProvider::buildDirPlaceholderInfo(
    const std::string &relPath, const std::string &uuid,
    const std::string &created_at) const {
  CF_PLACEHOLDER_CREATE_INFO info = {};
  std::string dirName = fs::path(relPath).filename().string();
  info.RelativeFileName = toWide(dirName).c_str();

  if (!created_at.empty()) {
    try {
      int64_t ts = std::stoll(created_at);
      LARGE_INTEGER li = unixToLargeIntFileTime(ts);
      info.FsMetadata.BasicInfo.CreationTime = li;
      info.FsMetadata.BasicInfo.LastWriteTime = li;
      info.FsMetadata.BasicInfo.LastAccessTime = li;
      info.FsMetadata.BasicInfo.ChangeTime = li;
    } catch (...) {
    }
  }
  info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
  info.FsMetadata.FileSize.QuadPart = 0;
  info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;

  static PlaceholderIdentity dirIdentity;
  memset(&dirIdentity, 0, sizeof(dirIdentity));
  strncpy_s(dirIdentity.uuid, uuid.c_str(), sizeof(dirIdentity.uuid) - 1);
  info.FileIdentity = &dirIdentity;
  info.FileIdentityLength = sizeof(dirIdentity);

  return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public placeholder creation
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::createFilePlaceholder(
    const CloudFileMetadata &file, const std::vector<std::string> &paths) {
  // Resolve the parent directory absolute path
  std::wstring parentDirW;
  std::string parentDir;
  if (file.path == "/") {
    parentDirW = m_syncPathW;
    parentDir = m_syncPath;
  } else {
    parentDirW = m_syncPathW + toWide(file.path);
    parentDir = m_syncPath + file.path;
  }
  // bool success = createDirsPlaceholder(parentDir, file.dirIDs.value(),
  // paths); if (!success)
  //    return false;
  // Ensure parent directory exists as a real or placeholder directory
  if (!fs::exists(toNarrow(parentDirW))) {
    std::cerr << "[CFProvider] Parent dir missing for: " << file.filename
              << " at " << file.path << "\n";
    return false;
  }

  // Build identity blob — use a local copy per-call to be thread-safe
  PlaceholderIdentity identity = {};
  strncpy_s(identity.uuid, file.uuid.c_str(), sizeof(identity.uuid) - 1);

  CF_PLACEHOLDER_CREATE_INFO info = {};

  std::wstring filenameW = toWide(file.filename);
  info.RelativeFileName = filenameW.c_str();
  info.FsMetadata.FileSize.QuadPart = file.size;
  if (!file.last_modified.empty()) {
    try {
      int64_t ts = std::stoll(file.last_modified);
      LARGE_INTEGER li = unixToLargeIntFileTime(ts);
      info.FsMetadata.BasicInfo.LastWriteTime = li;
      info.FsMetadata.BasicInfo.CreationTime = li;
      info.FsMetadata.BasicInfo.LastAccessTime = li;
      info.FsMetadata.BasicInfo.ChangeTime = li;
    } catch (...) {
    }
  }
  info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
  info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
  info.FileIdentity = &identity;
  info.FileIdentityLength = sizeof(identity);

  std::string absPath = (file.path == "/")
                            ? m_syncPath + "/" + file.filename
                            : m_syncPath + file.path + "/" + file.filename;

  // Tell SyncWorker to ignore the Added event this will generate
  m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);
  DWORD entriesProcessed = 0;
  HRESULT hr = CfCreatePlaceholders(parentDirW.c_str(), &info, 1,
                                    CF_CREATE_FLAG_NONE, &entriesProcessed);

  if (FAILED(hr) || entriesProcessed == 0) {
    //    m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Added);
    std::cerr << "[CFProvider] CfCreatePlaceholders failed for "
              << file.filename << ": 0x" << std::hex << hr << "\n";
    return false;
  }

  std::cout << "[CFProvider] Placeholder created: " << file.path << "/"
            << file.filename << "\n";

  std::wstring absPathW = toWide(absPath);

  HANDLE hFile = CreateFileW(
      absPathW.c_str(), WRITE_DAC | FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "[CFProvider] Could not open placeholder for post-setup: "
              << absPath << " err=" << GetLastError() << "\n";
    return true;
  }

  HRESULT hrCfPin = CfSetPinState(hFile, CF_PIN_STATE_UNPINNED,
                                  CF_SET_PIN_FLAG_NONE, nullptr);
  if (FAILED(hrCfPin)) {
    std::cerr << "[CFProvider] Unable to Pin the placeholder to UNPINNED : "
              << file.path << std::endl;
    return true;
  }
  std::cout << "[CFProvider] PINNED the place holder: " << file.path
            << std::endl;
  return true;
}

bool CloudFilesProvider::createDirsPlaceholder(
    const std::string &absPath,
    const std::map<std::string, std::string> &dirIDs,
    const std::vector<std::string> &paths) {
  try {
    bool success = true;
    if (!fs::exists(absPath)) {
      std::vector<LocalFolderCreateMetadata> dirs;
      for (auto &path : paths) {
        auto fp = path == "/" ? m_syncPath : m_syncPath + path;
        if (!fs::exists(fp)) {
          m_syncWorker.addIgnoreEvent(fp, WatchEvent::Added);
          LocalFolderCreateMetadata dir;

          dir.folder = Utility::getFolderDevice(fs::path(fp)).folder;
          dir.absPath = fp;
          dir.device = Utility::getFolderDevice(fs::path(fp)).device;
          dir.uuid = "";
          dir.path = path;
          auto now = std::chrono::system_clock::now().time_since_epoch();
          auto timestamp =
              std::chrono::duration_cast<std::chrono::seconds>(now).count();
          dir.created_at = std::to_string(timestamp);

          std::optional<DirectoryMetadata> dirExists{std::nullopt};
          {
            std::lock_guard<std::recursive_mutex> lock(
                m_dbManager.getSyncMutex());
            dirExists = m_dbManager.getDirectoryByPath(dir.device, dir.folder,
                                                       dir.path);
          }

          if (!dirExists.has_value()) {
            if (dirIDs.count(path)) {
              dir.uuid = dirIDs.at(path);
            }
            if (!dir.uuid.empty()) {
              dirs.push_back(dir);
            }
          }
          if (dirExists.has_value()) {
            dir.uuid = dirExists.value().uuid;
            dirs.push_back(dir);
          }
          success = createDirPlaceholder(dir);
          if (!success)
            return false;
        }
      }
    }
    return success;
  } catch (const std::exception &e) {
    std::cerr << "[CFProvider] dir Creation: " << e.what() << std::endl;
    return false;
  }
}

bool CloudFilesProvider::createDirPlaceholder(
    const LocalFolderCreateMetadata &dir) {
  std::wstring absDirW = fs::path(dir.absPath).make_preferred().wstring();
  // std::wstring absDirW = toWide(dir.absPath);
  m_syncWorker.addIgnoreEvent(dir.absPath, WatchEvent::Added);
  /*
  try {
    if (!fs::exists(dir.absPath)) {
      fs::create_directories(dir.absPath);
    }
  } catch (const std::exception &e) {
    m_syncWorker.removeIgnoreEvent(dir.absPath, WatchEvent::Added);
    std::cerr << "[CFProvider] createDirPlaceHolder: fs::create_directories "
                 "failed for "
              << dir.absPath << " : " << e.what() << std::endl;
  }

  HANDLE hDir = CreateFileW(
      absDirW.c_str(), FILE_WRITE_ATTRIBUTES | WRITE_DAC,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);

  if (hDir == INVALID_HANDLE_VALUE) {
    std::cerr << "[CFProvider] createDirPlaceholder : cannot open dir "
              << dir.absPath << " error=" << GetLastError() << std::endl;
    return false;
  }
  PlaceholderIdentity identity = {};
  strncpy_s(identity.uuid, dir.uuid.c_str(), sizeof(identity.uuid) - 1);

  HRESULT hr =
      CfConvertToPlaceholder(hDir, &identity, sizeof(identity),
                             CF_CONVERT_FLAG_MARK_IN_SYNC, nullptr, nullptr);
  if (FAILED(hr)) {
    if (hr != 0x80070161L && hr != 0x80071128L) {
      std::cerr << "[CFProvider] CfConvertToPlaceholder (dir) failed for "
                << dir.path << ": 0x" << std::hex << hr << "\n";
      CloseHandle(hDir);
      return false;
    }
  }
  // Step 4: Set timestamps from cloud metadata
  if (!dir.created_at.empty()) {
    try {
      std::cout << "[CFProvider] createDirPlaceholder: dir.created_at "
                << std::stoll(dir.created_at) << std::endl;
      int64_t ts = std::stoll(dir.created_at);
      LARGE_INTEGER li = unixToLargeIntFileTime(ts);
      FILE_BASIC_INFO fbi = {};
      fbi.CreationTime = li;
      fbi.LastWriteTime = li;
      fbi.LastAccessTime = li;
      fbi.ChangeTime = li;
      fbi.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      SetFileInformationByHandle(hDir, FileBasicInfo, &fbi, sizeof(fbi));
    } catch (...) {
    }
  }

  // Step 5: Mark in-sync and set pin state (allow offline deletion)
  CfSetInSyncState(hDir, CF_IN_SYNC_STATE_IN_SYNC, CF_SET_IN_SYNC_FLAG_NONE,
                   nullptr);
  CfSetPinState(hDir, CF_PIN_STATE_UNSPECIFIED, CF_SET_PIN_FLAG_NONE, nullptr);

  CloseHandle(hDir);
  */

  std::string parentPath =
      fs::path(dir.path).parent_path().make_preferred().string();
  if (parentPath.empty())
    parentPath = "\\";
  std::wstring parentDirW =
      (parentPath == "\\")
          ? fs::path(m_syncPathW).make_preferred().wstring() + L"\\"
          : fs::path(m_syncPathW).make_preferred().wstring() +
                toWide(parentPath) + L"\\";

  std::string dirName = fs::path(dir.path).filename().string();
  std::wstring dirNameW = toWide(dirName);

  PlaceholderIdentity identity = {};
  strncpy_s(identity.uuid, dir.uuid.c_str(), sizeof(identity.uuid) - 1);

  CF_PLACEHOLDER_CREATE_INFO info = {};
  std::wcout << "[CFProvider] dirNameW :  " << dirNameW

             << "  parentDirW : " << parentDirW << std::endl;

  info.RelativeFileName = dirNameW.c_str();

  if (!dir.created_at.empty()) {
    try {
      int64_t ts = std::stoll(dir.created_at);
      LARGE_INTEGER li = unixToLargeIntFileTime(ts);
      info.FsMetadata.BasicInfo.CreationTime = li;
      info.FsMetadata.BasicInfo.LastWriteTime = li;
      info.FsMetadata.BasicInfo.LastAccessTime = li;
      info.FsMetadata.BasicInfo.ChangeTime = li;
    } catch (...) {
    }
  }
  info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
  info.FsMetadata.FileSize.QuadPart = 0;
  info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
  info.FileIdentity = &identity;
  info.FileIdentityLength = sizeof(identity);

  m_syncWorker.addIgnoreEvent(dir.absPath, WatchEvent::Added);

  DWORD entriesProcessed = 0;
  HRESULT hr = CfCreatePlaceholders(parentDirW.c_str(), &info, 1,
                                    CF_CREATE_FLAG_NONE, &entriesProcessed);

  if (FAILED(hr) || entriesProcessed == 0) {
    m_syncWorker.removeIgnoreEvent(dir.absPath, WatchEvent::Added);
    std::cerr << "[CFProvider] CfCreatePlaceholders (dir) failed for "
              << dir.path << ": 0x" << std::hex << hr << "\n";
    return false;
  }

  markInSync(absDirW);

  std::cout << "[CFProvider] Dir placeholder created: " << dir.path << "\n";
  return true;
}

bool CloudFilesProvider::markUnspecifiedPinned(const std::wstring &absPath) {
  HANDLE hFile =
      CreateFileW(absPath.c_str(), WRITE_DAC,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS, // required to open directories
                  nullptr);

  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  HRESULT hr = CfSetPinState(hFile, CF_PIN_STATE_UNSPECIFIED,
                             CF_SET_PIN_FLAG_NONE, nullptr);
  CloseHandle(hFile);
  return SUCCEEDED(hr);
}

// ─────────────────────────────────────────────────────────────────────────────
// In-sync marking and dehydration
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::markInSync(const std::wstring &absPath) {
  HANDLE hFile =
      CreateFileW(absPath.c_str(), WRITE_DAC,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS, // required to open directories
                  nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    std::wcerr << "[CfProvider] markInSync: invalid file Handle: 0x" << hFile
               << std::endl;
    return false;
  }

  CF_SET_IN_SYNC_FLAGS flags = CF_SET_IN_SYNC_FLAG_NONE;
  HRESULT hr =
      CfSetInSyncState(hFile, CF_IN_SYNC_STATE_IN_SYNC, flags, nullptr);
  CloseHandle(hFile);
  if (SUCCEEDED(hr))
    return true;
  else {
    std::cerr << "[CfProvider] mark In Sync failed: 0x" << std::hex << hr
              << std::endl;
    return false;
  }
}

bool CloudFilesProvider::markNotInSync(const std::wstring &absPath) {
  HANDLE hFile =
      CreateFileW(absPath.c_str(), WRITE_DAC,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  HRESULT hr = CfSetInSyncState(hFile, CF_IN_SYNC_STATE_NOT_IN_SYNC,
                                CF_SET_IN_SYNC_FLAG_NONE, nullptr);
  CloseHandle(hFile);
  return SUCCEEDED(hr);
}

bool CloudFilesProvider::dehydrateFile(const std::wstring &absPath,
                                       const std::string &uuid) {
  std::string absPathN = toNarrow(absPath);
  m_syncWorker.addIgnoreEvent(absPathN, WatchEvent::Modified);
  HANDLE hFile = CreateFileW(
      absPath.c_str(), WRITE_DAC,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "[CFProvider] dehydrateFile: cannot open file\n";
    return false;
  }
  // convert to Place holder
  PlaceholderIdentity identity = {};
  strncpy_s(identity.uuid, uuid.c_str(), sizeof(identity.uuid) - 1);

  HRESULT hrConvert =
      CfConvertToPlaceholder(hFile, &identity, sizeof(identity),
                             CF_CONVERT_FLAG_MARK_IN_SYNC, nullptr, nullptr

      );

  if (FAILED(hrConvert)) {
    std::wcerr << "[CFProvider] conversion to place holder failed" << absPath
               << std::endl;
    return false;
  }
  // Dehydrate the entire file: offset 0, length -1 means "full file"
  LARGE_INTEGER startOffset = {};
  startOffset.QuadPart = 0;
  LARGE_INTEGER length = {};
  length.QuadPart = -1; // -1 means full file
  HRESULT hrHydrate = CfDehydratePlaceholder(
      hFile, startOffset, length, CF_DEHYDRATE_FLAG_BACKGROUND, nullptr);

  if (FAILED(hrHydrate)) {
    std::cerr << "[CFProvider] CfDehydratePlaceholder failed: 0x" << std::hex
              << hrHydrate << "\n";
    return false;
  }

  CloseHandle(hFile);
  if (hrConvert == S_OK) {
    std::cout << "[CFProvider] CfDehydratePlaceholder SUCCESS : " << hrConvert
              << "\n";
  }
  return true;
}

bool CloudFilesProvider::hydrateFile(const std::wstring &absPath) {
  HANDLE hFile =
      CreateFileW(absPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  // Simply opening the file with GENERIC_READ triggers hydration via
  // FETCH_DATA. We wait for it to complete by reading one byte.
  char buf;
  DWORD bytesRead = 0;
  ReadFile(hFile, &buf, 1, &bytesRead, nullptr);
  CloseHandle(hFile);
  return true;
}

bool CloudFilesProvider::revertPlaceholder(const std::wstring &absPath,
                                           bool isDirectory) {
  // CfRevertPlaceholder converts a CF placeholder back to a normal file.
  // After this the file can be deleted/moved without the sync provider
  // running. The file must be fully hydrated (bytes local) before reverting —
  // if it's a ghost, hydrate it first or just delete the placeholder
  // directly.
  DWORD flags = isDirectory
                    ? FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT
                    : FILE_FLAG_OPEN_REPARSE_POINT;

  HANDLE hFile = CreateFileW(
      absPath.c_str(),
      WRITE_DAC | FILE_WRITE_ATTRIBUTES | 0x00010000L, // DELETE access right
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, flags, nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "[CFProvider] revertPlaceholder: cannot open "
              << toNarrow(absPath) << " err=" << GetLastError() << "\n";
    return false;
  }

  HRESULT hr = CfRevertPlaceholder(hFile, CF_REVERT_FLAG_NONE, nullptr);
  CloseHandle(hFile);

  if (FAILED(hr)) {
    // E_INVALIDARG means it was already a normal file — not an error
    if (hr != E_INVALIDARG) {
      std::cerr << "[CFProvider] CfRevertPlaceholder failed: 0x" << std::hex
                << hr << "\n";
      return false;
    }
  }
  return true;
}

void CloudFilesProvider::revertAllPlaceholders() {
  std::cout << "[CFProvider] Reverting all placeholders in: " << m_syncPath
            << "\n";
  try {
    // Revert files first (depth-first), then directories
    std::vector<std::wstring> dirs;
    for (auto &entry : fs::recursive_directory_iterator(
             m_syncPath, fs::directory_options::skip_permission_denied)) {
      std::wstring pathW = toWide(entry.path().string());
      if (entry.is_directory()) {
        dirs.push_back(pathW);
      } else if (entry.is_regular_file()) {
        revertPlaceholder(pathW, false);
      }
    }
    // Revert directories in reverse order (deepest first)
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
      revertPlaceholder(*it, true);
    }
  } catch (const std::exception &e) {
    std::cerr << "[CFProvider] revertAllPlaceholders error: " << e.what()
              << "\n";
  }
  std::cout << "[CFProvider] All placeholders reverted.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// CF Callback: FETCH_DATA
//
// Windows calls this when an application opens a placeholder file and the
// bytes are not yet local. We must download the required byte range and
// feed it back to the kernel via CfExecute(CF_OPERATION_TYPE_TRANSFER_DATA).
//
// params->FetchData.RequiredFileOffset / RequiredLength — the range Windows
//   needs RIGHT NOW to unblock the calling process (must deliver this fast).
// params->FetchData.OptionalFileOffset / OptionalLength — additional range
//   Windows would like if we have bandwidth (pre-fetch hint, can ignore).
// ─────────────────────────────────────────────────────────────────────────────

void CALLBACK CloudFilesProvider::onDehydrateFile(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  std::cout << "[CFProvider] dehydrateFile invoked " << std::endl;
  if (!info || !info->CallbackContext || !info->FileIdentity)
    return;
  auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);
  const auto *identity =
      static_cast<const PlaceholderIdentity *>(info->FileIdentity);
  if (!identity || identity->uuid[0] == '\0') {
    std::cerr << "[CFProvider] DEHYDRATEFILE: null/empty fileidentity"
              << std::endl;
    return;
  }
  std::string uuid(identity->uuid);
  std::wstring volumename = info->VolumeDosName;
  std::wstring absPathW = volumename + info->NormalizedPath;
  std::string absPath = toNarrow(absPathW);
  std::string relPath = absPath.substr(self->m_syncPath.size());
  std::replace(relPath.begin(), relPath.end(), '\\', '/');
  std::string filename = fs::path(absPath).filename().string();
  std::string dirPath = fs::path(relPath).parent_path().generic_string();
  self->m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);
  HANDLE hFile = CreateFileW(
      absPathW.c_str(), WRITE_DAC,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "[CFProvider] dehydrateFile: cannot open file\n";
    return;
  }
  LARGE_INTEGER startOffset = {};
  startOffset.QuadPart = 0;
  LARGE_INTEGER length = {};
  length.QuadPart = -1;
  HRESULT hr = CfDehydratePlaceholder(hFile, startOffset, length,
                                      CF_DEHYDRATE_FLAG_BACKGROUND, nullptr);
  if (FAILED(hr)) {
    std::cerr << "[CFProvider] DehydratePlaceHolder failed: 0x" << std::hex
              << hr << std::endl;
    return;
  }
  std::cout << "[CFProvider] HydratePlace holder succeded: " << hr << std::endl;
}

void CALLBACK CloudFilesProvider::onFetchData(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  if (!info || !info->CallbackContext || !info->FileIdentity)
    return;
  auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);

  // Recover UUID from the identity blob we stored when creating the
  // placeholder
  const auto *identity =
      static_cast<const PlaceholderIdentity *>(info->FileIdentity);
  if (!identity || identity->uuid[0] == '\0') {
    std::cerr << "[CFProvider] FETCH_DATA: null/empty fileIdentity"
              << std::endl;
  }
  std::string uuid(identity->uuid);
  std::wstring volumeName = info->VolumeDosName;

  std::wstring absPathW = volumeName + info->NormalizedPath;
  std::string absPath = fs::path(toNarrow(absPathW)).generic_string();
  std::string relPath = absPath.substr(self->m_syncPath.size());
  // Normalize separators
  std::replace(relPath.begin(), relPath.end(), '\\', '/');

  std::string filename = fs::path(absPath).filename().string();
  std::string dirPath = fs::path(relPath).parent_path().generic_string();
  if (dirPath.empty())
    dirPath = "/";

  std::cout << "[CFProvider] FETCH_DATA: " << relPath
            << " offset=" << params->FetchData.RequiredFileOffset.QuadPart
            << " len=" << params->FetchData.RequiredLength.QuadPart << "\n";

  // Look up the CloudFileMetadata from our database by uuid
  // We need the full metadata to call ApiClient::downloadFile()
  std::optional<FileMetadata> dbFile;
  {
    std::lock_guard<std::recursive_mutex> lock(
        self->m_dbManager.getSyncMutex());
    dbFile = self->m_dbManager.getFileByPath(dirPath, filename);
  }

  if (!dbFile.has_value()) {
    std::cerr << "[CFProvider] FETCH_DATA: file not found in DB: " << relPath
              << "\n";
    // Report failure to CF kernel so the open call returns an error
    // rather than blocking forever
    CF_OPERATION_INFO opInfo = {};
    CF_OPERATION_PARAMETERS opP = {};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
    opInfo.ConnectionKey = info->ConnectionKey;
    opInfo.TransferKey = info->TransferKey;
    opP.ParamSize = sizeof(opP.TransferData);
    opP.TransferData.Offset = params->FetchData.RequiredFileOffset;
    opP.TransferData.Buffer = nullptr;
    opP.TransferData.Length.QuadPart = 0;
    opP.TransferData.Flags = CF_OPERATION_TRANSFER_DATA_FLAG_NONE;
    CfExecute(&opInfo, &opP);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(self->m_activeFetchMtx);
    if (self->m_activeFetches.count(uuid)) {
      std::cout << "[CFProvider] FETCH_DATA: already in progress for " << uuid
                << " - skipping duplicate" << std::endl;
      return;
    }
  }

  // Register SyncWorker ignore so the write we are about to do does not
  // trigger an upload event
  self->m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);

  // Build a CloudFileMetadata from what we have in the DB
  CloudFileMetadata cloudFile;
  cloudFile.uuid = dbFile->uuid;
  cloudFile.filename = dbFile->filename;
  cloudFile.path = dbFile->path;
  cloudFile.size = dbFile->size;
  cloudFile.hashvalue = dbFile->hashvalue;
  cloudFile.last_modified = dbFile->last_modified;
  cloudFile.dirID = dbFile->dirID;
  cloudFile.origin = dbFile->origin;
  cloudFile.versions = dbFile->versions;

  // copy connection/tranfer keys
  CF_CONNECTION_KEY connKey = info->ConnectionKey;
  CF_TRANSFER_KEY transferKey = info->TransferKey;

  auto reqOffset = params->FetchData.RequiredFileOffset;
  LARGE_INTEGER reqLength = params->FetchData.RequiredLength;
  // Add activity entry so the UI shows a download spinner
  auto activity = self->m_activityStore.getActivity(uuid);
  if (!activity.has_value()) {
    SyncItem item;
    item.id = uuid;
    item.name = filename;
    item.path = relPath;
    item.type = activityToString(ActivityStatus::DOWNLOAD);
    item.meta = "file";
    item.inQueue = false;
    item.isActive = true;
    self->m_activityStore.addActivity(uuid, item);
  }

  std::string tempPath = absPath + ".cftmp";

  self->m_fetchDataPool.enqueue([self, cloudFile, absPath, absPathW, tempPath,
                                 filename, connKey, transferKey, reqOffset,
                                 reqLength, uuid]() mutable {
    auto client = self->m_apiClient.clone();

    auto completeCF = [&](bool ok) {
      CF_OPERATION_INFO opInfo = {sizeof(opInfo),
                                  CF_OPERATION_TYPE_TRANSFER_DATA};
      opInfo.ConnectionKey = connKey;
      opInfo.TransferKey = transferKey;

      CF_OPERATION_PARAMETERS opP = {0};
      opP.ParamSize = sizeof(CF_OPERATION_PARAMETERS);

      if (ok) {
        opP.TransferData.CompletionStatus = 0; // STATUS_SUCCESS
      } else {
        // Use STATUS_UNSUCCESSFUL (0xC0000001) or a cloud-specific error.
        opP.TransferData.CompletionStatus = 0xC0000001;
        // Length and Offset are 0 for the failure signal
        opP.TransferData.Offset.QuadPart = 0;
        opP.TransferData.Length.QuadPart = 0;
        opP.TransferData.Buffer = nullptr;
      }

      HRESULT hr = CfExecute(&opInfo, &opP);

      if (FAILED(hr)) {
        std::cerr << "[CFProvider] Final CfExecute failed: 0x" << std::hex << hr
                  << "\n";
      }

      // Always clean up your tracking state
      std::lock_guard<std::mutex> lock(self->m_activeFetchMtx);
      self->m_activeFetches.erase(uuid);
    };

    self->m_syncWorker.addIgnoreEvent(tempPath, WatchEvent::Added);

    bool downloaded = client->downloadFile(
        cloudFile, tempPath,
        [self, uuid](const std::string &key, double progress) {
          auto it = self->m_activityStore.getActivity(uuid);
          if (it.has_value()) {
            it->progress = progress;
            it->isActive = true;
            it->isDone = progress >= 100.0;
            self->m_activityStore.updateActivity(uuid, it.value());
          }
        });

    if (!downloaded) {
      std::cerr << "[CFProvider] FETCH_DATA: download failed for " << filename
                << "\n";
      self->m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Modified);
      self->m_syncWorker.removeIgnoreEvent(tempPath, WatchEvent::Added);
      fs::remove(tempPath);

      auto it = self->m_activityStore.getActivity(uuid);
      if (it.has_value()) {
        it->isActive = false;
        it->isError = true;
        self->m_activityStore.updateActivity(uuid, it.value());
      }
      completeCF(false);
      return;
    }

    std::ifstream ifs(tempPath, std::ios::binary);
    if (!ifs) {
      std::cerr << "[CFProvider] Failed to open temp file: " << tempPath
                << "\n";
      // Notify the platform that we failed to fetch data
      completeCF(false);
      return;
    }

    // 2. Determine File Size
    ifs.seekg(0, std::ios::end);
    LONGLONG fileSize = static_cast<LONGLONG>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    // 3. Setup Chunking (1MB is efficient and 4KB aligned)
    constexpr LONGLONG PAGE_SIZE = 4096;
    constexpr LONGLONG CHUNK_SIZE = 256 * PAGE_SIZE; // 1 MB
    std::vector<char> buffer(CHUNK_SIZE);

    // Start at the offset the OS requested
    LARGE_INTEGER currentOffset = reqOffset;
    LONGLONG remainingToTransfer = reqLength.QuadPart;

    if (currentOffset.QuadPart + remainingToTransfer > fileSize) {
      remainingToTransfer = fileSize - currentOffset.QuadPart;
    }

    ifs.seekg(currentOffset.QuadPart, std::ios::beg);

    bool transferOk = true;
    while (remainingToTransfer > 0) {
      // Calculate how much to read for this chunk
      LONGLONG toRead =
          (remainingToTransfer > CHUNK_SIZE) ? CHUNK_SIZE : remainingToTransfer;

      ifs.read(buffer.data(), toRead);
      LONGLONG bytesRead = static_cast<LONGLONG>(ifs.gcount());

      if (bytesRead <= 0)
        break;

      // 4. Execute the Transfer
      CF_OPERATION_INFO opInfo = {0};
      opInfo.StructSize = sizeof(opInfo);
      opInfo.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
      opInfo.ConnectionKey = connKey;
      opInfo.TransferKey = transferKey;

      CF_OPERATION_PARAMETERS opP = {0};
      opP.ParamSize = sizeof(CF_OPERATION_PARAMETERS);
      opP.TransferData.Flags = CF_OPERATION_TRANSFER_DATA_FLAG_NONE;
      opP.TransferData.CompletionStatus = 0;
      opP.TransferData.Buffer = buffer.data();
      opP.TransferData.Offset = currentOffset;
      opP.TransferData.Length.QuadPart = bytesRead;

      HRESULT hr = CfExecute(&opInfo, &opP);

      if (FAILED(hr)) {
        std::cerr << "[CFProvider] CfExecute failed at offset "
                  << currentOffset.QuadPart << " with error: 0x" << std::hex
                  << hr << std::dec << "\n";
        transferOk = false;
        break;
      }

      currentOffset.QuadPart += bytesRead;
      remainingToTransfer -= bytesRead;
    }

    ifs.close();

    // Clean up temp file — ignore the delete event it generates
    self->m_syncWorker.addIgnoreEvent(tempPath, WatchEvent::Deleted);
    fs::remove(tempPath);

    if (transferOk) {
      // Mark the file as in-sync so Explorer shows the green checkmark
      bool isMarkedInSync = self->markInSync(absPathW);
      bool isPinUnspecified = self->markUnspecifiedPinned(absPathW);
      if (isMarkedInSync)
        std::cout << "[CFProvider] FETCH_DATA: MARKED_IN_SYNC : " << filename
                  << std::endl;
      if (isPinUnspecified)
        std::cout << "[CFProvider] FETCH_DATA: PINNED_UNSPECIFIED : "
                  << filename << std::endl;
      std::cout << "[CFProvider] FETCH_DATA COMPLETE: " << filename << "\n";

      auto it = self->m_activityStore.getActivity(uuid);
      if (it.has_value()) {
        it->isActive = false;
        it->isDone = true;
        it->progress = 100.0;
        self->m_activityStore.updateActivity(uuid, it.value());
      }
    }
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// CF Callback: CANCEL_FETCH_DATA
//
// Windows calls this when the app that triggered hydration was killed or
// the request timed out. We must stop any in-progress download for this key.
// For simplicity we just log — a production implementation should signal the
// download thread to abort via a cancellation token.
// ─────────────────────────────────────────────────────────────────────────────

void CALLBACK CloudFilesProvider::onCancelFetchData(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);
  const auto *identity =
      static_cast<const PlaceholderIdentity *>(info->FileIdentity);
  std::cout << "[CFProvider] CANCEL_FETCH_DATA: " << identity->uuid << "\n";

  // Mark as error in activity store so UI reflects the cancellation
  std::string uuid(identity->uuid);
  auto it = self->m_activityStore.getActivity(uuid);
  if (it.has_value()) {
    it->isActive = false;
    it->isError = true;
    self->m_activityStore.updateActivity(uuid, it.value());
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// CF Callback: FETCH_PLACEHOLDERS
//
// Windows calls this when the user opens a directory and Windows wants to
// populate it with placeholder entries for the items that exist in the cloud.
// We query ApiClient::getDirectoryContents() and create placeholders for
// any items that are not already present locally.
// ─────────────────────────────────────────────────────────────────────────────

void CALLBACK CloudFilesProvider::onFetchPlaceholders(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);

  // NormalizedPath is the absolute path of the directory being opened
  std::wstring volumeName = info->VolumeDosName;
  std::wstring pathW = volumeName + info->NormalizedPath;
  std::string absDirPath = toNarrow(pathW);
  std::string relDirPath = absDirPath.substr(self->m_syncPath.size());
  std::replace(relDirPath.begin(), relDirPath.end(), '\\', '/');
  if (relDirPath.empty())
    relDirPath = "/";

  std::cout << "[CFProvider] FETCH_PLACEHOLDERS: " << relDirPath << "\n";

  // Ask the cloud API for the directory contents
  auto result = self->m_apiClient.getDirectoryContents(relDirPath);
  std::cout << "[CFProvider] result -> " << result.has_value() << std::endl;
  if (!result.has_value()) {
    std::cerr << "[CFProvider] FETCH_PLACEHOLDERS: API call failed for "
              << relDirPath << "\n";

    // Report empty population so Windows does not hang
    CF_OPERATION_INFO opInfo = {};
    CF_OPERATION_PARAMETERS opP = {};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_TRANSFER_PLACEHOLDERS;
    opInfo.ConnectionKey = info->ConnectionKey;
    opInfo.TransferKey = info->TransferKey;
    opP.ParamSize = sizeof(opP.TransferPlaceholders);
    opP.TransferPlaceholders.Flags =
        CF_OPERATION_TRANSFER_PLACEHOLDERS_FLAG_NONE;
    opP.TransferPlaceholders.PlaceholderCount = 0;
    opP.TransferPlaceholders.PlaceholderArray = nullptr;
    CfExecute(&opInfo, &opP);
    return;
  }

  // Build placeholder array from cloud items
  // We need to keep wstring name buffers alive for the duration of CfExecute
  struct PlaceholderEntry {
    std::wstring nameW;
    PlaceholderIdentity identity;
    CF_PLACEHOLDER_CREATE_INFO info;
  };

  std::vector<PlaceholderEntry> entries;
  entries.reserve(result->items.size());

  for (const auto &item : result->items) {
    PlaceholderEntry entry;
    entry.nameW = item.name.toStdWString();
    memset(&entry.identity, 0, sizeof(entry.identity));
    strncpy_s(entry.identity.uuid, item.id.toStdString().c_str(),
              sizeof(entry.identity.uuid) - 1);

    CF_PLACEHOLDER_CREATE_INFO &ph = entry.info;
    ph = {};
    ph.RelativeFileName = entry.nameW.c_str();
    ph.FileIdentity = &entry.identity;
    ph.FileIdentityLength = sizeof(PlaceholderIdentity);
    std::wcout << "[CFProvider] | name : " << entry.nameW
               << " | path : " << item.path.toStdWString()
               << " | uuid : " << item.id.toStdWString()
               << " | type : " << item.type.toStdWString() << std::endl;

    bool isDir = (item.type == "folder");
    if (isDir) {
      ph.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      ph.FsMetadata.FileSize.QuadPart = 0;
    } else {
      ph.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
      try {
        ph.FsMetadata.FileSize.QuadPart = item.size.toLongLong();
      } catch (...) {
      }
    }

    // Set timestamps if available
    if (!item.lastModified.isEmpty()) {
      try {
        int64_t ts = item.lastModified.toLongLong();
        LARGE_INTEGER li = unixToLargeIntFileTime(ts);
        ph.FsMetadata.BasicInfo.LastWriteTime = li;
        ph.FsMetadata.BasicInfo.CreationTime = li;
        ph.FsMetadata.BasicInfo.LastAccessTime = li;
        ph.FsMetadata.BasicInfo.ChangeTime = li;
      } catch (...) {
      }
    }

    ph.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
    entries.push_back(std::move(entry));
  }

  // Build raw pointer array for CfExecute
  std::vector<CF_PLACEHOLDER_CREATE_INFO> rawInfos;
  rawInfos.reserve(entries.size());
  for (auto &e : entries)
    rawInfos.push_back(e.info);

  CF_OPERATION_INFO opInfo = {};
  CF_OPERATION_PARAMETERS opP = {};

  opInfo.StructSize = sizeof(opInfo);
  opInfo.Type = CF_OPERATION_TYPE_TRANSFER_PLACEHOLDERS;
  opInfo.ConnectionKey = info->ConnectionKey;
  opInfo.TransferKey = info->TransferKey;

  opP.ParamSize = sizeof(opP.TransferPlaceholders);
  opP.TransferPlaceholders.Flags =
      CF_OPERATION_TRANSFER_PLACEHOLDERS_FLAG_STOP_ON_ERROR;
  opP.TransferPlaceholders.PlaceholderCount =
      static_cast<DWORD>(rawInfos.size());
  opP.TransferPlaceholders.PlaceholderArray =
      rawInfos.empty() ? nullptr : rawInfos.data();

  HRESULT hr = CfExecute(&opInfo, &opP);
  if (FAILED(hr)) {
    std::cerr << "[CFProvider] FETCH_PLACEHOLDERS CfExecute failed: 0x"
              << std::hex << hr << "\n";
  } else {
    std::cout << "[CFProvider] FETCH_PLACEHOLDERS: created " << rawInfos.size()
              << " entries for " << relDirPath << "\n";
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// CF Callbacks: file open / close notifications
// ─────────────────────────────────────────────────────────────────────────────

void CALLBACK CloudFilesProvider::onNotifyFileOpened(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  // Informational — log which file was opened. We could use this to
  // pre-fetch the next N files in a sequence (read-ahead).
  //  std::cout << "[CFProvider] File opened: " <<
  //  toNarrow(info->NormalizedPath) << std::endl;
}

void CALLBACK CloudFilesProvider::onNotifyFileClosed(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  // If the file was modified we should trigger an upload.
  // The FilesystemWatcher will already detect the modification via efsw
  // and enqueue it through SyncWorker::handleModified — so we just log here.
  //  std::cout << "[CFProvider] File closed: " <<
  //  toNarrow(info->NormalizedPath) <<std::endl;
}

void CALLBACK CloudFilesProvider::onNotifyFileDelete(
    const CF_CALLBACK_INFO *info, const CF_CALLBACK_PARAMETERS *params) {
  CF_OPERATION_INFO opInfo = {0};
  opInfo.StructSize = sizeof(opInfo);
  opInfo.Type = CF_OPERATION_TYPE_ACK_DELETE;
  opInfo.ConnectionKey = info->ConnectionKey;
  opInfo.TransferKey = info->TransferKey;

  CF_OPERATION_PARAMETERS opParams = {0};
  opParams.ParamSize =
      sizeof(CF_OPERATION_PARAMETERS); // sizeof(opParams.AckDelete);

  // Use S_OK for success (equivalent to STATUS_SUCCESS)
  opParams.AckDelete.CompletionStatus = 0; // S_OK;

  // Correct flag name:
  opParams.AckDelete.Flags = CF_OPERATION_ACK_DELETE_FLAG_NONE;

  HRESULT hr = CfExecute(&opInfo, &opParams);
  if (FAILED(hr)) {
    std::cerr << "[CFProvider] CfExecute AckDelete failed: 0x" << std::hex << hr
              << std::endl;
  } else {
    std::wcout << "[CFProvider] CfExecute AckDelete successful: "
               << info->NormalizedPath << std::endl;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: timestamp conversion
// ─────────────────────────────────────────────────────────────────────────────

LARGE_INTEGER CloudFilesProvider::unixToLargeIntFileTime(int64_t unixMs) {
  // FILETIME epoch is Jan 1 1601; Unix epoch is Jan 1 1970.
  // Difference in 100-nanosecond intervals:
  constexpr int64_t EPOCH_DIFF = 116444736000000000LL;
  LARGE_INTEGER li;
  li.QuadPart = (unixMs * 10000LL) + EPOCH_DIFF;
  return li;
}

FILETIME
CloudFilesProvider::unixStringToFileTime(const std::string &unixSeconds) {
  FILETIME ft = {};
  try {
    int64_t ts = std::stoll(unixSeconds);
    LARGE_INTEGER li = unixToLargeIntFileTime(ts);
    ft.dwLowDateTime = li.LowPart;
    ft.dwHighDateTime = li.HighPart;
  } catch (...) {
  }
  return ft;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: GUID from provider name (deterministic, same across sessions)
// ─────────────────────────────────────────────────────────────────────────────

GUID CloudFilesProvider::makeGuidFromName(const std::string &name) {
  // Simple deterministic GUID: hash the name bytes into the GUID fields.
  // Not cryptographically safe, but stable and unique per provider name.
  GUID g = {};
  uint32_t h = 0x811c9dc5u; // FNV-1a basis
  for (unsigned char c : name) {
    h ^= c;
    h *= 0x01000193u;
  }
  g.Data1 = h;
  g.Data2 = static_cast<uint16_t>(h >> 16);
  g.Data3 = static_cast<uint16_t>(h >> 8) | 0x4000u; // version 4
  g.Data4[0] = 0x80u | static_cast<uint8_t>(h);
  g.Data4[1] = static_cast<uint8_t>(h >> 8);
  for (int i = 2; i < 8; ++i)
    g.Data4[i] = static_cast<uint8_t>(name[i % name.size()]);
  return g;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: string conversion
// ─────────────────────────────────────────────────────────────────────────────

std::wstring CloudFilesProvider::toWide(const std::string &s) {
  if (s.empty())
    return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
  std::wstring result(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      result.data(), len);
  return result;
}

std::string CloudFilesProvider::toNarrow(const std::wstring &w) {
  if (w.empty())
    return {};
  int len =
      WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          nullptr, 0, nullptr, nullptr);
  std::string result(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                      result.data(), len, nullptr, nullptr);
  return result;
}

} // namespace sync_app

#endif // _WIN32
