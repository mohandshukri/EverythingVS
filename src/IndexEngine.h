#pragma once
#include "SearchEngine.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class IndexEngine {
public:
    IndexEngine();
    ~IndexEngine();

    // Starts ultra-fast parallel NTFS MFT/USN indexing (with fallback for non-NTFS/non-admin)
    void StartIndexing(HWND notifyHwnd);
    void Stop();

    bool IsIndexing() const { return m_indexing.load(); }
    bool UsedNtfsMft() const { return m_usedNtfsMft.load(); }
    size_t IndexedCount() const;

    // O(1) zero-copy immutable snapshot retrieval
    IndexDataPtr GetSnapshot() const;

    // Convenience query for HTTP server
    std::vector<uint32_t> Query(const std::wstring& query, const SearchOptions& opts, IndexDataPtr& outSnap) const;

    // Export results to Everything EFU / CSV format
    bool ExportEfu(const std::wstring& filePath, const IndexDataPtr& snap, const std::vector<uint32_t>& indices) const;

    // Ultra-fast flat binary database cache (Everything.db — 4 bulk fread/fwrite calls!)
    bool SaveDatabase(const std::wstring& dbPath) const;
    bool LoadDatabase(const std::wstring& dbPath);

private:
    struct DriveBatch {
        std::wstring driveRoot;
        std::vector<CompactEntry> entries;
        std::vector<wchar_t>      nameArena;
        std::vector<wchar_t>      lowerArena;
    };

    void IndexWorker(HWND notifyHwnd);
    bool IndexVolumeNtfsUsn(wchar_t driveLetter, DriveBatch& batch);
    void IndexVolumeFallback(const std::wstring& rootPath, DriveBatch& batch);
    void PublishSnapshot(std::shared_ptr<IndexData>&& data);

    mutable std::mutex m_snapMutex;
    IndexDataPtr       m_snapshot;

    std::atomic<bool>   m_stopFlag{false};
    std::atomic<bool>   m_indexing{false};
    std::atomic<bool>   m_usedNtfsMft{false};
    std::atomic<size_t> m_progressCount{0};

    std::thread m_indexThread;
};
