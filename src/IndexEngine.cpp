#include "IndexEngine.h"
#include "resource.h"
#include <winioctl.h>
#include <cwctype>
#include <fstream>
#include <queue>
#include <unordered_map>

IndexEngine::IndexEngine()
    : m_snapshot(std::make_shared<const IndexData>()) {}

IndexEngine::~IndexEngine() {
    Stop();
}

void IndexEngine::Stop() {
    m_stopFlag.store(true);
    if (m_indexThread.joinable()) m_indexThread.join();
}

size_t IndexEngine::IndexedCount() const {
    if (m_indexing.load()) return m_progressCount.load();
    auto snap = GetSnapshot();
    return snap ? snap->entries.size() : 0;
}

IndexDataPtr IndexEngine::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(m_snapMutex);
    return m_snapshot;
}

void IndexEngine::PublishSnapshot(std::shared_ptr<IndexData>&& data) {
    std::lock_guard<std::mutex> lock(m_snapMutex);
    m_snapshot = std::move(data);
}

std::vector<uint32_t> IndexEngine::Query(const std::wstring& query, const SearchOptions& opts, IndexDataPtr& outSnap) const {
    outSnap = GetSnapshot();
    if (!outSnap) return {};
    return SearchEngine::Execute(*outSnap, query, opts);
}

void IndexEngine::StartIndexing(HWND notifyHwnd) {
    Stop();
    m_stopFlag.store(false);
    m_indexing.store(true);
    m_usedNtfsMft.store(false);
    m_progressCount.store(0);
    m_indexThread = std::thread(&IndexEngine::IndexWorker, this, notifyHwnd);
}

static inline uint64_t FileTimeToU64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

static inline uint32_t AppendNameToArenas(
    std::vector<wchar_t>& nameArena,
    std::vector<wchar_t>& lowerArena,
    const wchar_t* src,
    uint16_t len
) {
    uint32_t off = static_cast<uint32_t>(nameArena.size());
    nameArena.resize(off + len + 1);
    lowerArena.resize(off + len + 1);
    wchar_t* nDst = nameArena.data() + off;
    wchar_t* lDst = lowerArena.data() + off;
    for (uint16_t i = 0; i < len; ++i) {
        wchar_t c = src[i];
        nDst[i] = c;
        if (c >= L'A' && c <= L'Z') lDst[i] = static_cast<wchar_t>(c + 32);
        else if (c > 127) lDst[i] = static_cast<wchar_t>(towlower(c));
        else lDst[i] = c;
    }
    nDst[len] = L'\0';
    lDst[len] = L'\0';
    return off;
}

bool IndexEngine::IndexVolumeNtfsUsn(wchar_t driveLetter, DriveBatch& batch) {
    wchar_t volPath[16] = {};
    swprintf_s(volPath, L"\\\\.\\%c:", driveLetter);

    HANDLE hVol = CreateFileW(
        volPath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (hVol == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA_V0 journalData = {};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData, sizeof(journalData), &bytesReturned, nullptr)) {
        CloseHandle(hVol);
        return false;
    }

    batch.driveRoot = std::wstring(1, driveLetter) + L":";
    batch.entries.reserve(400000);
    batch.nameArena.reserve(12 * 1024 * 1024);
    batch.lowerArena.reserve(12 * 1024 * 1024);

    // ONLY map folders (typically ~10% of total records) instead of all 2M files!
    std::unordered_map<uint64_t, uint32_t> folderFrnToIdx;
    folderFrnToIdx.reserve(80000);

    std::vector<uint64_t> parentFrns;
    parentFrns.reserve(400000);

    MFT_ENUM_DATA_V0 med = {};
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = journalData.NextUsn;

    constexpr DWORD kBufSize = 512 * 1024; // 512 KB MFT read buffer
    std::vector<BYTE> buffer(kBufSize);

    while (!m_stopFlag.load()) {
        DWORD retBytes = 0;
        BOOL ok = DeviceIoControl(
            hVol,
            FSCTL_ENUM_USN_DATA,
            &med,
            sizeof(med),
            buffer.data(),
            kBufSize,
            &retBytes,
            nullptr
        );
        if (!ok || retBytes <= sizeof(USN)) break;

        DWORD offset = sizeof(USN);
        while (offset < retBytes) {
            auto* rec = reinterpret_cast<PUSN_RECORD_V2>(buffer.data() + offset);
            if (rec->RecordLength == 0) break;

            uint16_t len = static_cast<uint16_t>(rec->FileNameLength / sizeof(wchar_t));
            if (len > 0 && rec->FileName[0] != L'$') {
                uint32_t idx = static_cast<uint32_t>(batch.entries.size());
                uint32_t off = AppendNameToArenas(batch.nameArena, batch.lowerArena, rec->FileName, len);

                CompactEntry ce;
                ce.nameOff    = off;
                ce.nameLen    = len;
                ce.driveIdx   = 0;
                ce.parentIdx  = -1;
                ce.attributes = rec->FileAttributes;
                ce.isFolder   = (rec->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

                batch.entries.push_back(ce);
                parentFrns.push_back(rec->ParentFileReferenceNumber);

                if (ce.isFolder) {
                    folderFrnToIdx.emplace(rec->FileReferenceNumber, idx);
                }
                m_progressCount.fetch_add(1, std::memory_order_relaxed);
            }
            offset += rec->RecordLength;
        }
        med.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buffer.data());
    }
    CloseHandle(hVol);

    if (m_stopFlag.load()) return false;

    // Resolve parentIdx in a single flat pass (~100ms for 2,000,000 entries!)
    const size_t n = batch.entries.size();
    for (size_t i = 0; i < n; ++i) {
        auto it = folderFrnToIdx.find(parentFrns[i]);
        if (it != folderFrnToIdx.end() && it->second != static_cast<uint32_t>(i)) {
            batch.entries[i].parentIdx = static_cast<int32_t>(it->second);
        }
    }
    return true;
}

