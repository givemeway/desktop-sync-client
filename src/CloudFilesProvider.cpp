// CloudFilesProvider.hpp includes windows.h + cfapi.h first,
// then undefs conflicting Windows macros (DELETE, ERROR, UNKNOWN etc.)
// before pulling in types.hpp. All other project headers come after
// so they see the clean namespace, not the polluted one.
#include "CloudFilesProvider.hpp"

#ifdef _WIN32

#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "ActivityStore.hpp"
#include "ApiClient.hpp"
#include "DatabaseManager.hpp"
#include "FilesystemWatcher.hpp"
#include "SyncWorker.hpp"

#pragma comment(lib, "cldapi.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace sync_app {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

CloudFilesProvider::CloudFilesProvider(
    ApiClient       &apiClient,
    DatabaseManager &dbManager,
    SyncWorker      &syncWorker,
    ActivityStore   &activityStore,
    const std::string &syncPath,
    const std::string &userEmail,
    const std::string &providerName)
    : m_apiClient(apiClient)
    , m_dbManager(dbManager)
    , m_syncWorker(syncWorker)
    , m_activityStore(activityStore)
    , m_syncPath(syncPath)
    , m_syncPathW(toWide(syncPath))
    , m_userEmail(userEmail)
    , m_providerName(providerName)
    , m_providerGuid(makeGuidFromName(providerName))
{
}

