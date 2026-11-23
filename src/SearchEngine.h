#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Compact trivially-copyable entry (48 bytes, ZERO per-entry heap allocations!)
struct CompactEntry {
    uint32_t nameOff = 0;      // offset into IndexData::nameArena (null-terminated)
    uint16_t nameLen = 0;      // length in wchar_t
    uint16_t driveIdx = 0;     // index into IndexData::driveRoots (e.g. "C:")
    int32_t  parentIdx = -1;   // index of parent folder in entries (-1 = drive root)
    uint32_t attributes = 0;
    uint64_t size = 0;
    uint64_t dateModified = 0; // FILETIME as uint64
    uint64_t dateCreated  = 0;
    uint8_t  isFolder = 0;
};

struct IndexData {
    std::vector<CompactEntry> entries;
    std::vector<wchar_t>      nameArena;   // Original-case null-terminated names
    std::vector<wchar_t>      lowerArena;  // Lowercase null-terminated names
    std::vector<std::wstring> driveRoots;  // e.g. L"C:", L"D:"
    std::vector<uint32_t>     defaultOrder; // Pre-sorted indices by (isFolder desc, nameLower asc)
    bool                      hasDefaultOrder = false;

    inline const wchar_t* NamePtr(uint32_t idx) const {
        return nameArena.data() + entries[idx].nameOff;
    }
    inline const wchar_t* LowerPtr(uint32_t idx) const {
        return lowerArena.data() + entries[idx].nameOff;
    }

    // Reconstructs parent directory path on-demand (takes ~200ns; only called for visible rows or path: queries)
    std::wstring GetParentPath(uint32_t idx) const;
    std::wstring GetFullPath(uint32_t idx) const;
};

using IndexDataPtr = std::shared_ptr<const IndexData>;

enum class FilterCategory {
    All = 0,
    Audio,
    Compressed,
    Document,
    Executable,
    Folder,
    Picture,
    Video
};

struct SearchOptions {
    bool matchCase = false;
    bool matchWholeWord = false;
    bool matchPath = false;
    bool matchRegex = false;
    FilterCategory filter = FilterCategory::All;
    int sortColumn = 0;       // 0=Name, 1=Path, 2=Size, 3=DateModified
    bool sortAscending = true;
};

class SearchEngine {
public:
    static std::wstring ToLower(const std::wstring& s);
    static const wchar_t* GetExtensionPtr(const wchar_t* nameLower, uint16_t nameLen);

    // Builds defaultOrder (indices sorted by isFolder desc, nameLower asc)
    static void BuildDefaultOrder(IndexData& data);

    // Multi-core parallel search over flat arena with cancellation token
    static std::vector<uint32_t> Execute(
        const IndexData& data,
        const std::wstring& query,
        const SearchOptions& opts,
        const std::atomic<uint64_t>* cancelGen = nullptr,
        uint64_t myGen = 0
    );

    static bool MatchesCategory(const IndexData& data, uint32_t idx, FilterCategory cat);
};