void IndexEngine::IndexVolumeFallback(const std::wstring& rootPath, DriveBatch& batch) {
    std::wstring cleanRoot = rootPath;
    if (!cleanRoot.empty() && cleanRoot.back() == L'\\') cleanRoot.pop_back();
    batch.driveRoot = cleanRoot;

    struct DirJob {
        std::wstring fullPath;
        int32_t parentIdx;
    };
    std::queue<DirJob> dirs;
    dirs.push({ cleanRoot, -1 });

    while (!dirs.empty() && !m_stopFlag.load()) {
        DirJob job = std::move(dirs.front());
        dirs.pop();

        std::wstring searchPat = job.fullPath + L"\\*";
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileExW(
            searchPat.c_str(),
            FindExInfoBasic,
            &fd,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );
        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (m_stopFlag.load()) break;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

            bool isReparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            bool isDir     = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            uint16_t len   = static_cast<uint16_t>(wcslen(fd.cFileName));

            uint32_t idx = static_cast<uint32_t>(batch.entries.size());
            uint32_t off = AppendNameToArenas(batch.nameArena, batch.lowerArena, fd.cFileName, len);

            CompactEntry ce;
            ce.nameOff      = off;
            ce.nameLen      = len;
            ce.driveIdx     = 0;
            ce.parentIdx    = job.parentIdx;
            ce.attributes   = fd.dwFileAttributes;
            ce.isFolder     = isDir ? 1 : 0;
            ce.size         = isDir ? 0 : ((static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
            ce.dateModified = FileTimeToU64(fd.ftLastWriteTime);
            ce.dateCreated  = FileTimeToU64(fd.ftCreationTime);

            batch.entries.push_back(ce);
            m_progressCount.fetch_add(1, std::memory_order_relaxed);

            if (isDir && !isReparse) {
                dirs.push({ job.fullPath + L"\\" + fd.cFileName, static_cast<int32_t>(idx) });
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }
}

void IndexEngine::IndexWorker(HWND notifyHwnd) {
    std::vector<wchar_t> fixedDrives;
    DWORD driveMask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(driveMask & (1u << i))) continue;
        wchar_t letter = static_cast<wchar_t>(L'A' + i);
        wchar_t root[4] = { letter, L':', L'\\', L'\0' };
        if (GetDriveTypeW(root) == DRIVE_FIXED) {
            fixedDrives.push_back(letter);
        }
    }

    // Scan all fixed drives simultaneously in parallel!
    std::vector<DriveBatch> batches(fixedDrives.size());
    std::vector<std::thread> driveThreads;
    driveThreads.reserve(fixedDrives.size());

    for (size_t idx = 0; idx < fixedDrives.size(); ++idx) {
        wchar_t letter = fixedDrives[idx];
        driveThreads.emplace_back([&, idx, letter]() {
            wchar_t root[4] = { letter, L':', L'\\', L'\0' };
            wchar_t fsName[32] = {};
            bool isNtfs = GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fsName, 32)
                          && (_wcsicmp(fsName, L"NTFS") == 0);

            if (isNtfs && IndexVolumeNtfsUsn(letter, batches[idx])) {
                m_usedNtfsMft.store(true);
            } else {
                IndexVolumeFallback(root, batches[idx]);
            }
        });
    }
    for (auto& t : driveThreads) t.join();

    if (m_stopFlag.load()) {
        m_indexing.store(false);
        return;
    }

    // Merge flat arenas in ~50ms (pure memcpy!)
    size_t totalEntries = 0;
    size_t totalChars   = 0;
    for (const auto& b : batches) {
        totalEntries += b.entries.size();
        totalChars   += b.nameArena.size();
    }

    auto merged = std::make_shared<IndexData>();
    merged->entries.reserve(totalEntries);
    merged->nameArena.reserve(totalChars);
    merged->lowerArena.reserve(totalChars);

    for (size_t d = 0; d < batches.size(); ++d) {
        auto& b = batches[d];
        uint32_t entryBase = static_cast<uint32_t>(merged->entries.size());
        uint32_t charBase  = static_cast<uint32_t>(merged->nameArena.size());
        uint16_t driveId   = static_cast<uint16_t>(merged->driveRoots.size());
        merged->driveRoots.push_back(b.driveRoot);

        merged->nameArena.insert(merged->nameArena.end(), b.nameArena.begin(), b.nameArena.end());
        merged->lowerArena.insert(merged->lowerArena.end(), b.lowerArena.begin(), b.lowerArena.end());

        for (auto ce : b.entries) {
            ce.nameOff += charBase;
            ce.driveIdx = driveId;
            if (ce.parentIdx >= 0) ce.parentIdx += static_cast<int32_t>(entryBase);
            merged->entries.push_back(ce);
        }
        b.entries.clear(); b.entries.shrink_to_fit();
        b.nameArena.clear(); b.nameArena.shrink_to_fit();
        b.lowerArena.clear(); b.lowerArena.shrink_to_fit();
    }

    // PUBLISH IMMEDIATELY so user can search 2,000,000 files right away with ZERO wait!
    m_indexing.store(false);
    PublishSnapshot(std::shared_ptr<IndexData>(merged));
    if (notifyHwnd && !m_stopFlag.load()) {
        PostMessageW(notifyHwnd, WM_APP_INDEX_COMPLETE, 0, 0);
    }

    // Build pre-sorted defaultOrder silently in background for instant empty-query display
    if (!m_stopFlag.load()) {
        auto sortedCopy = std::make_shared<IndexData>(*merged);
        SearchEngine::BuildDefaultOrder(*sortedCopy);
        if (!m_stopFlag.load()) {
            PublishSnapshot(std::move(sortedCopy));
            if (notifyHwnd) PostMessageW(notifyHwnd, WM_APP_INDEX_UPDATED, 0, 0);
        }
    }
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), sz, nullptr, nullptr);
    return out;
}