CloudFilesProvider::~CloudFilesProvider() {
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync root registration
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::registerSyncRoot() {
    // CF_SYNC_REGISTRATION — tells Windows about our provider
    CF_SYNC_REGISTRATION reg = {};
    reg.StructSize      = sizeof(reg);

    std::wstring providerNameW = toWide(m_providerName);
    std::wstring providerVer   = L"1.0";

    reg.ProviderName    = providerNameW.c_str();
    reg.ProviderVersion = providerVer.c_str();
    reg.ProviderId      = m_providerGuid;

    // CF_SYNC_POLICIES — controls hydration and population behaviour
    CF_SYNC_POLICIES policies = {};
    policies.StructSize = sizeof(policies);

    // FULL hydration: when the user opens a file, Windows calls FETCH_DATA
    // and waits for us to deliver all bytes before handing the handle to the app.
    policies.Hydration.Primary  = CF_HYDRATION_POLICY_FULL;
    policies.Hydration.Modifier = CF_HYDRATION_POLICY_MODIFIER_NONE;

    // PARTIAL population: we create placeholders for items we know about,
    // but we do NOT have to enumerate the entire cloud upfront.
    policies.Population.Primary  = CF_POPULATION_POLICY_PARTIAL;
    policies.Population.Modifier = CF_POPULATION_POLICY_MODIFIER_NONE;

    // Track all in-sync states so overlay icons work
    policies.InSync        = CF_INSYNC_POLICY_TRACK_ALL;
    policies.HardLink      = CF_HARDLINK_POLICY_NONE;
    policies.PlaceholderManagement = CF_PLACEHOLDER_MANAGEMENT_POLICY_DEFAULT;

    HRESULT hr = CfRegisterSyncRoot(
        m_syncPathW.c_str(),
        &reg,
        &policies,
        CF_REGISTER_FLAG_UPDATE   // idempotent — safe to call on every launch
    );

    if (FAILED(hr)) {
        std::cerr << "[CFProvider] CfRegisterSyncRoot failed: 0x"
                  << std::hex << hr << "\n";
        return false;
    }

    std::cout << "[CFProvider] Sync root registered: " << m_syncPath << "\n";
    return true;
}

bool CloudFilesProvider::unregisterSyncRoot() {
    HRESULT hr = CfUnregisterSyncRoot(m_syncPathW.c_str());
    if (FAILED(hr)) {
        std::cerr << "[CFProvider] CfUnregisterSyncRoot failed: 0x"
                  << std::hex << hr << "\n";
        return false;
    }
    std::cout << "[CFProvider] Sync root unregistered.\n";
    return true;
}

bool CloudFilesProvider::isSyncRootRegistered() const {
    // Try to query the sync root info — succeeds only if registered
    CF_SYNC_ROOT_BASIC_INFO info = {};
    DWORD returned = 0;
    HRESULT hr = CfGetSyncRootInfoByPath(
        m_syncPathW.c_str(),
        CF_SYNC_ROOT_INFO_BASIC,
        &info,
        sizeof(info),
        &returned);
    return SUCCEEDED(hr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime connection
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::start() {
    if (m_connected.load()) return true;

    // Register the sync root if not already done
    if (!isSyncRootRegistered()) {
        if (!registerSyncRoot()) return false;
    }

    // CF_CALLBACK_REGISTRATION array — terminated by CF_CALLBACK_REGISTRATION_END
    // We pass `this` as CallbackContext so static callbacks can reach our members.
    CF_CALLBACK_REGISTRATION callbacks[] = {
        { CF_CALLBACK_TYPE_FETCH_DATA,              onFetchData         },
        { CF_CALLBACK_TYPE_CANCEL_FETCH_DATA,        onCancelFetchData   },
        { CF_CALLBACK_TYPE_FETCH_PLACEHOLDERS,       onFetchPlaceholders },
        { CF_CALLBACK_TYPE_NOTIFY_FILE_OPEN_COMPLETION,  onNotifyFileOpened  },
        { CF_CALLBACK_TYPE_NOTIFY_FILE_CLOSE_COMPLETION, onNotifyFileClosed  },
        CF_CALLBACK_REGISTRATION_END
    };

    HRESULT hr = CfConnectSyncRoot(
        m_syncPathW.c_str(),
        callbacks,
        this,                          // CallbackContext — passed back in CF_CALLBACK_INFO
        CF_CONNECT_FLAG_REQUIRE_PROCESS_INFO |
        CF_CONNECT_FLAG_REQUIRE_FULL_FILE_PATH,
        &m_connectionKey
    );

    if (FAILED(hr)) {
        std::cerr << "[CFProvider] CfConnectSyncRoot failed: 0x"
                  << std::hex << hr << "\n";
        return false;
    }

    m_connected.store(true);
    std::cout << "[CFProvider] Connected to sync root.\n";
    return true;
}

void CloudFilesProvider::stop() {
    if (!m_connected.exchange(false)) return;

    HRESULT hr = CfDisconnectSyncRoot(m_connectionKey);
    if (FAILED(hr)) {
        std::cerr << "[CFProvider] CfDisconnectSyncRoot failed: 0x"
                  << std::hex << hr << "\n";
    } else {
        std::cout << "[CFProvider] Disconnected from sync root.\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Placeholder creation helpers
// ─────────────────────────────────────────────────────────────────────────────

// CF API stores an opaque "file identity" blob on each placeholder.
// We store the file's UUID (std::string) so FETCH_DATA can identify what
// to download without touching the filesystem or database.
struct PlaceholderIdentity {
    char uuid[128];  // null-terminated UTF-8 UUID string
};

CF_PLACEHOLDER_CREATE_INFO CloudFilesProvider::buildFilePlaceholderInfo(
    const CloudFileMetadata &file) const
{
    CF_PLACEHOLDER_CREATE_INFO info = {};
    info.RelativeFileName = toWide(file.filename).c_str();  // NOTE: see comment below

    // Metadata — shown in Explorer without downloading
    info.FsMetadata.FileSize.QuadPart = file.size;

    // Convert timestamps
    if (!file.last_modified.empty()) {
        try {
            int64_t ts = std::stoll(file.last_modified);
            LARGE_INTEGER li = unixToLargeIntFileTime(ts);
            info.FsMetadata.BasicInfo.LastWriteTime   = li;
            info.FsMetadata.BasicInfo.CreationTime    = li;
            info.FsMetadata.BasicInfo.LastAccessTime  = li;
            info.FsMetadata.BasicInfo.ChangeTime      = li;
        } catch (...) {}
    }

    info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;

    // Store UUID as identity blob so FETCH_DATA knows what to download
    static PlaceholderIdentity identity;  // NOTE: not thread-safe for concurrent creates — see below
    memset(&identity, 0, sizeof(identity));
    strncpy_s(identity.uuid, file.uuid.c_str(), sizeof(identity.uuid) - 1);
    info.FileIdentity       = &identity;
    info.FileIdentityLength = sizeof(identity);

    return info;
}

CF_PLACEHOLDER_CREATE_INFO CloudFilesProvider::buildDirPlaceholderInfo(
    const std::string &relPath,
    const std::string &uuid,
    const std::string &created_at) const
{
    CF_PLACEHOLDER_CREATE_INFO info = {};
    std::string dirName = fs::path(relPath).filename().string();
    info.RelativeFileName = toWide(dirName).c_str();

    if (!created_at.empty()) {
        try {
            int64_t ts = std::stoll(created_at);
            LARGE_INTEGER li = unixToLargeIntFileTime(ts);
            info.FsMetadata.BasicInfo.CreationTime   = li;
            info.FsMetadata.BasicInfo.LastWriteTime  = li;
            info.FsMetadata.BasicInfo.LastAccessTime = li;
            info.FsMetadata.BasicInfo.ChangeTime     = li;
        } catch (...) {}
    }
    info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    info.FsMetadata.FileSize.QuadPart = 0;
    info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;

    static PlaceholderIdentity dirIdentity;
    memset(&dirIdentity, 0, sizeof(dirIdentity));
    strncpy_s(dirIdentity.uuid, uuid.c_str(), sizeof(dirIdentity.uuid) - 1);
    info.FileIdentity       = &dirIdentity;
    info.FileIdentityLength = sizeof(dirIdentity);

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public placeholder creation
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::createFilePlaceholder(const CloudFileMetadata &file) {
    // Resolve the parent directory absolute path
    std::wstring parentDirW;
    if (file.path == "/") {
        parentDirW = m_syncPathW;
    } else {
        parentDirW = m_syncPathW + toWide(file.path);
    }

    // Ensure parent directory exists as a real or placeholder directory
    if (!fs::exists(toNarrow(parentDirW))) {
        std::cerr << "[CFProvider] Parent dir missing for: "
                  << file.filename << " at " << file.path << "\n";
        return false;
    }

    // Build identity blob — use a local copy per-call to be thread-safe
    PlaceholderIdentity identity = {};
    strncpy_s(identity.uuid, file.uuid.c_str(), sizeof(identity.uuid) - 1);

    CF_PLACEHOLDER_CREATE_INFO info = {};

    // RelativeFileName must stay alive for the duration of CfCreatePlaceholders.
    // We use a local wstring to guarantee lifetime.
    std::wstring filenameW = toWide(file.filename);
    info.RelativeFileName = filenameW.c_str();

    info.FsMetadata.FileSize.QuadPart = file.size;
    if (!file.last_modified.empty()) {
        try {
            int64_t ts = std::stoll(file.last_modified);
            LARGE_INTEGER li = unixToLargeIntFileTime(ts);
            info.FsMetadata.BasicInfo.LastWriteTime   = li;
            info.FsMetadata.BasicInfo.CreationTime    = li;
            info.FsMetadata.BasicInfo.LastAccessTime  = li;
            info.FsMetadata.BasicInfo.ChangeTime      = li;
        } catch (...) {}
    }
    info.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    info.Flags          = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
    info.FileIdentity   = &identity;
    info.FileIdentityLength = sizeof(identity);

    // Tell SyncWorker to ignore the Added event this will generate
    std::string absPath = (file.path == "/")
        ? m_syncPath + "/" + file.filename
        : m_syncPath + file.path + "/" + file.filename;
    m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Added);

    DWORD entriesProcessed = 0;
    HRESULT hr = CfCreatePlaceholders(
        parentDirW.c_str(),
        &info,
        1,
        CF_CREATE_FLAG_NONE,
        &entriesProcessed);

    if (FAILED(hr) || entriesProcessed == 0) {
        m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Added);
        std::cerr << "[CFProvider] CfCreatePlaceholders failed for "
                  << file.filename << ": 0x" << std::hex << hr << "\n";
        return false;
    }

    std::cout << "[CFProvider] Placeholder created: " << file.path
              << "/" << file.filename << "\n";
    return true;
}

bool CloudFilesProvider::createDirPlaceholder(
    const LocalFolderCreateMetadata &dir)
{
    std::string parentPath = fs::path(dir.path).parent_path().generic_string();
    if (parentPath.empty()) parentPath = "/";

    std::wstring parentDirW = (parentPath == "/")
        ? m_syncPathW
        : m_syncPathW + toWide(parentPath);

    std::string dirName = fs::path(dir.path).filename().string();
    std::wstring dirNameW = toWide(dirName);

    PlaceholderIdentity identity = {};
    strncpy_s(identity.uuid, dir.uuid.c_str(), sizeof(identity.uuid) - 1);

    CF_PLACEHOLDER_CREATE_INFO info = {};
    info.RelativeFileName = dirNameW.c_str();

    if (!dir.created_at.empty()) {
        try {
            int64_t ts = std::stoll(dir.created_at);
            LARGE_INTEGER li = unixToLargeIntFileTime(ts);
            info.FsMetadata.BasicInfo.CreationTime   = li;
            info.FsMetadata.BasicInfo.LastWriteTime  = li;
            info.FsMetadata.BasicInfo.LastAccessTime = li;
            info.FsMetadata.BasicInfo.ChangeTime     = li;
        } catch (...) {}
    }
    info.FsMetadata.BasicInfo.FileAttributes  = FILE_ATTRIBUTE_DIRECTORY;
    info.FsMetadata.FileSize.QuadPart = 0;
    info.Flags           = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
    info.FileIdentity    = &identity;
    info.FileIdentityLength = sizeof(identity);

    m_syncWorker.addIgnoreEvent(dir.absPath, WatchEvent::Added);

    DWORD entriesProcessed = 0;
    HRESULT hr = CfCreatePlaceholders(
        parentDirW.c_str(),
        &info,
        1,
        CF_CREATE_FLAG_NONE,
        &entriesProcessed);

    if (FAILED(hr) || entriesProcessed == 0) {
        m_syncWorker.removeIgnoreEvent(dir.absPath, WatchEvent::Added);
        std::cerr << "[CFProvider] CfCreatePlaceholders (dir) failed for "
                  << dir.path << ": 0x" << std::hex << hr << "\n";
        return false;
    }

    std::cout << "[CFProvider] Dir placeholder created: " << dir.path << "\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// In-sync marking and dehydration
// ─────────────────────────────────────────────────────────────────────────────

bool CloudFilesProvider::markInSync(const std::wstring &absPath) {
    HANDLE hFile = CreateFileW(
        absPath.c_str(),
        WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,   // required to open directories
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return false;

    CF_SET_IN_SYNC_FLAGS flags = CF_SET_IN_SYNC_FLAG_NONE;
    HRESULT hr = CfSetInSyncState(hFile, CF_IN_SYNC_STATE_IN_SYNC, flags, nullptr);
    CloseHandle(hFile);
    return SUCCEEDED(hr);
}

bool CloudFilesProvider::markNotInSync(const std::wstring &absPath) {
    HANDLE hFile = CreateFileW(
        absPath.c_str(),
        WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return false;

    HRESULT hr = CfSetInSyncState(hFile, CF_IN_SYNC_STATE_NOT_IN_SYNC,
                                   CF_SET_IN_SYNC_FLAG_NONE, nullptr);
    CloseHandle(hFile);
    return SUCCEEDED(hr);
}

bool CloudFilesProvider::dehydrateFile(const std::wstring &absPath) {
    HANDLE hFile = CreateFileW(
        absPath.c_str(),
        WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "[CFProvider] dehydrateFile: cannot open file\n";
        return false;
    }

    // Dehydrate the entire file: offset 0, length -1 means "full file"
    LARGE_INTEGER startOffset = {};
    startOffset.QuadPart = 0;
    LARGE_INTEGER length = {};
    length.QuadPart = -1;   // -1 means full file
    HRESULT hr = CfDehydratePlaceholder(
        hFile,
        startOffset,
        length,
        CF_DEHYDRATE_FLAG_BACKGROUND,
        nullptr);

    CloseHandle(hFile);

    if (FAILED(hr)) {
        std::cerr << "[CFProvider] CfDehydratePlaceholder failed: 0x"
                  << std::hex << hr << "\n";
        return false;
    }
    return true;
}

bool CloudFilesProvider::hydrateFile(const std::wstring &absPath) {
    HANDLE hFile = CreateFileW(
        absPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return false;

    // Simply opening the file with GENERIC_READ triggers hydration via FETCH_DATA.
    // We wait for it to complete by reading one byte.
    char buf;
    DWORD bytesRead = 0;
    ReadFile(hFile, &buf, 1, &bytesRead, nullptr);
    CloseHandle(hFile);
    return true;
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

void CALLBACK CloudFilesProvider::onFetchData(
    const CF_CALLBACK_INFO       *info,
    const CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);

    // Recover UUID from the identity blob we stored when creating the placeholder
    const auto *identity =
        static_cast<const PlaceholderIdentity *>(info->FileIdentity);
    std::string uuid(identity->uuid);

    // Reconstruct the relative path from NormalizedPath
    // NormalizedPath is the absolute path to the file in the sync root.
    std::string absPath = toNarrow(info->NormalizedPath);
    std::string relPath = absPath.substr(self->m_syncPath.size());
    // Normalize separators
    std::replace(relPath.begin(), relPath.end(), '\\', '/');

    std::string filename = fs::path(absPath).filename().string();
    std::string dirPath  = fs::path(relPath).parent_path().generic_string();
    if (dirPath.empty()) dirPath = "/";

    std::cout << "[CFProvider] FETCH_DATA: " << relPath
              << " offset=" << params->FetchData.RequiredFileOffset.QuadPart
              << " len=" << params->FetchData.RequiredLength.QuadPart << "\n";

    // Look up the CloudFileMetadata from our database by uuid
    // We need the full metadata to call ApiClient::downloadFile()
    std::optional<FileMetadata> dbFile;
    {
        std::lock_guard<std::recursive_mutex> lock(self->m_dbManager.getSyncMutex());
        dbFile = self->m_dbManager.getFileByPath(dirPath, filename);
    }

    if (!dbFile.has_value()) {
        std::cerr << "[CFProvider] FETCH_DATA: file not found in DB: "
                  << relPath << "\n";
        // Report failure to CF kernel so the open call returns an error
        // rather than blocking forever
        CF_OPERATION_INFO opInfo    = {};
        CF_OPERATION_PARAMETERS opP = {};
        opInfo.StructSize    = sizeof(opInfo);
        opInfo.Type          = CF_OPERATION_TYPE_TRANSFER_DATA;
        opInfo.ConnectionKey = info->ConnectionKey;
        opInfo.TransferKey   = info->TransferKey;
        opP.ParamSize        = sizeof(opP.TransferData);
                opP.TransferData.Offset         = params->FetchData.RequiredFileOffset;
        opP.TransferData.Buffer         = nullptr;
        opP.TransferData.Length.QuadPart = 0;
        opP.TransferData.Flags          = CF_OPERATION_TRANSFER_DATA_FLAG_NONE;
        CfExecute(&opInfo, &opP);
        return;
    }

    // Register SyncWorker ignore so the write we are about to do does not
    // trigger an upload event
    self->m_syncWorker.addIgnoreEvent(absPath, WatchEvent::Modified);

    // Build a CloudFileMetadata from what we have in the DB
    CloudFileMetadata cloudFile;
    cloudFile.uuid          = dbFile->uuid;
    cloudFile.filename      = dbFile->filename;
    cloudFile.path          = dbFile->path;
    cloudFile.size          = dbFile->size;
    cloudFile.hashvalue     = dbFile->hashvalue;
    cloudFile.last_modified = dbFile->last_modified;
    cloudFile.dirID         = dbFile->dirID;
    cloudFile.origin        = dbFile->origin;
    cloudFile.versions      = dbFile->versions;

    // Add activity entry so the UI shows a download spinner
    auto activity = self->m_activityStore.getActivity(uuid);
    if (!activity.has_value()) {
        SyncItem item;
        item.id       = uuid;
        item.name     = filename;
        item.path     = relPath;
        item.type     = activityToString(ActivityStatus::DOWNLOAD);
        item.meta     = "file";
        item.inQueue  = false;
        item.isActive = true;
        self->m_activityStore.addActivity(uuid, item);
    }

    // Download the full file to a temp path, then transfer bytes to CF kernel.
    // We download to a temp file rather than directly to absPath because CF API
    // requires us to feed bytes via CfExecute, not write directly to the file.
    std::string tempPath = absPath + ".cftmp";
    self->m_syncWorker.addIgnoreEvent(tempPath, WatchEvent::Added);

    bool downloaded = self->m_apiClient.downloadFile(
        cloudFile,
        tempPath,
        [self, uuid](const std::string &key, double progress) {
            auto it = self->m_activityStore.getActivity(uuid);
            if (it.has_value()) {
                it->progress = progress;
                it->isActive = true;
                it->isDone   = progress >= 100.0;
                self->m_activityStore.updateActivity(uuid, it.value());
            }
        });

    if (!downloaded) {
        std::cerr << "[CFProvider] FETCH_DATA: download failed for " << filename << "\n";
        self->m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Modified);
        self->m_syncWorker.removeIgnoreEvent(tempPath, WatchEvent::Added);
        fs::remove(tempPath);

        // Report error to CF
        CF_OPERATION_INFO opInfo    = {};
        CF_OPERATION_PARAMETERS opP = {};
        opInfo.StructSize    = sizeof(opInfo);
        opInfo.Type          = CF_OPERATION_TYPE_TRANSFER_DATA;
        opInfo.ConnectionKey = info->ConnectionKey;
        opInfo.TransferKey   = info->TransferKey;
        opP.ParamSize        = sizeof(opP.TransferData);
                opP.TransferData.Offset         = params->FetchData.RequiredFileOffset;
        opP.TransferData.Buffer         = nullptr;
        opP.TransferData.Length.QuadPart = 0;
        opP.TransferData.Flags          = CF_OPERATION_TRANSFER_DATA_FLAG_NONE;
        CfExecute(&opInfo, &opP);

        auto it = self->m_activityStore.getActivity(uuid);
        if (it.has_value()) {
            it->isActive = false;
            it->isError  = true;
            self->m_activityStore.updateActivity(uuid, it.value());
        }
        return;
    }

    // Read the downloaded temp file and feed it to CF in chunks
    std::ifstream ifs(tempPath, std::ios::binary);
    if (!ifs) {
        std::cerr << "[CFProvider] FETCH_DATA: cannot open temp file\n";
        self->m_syncWorker.removeIgnoreEvent(absPath, WatchEvent::Modified);
        fs::remove(tempPath);
        return;
    }

    constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 MB chunks
    std::vector<char> buffer(CHUNK_SIZE);
    LARGE_INTEGER offset;
    offset.QuadPart = 0;

    bool transferOk = true;
    while (ifs.read(buffer.data(), CHUNK_SIZE) || ifs.gcount() > 0) {
        std::streamsize bytesRead = ifs.gcount();

        CF_OPERATION_INFO opInfo    = {};
        CF_OPERATION_PARAMETERS opP = {};
        opInfo.StructSize    = sizeof(opInfo);
        opInfo.Type          = CF_OPERATION_TYPE_TRANSFER_DATA;
        opInfo.ConnectionKey = info->ConnectionKey;
        opInfo.TransferKey   = info->TransferKey;

        opP.ParamSize                     = sizeof(opP.TransferData);
        opP.TransferData.Flags            = CF_OPERATION_TRANSFER_DATA_FLAG_NONE;
        opP.TransferData.Buffer           = buffer.data();
        opP.TransferData.Offset           = offset;
        opP.TransferData.Length.QuadPart  = static_cast<LONGLONG>(bytesRead);
        
        HRESULT hr = CfExecute(&opInfo, &opP);
        if (FAILED(hr)) {
            std::cerr << "[CFProvider] CfExecute TransferData failed: 0x"
                      << std::hex << hr << "\n";
            transferOk = false;
            break;
        }
        offset.QuadPart += bytesRead;
    }
    ifs.close();

    // Clean up temp file — ignore the delete event it generates
    self->m_syncWorker.addIgnoreEvent(tempPath, WatchEvent::Deleted);
    fs::remove(tempPath);

    if (transferOk) {
        // Mark the file as in-sync so Explorer shows the green checkmark
        self->markInSync(info->NormalizedPath);
        std::cout << "[CFProvider] FETCH_DATA complete: " << filename << "\n";

        auto it = self->m_activityStore.getActivity(uuid);
        if (it.has_value()) {
            it->isActive = false;
            it->isDone   = true;
            it->progress = 100.0;
            self->m_activityStore.updateActivity(uuid, it.value());
        }
    }
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
    const CF_CALLBACK_INFO       *info,
    const CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);
    const auto *identity =
        static_cast<const PlaceholderIdentity *>(info->FileIdentity);
    std::cout << "[CFProvider] CANCEL_FETCH_DATA: " << identity->uuid << "\n";

    // Mark as error in activity store so UI reflects the cancellation
    std::string uuid(identity->uuid);
    auto it = self->m_activityStore.getActivity(uuid);
    if (it.has_value()) {
        it->isActive = false;
        it->isError  = true;
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
    const CF_CALLBACK_INFO       *info,
    const CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesProvider *>(info->CallbackContext);

    // NormalizedPath is the absolute path of the directory being opened
    std::string absDirPath = toNarrow(info->NormalizedPath);
    std::string relDirPath = absDirPath.substr(self->m_syncPath.size());
    std::replace(relDirPath.begin(), relDirPath.end(), '\\', '/');
    if (relDirPath.empty()) relDirPath = "/";

    std::cout << "[CFProvider] FETCH_PLACEHOLDERS: " << relDirPath << "\n";

    // Ask the cloud API for the directory contents
    auto result = self->m_apiClient.getDirectoryContents(relDirPath);
    if (!result.has_value()) {
        std::cerr << "[CFProvider] FETCH_PLACEHOLDERS: API call failed for "
                  << relDirPath << "\n";

        // Report empty population so Windows does not hang
        CF_OPERATION_INFO opInfo    = {};
        CF_OPERATION_PARAMETERS opP = {};
        opInfo.StructSize    = sizeof(opInfo);
        opInfo.Type          = CF_OPERATION_TYPE_TRANSFER_PLACEHOLDERS;
        opInfo.ConnectionKey = info->ConnectionKey;
        opInfo.TransferKey   = info->TransferKey;
        opP.ParamSize                             = sizeof(opP.TransferPlaceholders);
        opP.TransferPlaceholders.Flags            = CF_OPERATION_TRANSFER_PLACEHOLDERS_FLAG_NONE;
        opP.TransferPlaceholders.PlaceholderCount = 0;
        opP.TransferPlaceholders.PlaceholderArray = nullptr;
        CfExecute(&opInfo, &opP);
        return;
    }

    // Build placeholder array from cloud items
    // We need to keep wstring name buffers alive for the duration of CfExecute
    struct PlaceholderEntry {
        std::wstring           nameW;
        PlaceholderIdentity    identity;
        CF_PLACEHOLDER_CREATE_INFO info;
    };

    std::vector<PlaceholderEntry> entries;
    entries.reserve(result->items.size());

    for (const auto &item : result->items) {
        PlaceholderEntry entry;
        entry.nameW = item.name.toStdWString();
        memset(&entry.identity, 0, sizeof(entry.identity));
        strncpy_s(entry.identity.uuid,
                  item.id.toStdString().c_str(),
                  sizeof(entry.identity.uuid) - 1);

        CF_PLACEHOLDER_CREATE_INFO &ph = entry.info;
        ph = {};
        ph.RelativeFileName   = entry.nameW.c_str();
        ph.FileIdentity       = &entry.identity;
        ph.FileIdentityLength = sizeof(PlaceholderIdentity);

        bool isDir = (item.type == "folder");
        if (isDir) {
            ph.FsMetadata.BasicInfo.FileAttributes  = FILE_ATTRIBUTE_DIRECTORY;
            ph.FsMetadata.FileSize.QuadPart = 0;
        } else {
            ph.FsMetadata.BasicInfo.FileAttributes  = FILE_ATTRIBUTE_NORMAL;
            try {
                ph.FsMetadata.FileSize.QuadPart =
                    item.size.toLongLong();
            } catch (...) {}
        }

        // Set timestamps if available
        if (!item.lastModified.isEmpty()) {
            try {
                int64_t ts = item.lastModified.toLongLong();
                LARGE_INTEGER li = unixToLargeIntFileTime(ts);
                ph.FsMetadata.BasicInfo.LastWriteTime   = li;
                ph.FsMetadata.BasicInfo.CreationTime    = li;
                ph.FsMetadata.BasicInfo.LastAccessTime  = li;
                ph.FsMetadata.BasicInfo.ChangeTime      = li;
            } catch (...) {}
        }

        ph.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
        entries.push_back(std::move(entry));
    }

    // Build raw pointer array for CfExecute
    std::vector<CF_PLACEHOLDER_CREATE_INFO> rawInfos;
    rawInfos.reserve(entries.size());
    for (auto &e : entries) rawInfos.push_back(e.info);

    // Transfer placeholders to the kernel in one shot
    CF_OPERATION_INFO opInfo    = {};
    CF_OPERATION_PARAMETERS opP = {};
    opInfo.StructSize    = sizeof(opInfo);
    opInfo.Type          = CF_OPERATION_TYPE_TRANSFER_PLACEHOLDERS;
    opInfo.ConnectionKey = info->ConnectionKey;
    opInfo.TransferKey   = info->TransferKey;

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
        std::cout << "[CFProvider] FETCH_PLACEHOLDERS: created "
                  << rawInfos.size() << " entries for " << relDirPath << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CF Callbacks: file open / close notifications
// ─────────────────────────────────────────────────────────────────────────────

void CALLBACK CloudFilesProvider::onNotifyFileOpened(
    const CF_CALLBACK_INFO       *info,
    const CF_CALLBACK_PARAMETERS *params)
{
    // Informational — log which file was opened. We could use this to
    // pre-fetch the next N files in a sequence (read-ahead).
    std::cout << "[CFProvider] File opened: "
              << toNarrow(info->NormalizedPath) << "\n";
}

void CALLBACK CloudFilesProvider::onNotifyFileClosed(
    const CF_CALLBACK_INFO       *info,
    const CF_CALLBACK_PARAMETERS *params)
{
    // If the file was modified we should trigger an upload.
    // The FilesystemWatcher will already detect the modification via efsw
    // and enqueue it through SyncWorker::handleModified — so we just log here.
    std::cout << "[CFProvider] File closed: "
              << toNarrow(info->NormalizedPath) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: timestamp conversion
// ─────────────────────────────────────────────────────────────────────────────

LARGE_INTEGER CloudFilesProvider::unixToLargeIntFileTime(int64_t unixSeconds) {
    // FILETIME epoch is Jan 1 1601; Unix epoch is Jan 1 1970.
    // Difference in 100-nanosecond intervals:
    constexpr int64_t EPOCH_DIFF = 116444736000000000LL;
    LARGE_INTEGER li;
    li.QuadPart = (unixSeconds * 10000000LL) + EPOCH_DIFF;
    return li;
}

FILETIME CloudFilesProvider::unixStringToFileTime(
    const std::string &unixSeconds)
{
    FILETIME ft = {};
    try {
        int64_t ts = std::stoll(unixSeconds);
        LARGE_INTEGER li = unixToLargeIntFileTime(ts);
        ft.dwLowDateTime  = li.LowPart;
        ft.dwHighDateTime = li.HighPart;
    } catch (...) {}
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
    g.Data3 = static_cast<uint16_t>(h >>  8) | 0x4000u; // version 4
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
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                   static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                        static_cast<int>(s.size()), result.data(), len);
    return result;
}

std::string CloudFilesProvider::toNarrow(const std::wstring &w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                   static_cast<int>(w.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                        static_cast<int>(w.size()),
                        result.data(), len, nullptr, nullptr);
    return result;
}

} // namespace sync_app

#endif // _WIN32