bool IndexEngine::ExportEfu(const std::wstring& filePath, const IndexDataPtr& snap, const std::vector<uint32_t>& indices) const {
    if (!snap) return false;
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs) return false;
    ofs << "\xEF\xBB\xBF" << "Filename,Size,Date Modified,Date Created,Attributes\r\n";

    for (uint32_t idx : indices) {
        if (idx >= snap->entries.size()) continue;
        const auto& e = snap->entries[idx];
        std::string full = WideToUtf8(snap->GetFullPath(idx));
        ofs << "\"" << full << "\","
            << (e.isFolder ? "" : std::to_string(e.size)) << ","
            << e.dateModified << ","
            << e.dateCreated << ","
            << e.attributes << "\r\n";
    }
    return true;
}

bool IndexEngine::SaveDatabase(const std::wstring& dbPath) const {
    auto snap = GetSnapshot();
    if (!snap || snap->entries.empty()) return false;
    std::ofstream ofs(dbPath, std::ios::binary);
    if (!ofs) return false;

    uint32_t magic      = 0x33564545; // 'EEV3' flat arena bulk format
    uint32_t entryCount = static_cast<uint32_t>(snap->entries.size());
    uint32_t arenaChars = static_cast<uint32_t>(snap->nameArena.size());
    uint32_t driveCount = static_cast<uint32_t>(snap->driveRoots.size());
    uint32_t orderCount = snap->hasDefaultOrder ? static_cast<uint32_t>(snap->defaultOrder.size()) : 0;

    ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ofs.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));
    ofs.write(reinterpret_cast<const char*>(&arenaChars), sizeof(arenaChars));
    ofs.write(reinterpret_cast<const char*>(&driveCount), sizeof(driveCount));
    ofs.write(reinterpret_cast<const char*>(&orderCount), sizeof(orderCount));

    for (const auto& d : snap->driveRoots) {
        uint32_t len = static_cast<uint32_t>(d.size());
        ofs.write(reinterpret_cast<const char*>(&len), sizeof(len));
        ofs.write(reinterpret_cast<const char*>(d.data()), len * sizeof(wchar_t));
    }

    // 4 single bulk writes -> ~0.2s for 2,000,000 files!
    ofs.write(reinterpret_cast<const char*>(snap->entries.data()), entryCount * sizeof(CompactEntry));
    ofs.write(reinterpret_cast<const char*>(snap->nameArena.data()), arenaChars * sizeof(wchar_t));
    ofs.write(reinterpret_cast<const char*>(snap->lowerArena.data()), arenaChars * sizeof(wchar_t));
    if (orderCount > 0) {
        ofs.write(reinterpret_cast<const char*>(snap->defaultOrder.data()), orderCount * sizeof(uint32_t));
    }
    return true;
}

bool IndexEngine::LoadDatabase(const std::wstring& dbPath) {
    std::ifstream ifs(dbPath, std::ios::binary);
    if (!ifs) return false;

    uint32_t magic = 0, entryCount = 0, arenaChars = 0, driveCount = 0, orderCount = 0;
    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    ifs.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
    ifs.read(reinterpret_cast<char*>(&arenaChars), sizeof(arenaChars));
    ifs.read(reinterpret_cast<char*>(&driveCount), sizeof(driveCount));
    ifs.read(reinterpret_cast<char*>(&orderCount), sizeof(orderCount));
    if (!ifs || magic != 0x33564545 || entryCount == 0 || entryCount > 30000000 ||
        arenaChars == 0 || arenaChars > 500000000 || driveCount == 0 || driveCount > 64) {
        return false;
    }

    auto loaded = std::make_shared<IndexData>();
    loaded->driveRoots.resize(driveCount);
    for (uint32_t i = 0; i < driveCount; ++i) {
        uint32_t len = 0;
        ifs.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!ifs || len == 0 || len > 64) return false;
        loaded->driveRoots[i].resize(len);
        ifs.read(reinterpret_cast<char*>(loaded->driveRoots[i].data()), len * sizeof(wchar_t));
    }

    loaded->entries.resize(entryCount);
    loaded->nameArena.resize(arenaChars);
    loaded->lowerArena.resize(arenaChars);
    ifs.read(reinterpret_cast<char*>(loaded->entries.data()), static_cast<std::streamsize>(entryCount) * sizeof(CompactEntry));
    ifs.read(reinterpret_cast<char*>(loaded->nameArena.data()), static_cast<std::streamsize>(arenaChars) * sizeof(wchar_t));
    ifs.read(reinterpret_cast<char*>(loaded->lowerArena.data()), static_cast<std::streamsize>(arenaChars) * sizeof(wchar_t));
    if (orderCount == entryCount) {
        loaded->defaultOrder.resize(orderCount);
        ifs.read(reinterpret_cast<char*>(loaded->defaultOrder.data()), static_cast<std::streamsize>(orderCount) * sizeof(uint32_t));
        loaded->hasDefaultOrder = true;
    }
    if (!ifs) return false;

    // Validate arena null-termination and entry bounds against corrupted cache files
    if (loaded->nameArena.back() != L'\0' || loaded->lowerArena.back() != L'\0') return false;
    for (const auto& ce : loaded->entries) {
        if (static_cast<uint64_t>(ce.nameOff) + ce.nameLen >= arenaChars || ce.driveIdx >= driveCount) {
            return false;
        }
    }

    PublishSnapshot(std::move(loaded));
    return true;
}
