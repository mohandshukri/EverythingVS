#include "SearchEngine.h"
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>

std::wstring IndexData::GetParentPath(uint32_t idx) const {
    if (idx >= entries.size()) return L"";
    const auto& e = entries[idx];
    const std::wstring& root = (e.driveIdx < driveRoots.size()) ? driveRoots[e.driveIdx] : L"";
    if (e.parentIdx < 0) return root;

    uint32_t chain[64];
    int depth = 0;
    int32_t cur = e.parentIdx;
    while (cur >= 0 && static_cast<uint32_t>(cur) < entries.size() && depth < 64) {
        chain[depth++] = static_cast<uint32_t>(cur);
        int32_t next = entries[cur].parentIdx;
        if (next == cur) break;
        cur = next;
    }

    size_t totalLen = root.size();
    for (int i = 0; i < depth; ++i) totalLen += 1 + entries[chain[i]].nameLen;

    std::wstring res;
    res.reserve(totalLen);
    res.append(root);
    for (int i = depth - 1; i >= 0; --i) {
        res.push_back(L'\\');
        const auto& pe = entries[chain[i]];
        res.append(nameArena.data() + pe.nameOff, pe.nameLen);
    }
    return res;
}

std::wstring IndexData::GetFullPath(uint32_t idx) const {
    if (idx >= entries.size()) return L"";
    std::wstring p = GetParentPath(idx);
    const auto& e = entries[idx];
    if (p.empty()) return std::wstring(nameArena.data() + e.nameOff, e.nameLen);
    if (p.back() != L'\\') p.push_back(L'\\');
    p.append(nameArena.data() + e.nameOff, e.nameLen);
    return p;
}

std::wstring SearchEngine::ToLower(const std::wstring& s) {
    std::wstring out = s;
    for (auto& ch : out) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch + 32);
        } else if (ch > 127) {
            ch = static_cast<wchar_t>(towlower(ch));
        }
    }
    return out;
}

const wchar_t* SearchEngine::GetExtensionPtr(const wchar_t* nameLower, uint16_t nameLen) {
    if (nameLen == 0) return nullptr;
    for (int i = static_cast<int>(nameLen) - 1; i >= 0; --i) {
        if (nameLower[i] == L'.') {
            return (i + 1 < nameLen) ? (nameLower + i + 1) : nullptr;
        }
    }
    return nullptr;
}

void SearchEngine::BuildDefaultOrder(IndexData& data) {
    const uint32_t n = static_cast<uint32_t>(data.entries.size());
    data.defaultOrder.resize(n);
    for (uint32_t i = 0; i < n; ++i) data.defaultOrder[i] = i;

    const auto* ents = data.entries.data();
    const wchar_t* arena = data.lowerArena.data();

    std::sort(data.defaultOrder.begin(), data.defaultOrder.end(), [ents, arena](uint32_t a, uint32_t b) {
        if (ents[a].isFolder != ents[b].isFolder) return ents[a].isFolder > ents[b].isFolder;
        return wcscmp(arena + ents[a].nameOff, arena + ents[b].nameOff) < 0;
    });
    data.hasDefaultOrder = true;
}

bool SearchEngine::MatchesCategory(const IndexData& data, uint32_t idx, FilterCategory cat) {
    if (cat == FilterCategory::All) return true;
    const auto& e = data.entries[idx];
    if (cat == FilterCategory::Folder) return e.isFolder != 0;
    if (e.isFolder) return false;

    const wchar_t* extPtr = GetExtensionPtr(data.LowerPtr(idx), e.nameLen);
    if (!extPtr) return false;

    static const std::unordered_set<std::wstring> kAudio = {
        L"aac", L"ac3", L"aif", L"aiff", L"amr", L"ape", L"flac", L"m4a", L"mid",
        L"midi", L"mp3", L"ogg", L"opus", L"wav", L"wma"
    };
    static const std::unordered_set<std::wstring> kCompressed = {
        L"7z", L"ace", L"arj", L"bz2", L"cab", L"gz", L"gzip", L"iso", L"lz",
        L"rar", L"tar", L"tgz", L"xz", L"zip"
    };
    static const std::unordered_set<std::wstring> kDocument = {
        L"c", L"cpp", L"cs", L"csv", L"doc", L"docx", L"epub", L"h", L"hpp", L"htm",
        L"html", L"ini", L"java", L"js", L"json", L"log", L"md", L"odt", L"pdf",
        L"ppt", L"pptx", L"py", L"rtf", L"sql", L"ts", L"txt", L"xls", L"xlsx", L"xml"
    };
    static const std::unordered_set<std::wstring> kExecutable = {
        L"bat", L"cmd", L"com", L"dll", L"exe", L"msi", L"ps1", L"scr", L"sys", L"vbs"
    };
    static const std::unordered_set<std::wstring> kPicture = {
        L"avif", L"bmp", L"gif", L"heic", L"ico", L"jpeg", L"jpg", L"png", L"psd",
        L"raw", L"svg", L"tif", L"tiff", L"webp"
    };
    static const std::unordered_set<std::wstring> kVideo = {
        L"3gp", L"asf", L"avi", L"flv", L"m2ts", L"m4v", L"mkv", L"mov", L"mp4",
        L"mpeg", L"mpg", L"ts", L"vob", L"webm", L"wmv"
    };

    std::wstring ext(extPtr);
    switch (cat) {
        case FilterCategory::Audio:      return kAudio.count(ext) > 0;
        case FilterCategory::Compressed: return kCompressed.count(ext) > 0;
        case FilterCategory::Document:   return kDocument.count(ext) > 0;
        case FilterCategory::Executable: return kExecutable.count(ext) > 0;
        case FilterCategory::Picture:    return kPicture.count(ext) > 0;
        case FilterCategory::Video:      return kVideo.count(ext) > 0;
        default: return true;
    }
}

namespace {

inline bool WildcardMatch(const wchar_t* text, const wchar_t* pat) {
    const wchar_t* star = nullptr;
    const wchar_t* ss = text;
    while (*text) {
        if (*pat == L'?' || *pat == *text) {
            ++text; ++pat;
        } else if (*pat == L'*') {
            star = pat++; ss = text;
        } else if (star) {
            pat = star + 1; text = ++ss;
        } else {
            return false;
        }
    }
    while (*pat == L'*') ++pat;
    return *pat == L'\0';
}

inline bool ContainsWholeWord(const wchar_t* hay, size_t hayLen, const wchar_t* needle, size_t nedLen) {
    if (nedLen == 0) return true;
    const wchar_t* p = wcsstr(hay, needle);
    while (p) {
        size_t pos = static_cast<size_t>(p - hay);
        bool leftOk  = (pos == 0) || !iswalnum(hay[pos - 1]);
        size_t end   = pos + nedLen;
        bool rightOk = (end >= hayLen) || !iswalnum(hay[end]);
        if (leftOk && rightOk) return true;
        p = wcsstr(p + 1, needle);
    }
    return false;
}

uint64_t ParseSizeSpec(const std::wstring& s) {
    if (s.empty()) return 0;
    size_t idx = 0;
    double val = 0.0;
    try { val = std::stod(s, &idx); } catch (...) { return 0; }
    std::wstring unit = SearchEngine::ToLower(s.substr(idx));
    if (unit == L"kb" || unit == L"k") return static_cast<uint64_t>(val * 1024.0);
    if (unit == L"mb" || unit == L"m") return static_cast<uint64_t>(val * 1024.0 * 1024.0);
    if (unit == L"gb" || unit == L"g") return static_cast<uint64_t>(val * 1024.0 * 1024.0 * 1024.0);
    if (unit == L"tb" || unit == L"t") return static_cast<uint64_t>(val * 1024.0 * 1024.0 * 1024.0 * 1024.0);
    return static_cast<uint64_t>(val);
}

enum class TermKind {
    Plain,
    Wildcard,
    Regex,
    ExtList,
    FolderOnly,
    FileOnly,
    SizeCompare,
    PathSubstr
};

struct ParsedTerm {
    bool negate = false;
    TermKind kind = TermKind::Plain;
    std::wstring value;
    std::wstring valueLower;
    std::vector<std::wstring> extList;
    wchar_t sizeOp = L'=';
    uint64_t sizeBytes = 0;
    bool hasRegex = false;
    std::wregex regexObj;
};

struct OrGroup {
    std::vector<ParsedTerm> alts;
};

ParsedTerm ParseSingleTerm(std::wstring raw, const SearchOptions& opts) {
    ParsedTerm t;
    if (!raw.empty() && raw[0] == L'!') {
        t.negate = true;
        raw.erase(0, 1);
    }
    std::wstring lower = SearchEngine::ToLower(raw);

    if (lower == L"folder:") { t.kind = TermKind::FolderOnly; return t; }
    if (lower == L"file:")   { t.kind = TermKind::FileOnly; return t; }
    if (lower.rfind(L"ext:", 0) == 0) {
        t.kind = TermKind::ExtList;
        std::wstringstream ss(lower.substr(4));
        std::wstring item;
        while (std::getline(ss, item, L';')) {
            if (!item.empty() && item[0] == L'.') item.erase(0, 1);
            if (!item.empty()) t.extList.push_back(item);
        }
        return t;
    }
    if (lower.rfind(L"size:", 0) == 0) {
        t.kind = TermKind::SizeCompare;
        std::wstring spec = lower.substr(5);
        if (!spec.empty() && (spec[0] == L'>' || spec[0] == L'<' || spec[0] == L'=')) {
            t.sizeOp = spec[0];
            spec.erase(0, 1);
        }
        t.sizeBytes = ParseSizeSpec(spec);
        return t;
    }
    if (lower.rfind(L"path:", 0) == 0 || lower.rfind(L"parent:", 0) == 0) {
        t.kind = TermKind::PathSubstr;
        size_t colon = raw.find(L':');
        t.value = raw.substr(colon + 1);
        t.valueLower = SearchEngine::ToLower(t.value);
        return t;
    }
    if (lower.rfind(L"regex:", 0) == 0 || opts.matchRegex) {
        t.kind = TermKind::Regex;
        std::wstring pat = (lower.rfind(L"regex:", 0) == 0) ? raw.substr(6) : raw;
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!opts.matchCase) flags |= std::regex_constants::icase;
            t.regexObj = std::wregex(pat, flags);
            t.hasRegex = true;
        } catch (...) {
            t.hasRegex = false;
        }
        return t;
    }

    t.value = raw;
    t.valueLower = lower;
    if (raw.find(L'*') != std::wstring::npos || raw.find(L'?') != std::wstring::npos) {
        t.kind = TermKind::Wildcard;
    } else {
        t.kind = TermKind::Plain;
    }
    return t;
}

std::vector<OrGroup> ParseQuery(const std::wstring& query, const SearchOptions& opts) {
    std::vector<std::wstring> tokens;
    std::wstring cur;
    bool inQuotes = false;
    for (wchar_t c : query) {
        if (c == L'"') {
            inQuotes = !inQuotes;
        } else if (!inQuotes && iswspace(c)) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);

    std::vector<OrGroup> groups;
    for (const auto& tok : tokens) {
        OrGroup g;
        size_t start = 0;
        while (start <= tok.size()) {
            size_t bar = tok.find(L'|', start);
            std::wstring part = (bar == std::wstring::npos) ? tok.substr(start) : tok.substr(start, bar - start);
            if (!part.empty()) g.alts.push_back(ParseSingleTerm(part, opts));
            if (bar == std::wstring::npos) break;
            start = bar + 1;
        }
        if (!g.alts.empty()) groups.push_back(std::move(g));
    }
    return groups;
}

inline bool EvalTerm(const IndexData& data, uint32_t idx, const ParsedTerm& t, const SearchOptions& opts) {
    const auto& e = data.entries[idx];
    bool hit = false;
    switch (t.kind) {
        case TermKind::Plain: {
            if (!opts.matchPath && !opts.matchWholeWord) {
                const wchar_t* hay = opts.matchCase ? data.NamePtr(idx) : data.LowerPtr(idx);
                const wchar_t* ned = opts.matchCase ? t.value.c_str() : t.valueLower.c_str();
                hit = (wcsstr(hay, ned) != nullptr);
            } else if (!opts.matchPath) {
                const wchar_t* hay = opts.matchCase ? data.NamePtr(idx) : data.LowerPtr(idx);
                const std::wstring& ned = opts.matchCase ? t.value : t.valueLower;
                hit = ContainsWholeWord(hay, e.nameLen, ned.c_str(), ned.size());
            } else {
                std::wstring full = data.GetFullPath(idx);
                if (!opts.matchCase) full = SearchEngine::ToLower(full);
                const std::wstring& ned = opts.matchCase ? t.value : t.valueLower;
                hit = opts.matchWholeWord
                    ? ContainsWholeWord(full.c_str(), full.size(), ned.c_str(), ned.size())
                    : (full.find(ned) != std::wstring::npos);
            }
            break;
        }
        case TermKind::FolderOnly:
            hit = e.isFolder != 0;
            break;
        case TermKind::FileOnly:
            hit = e.isFolder == 0;
            break;
        case TermKind::ExtList: {
            if (!e.isFolder) {
                const wchar_t* extPtr = SearchEngine::GetExtensionPtr(data.LowerPtr(idx), e.nameLen);
                if (extPtr) {
                    for (const auto& target : t.extList) {
                        if (wcscmp(extPtr, target.c_str()) == 0) { hit = true; break; }
                    }
                }
            }
            break;
        }
        case TermKind::SizeCompare:
            if (!e.isFolder) {
                if (t.sizeOp == L'>') hit = e.size > t.sizeBytes;
                else if (t.sizeOp == L'<') hit = e.size < t.sizeBytes;
                else hit = e.size == t.sizeBytes;
            }
            break;
        case TermKind::PathSubstr: {
            std::wstring p = data.GetParentPath(idx);
            if (!opts.matchCase) p = SearchEngine::ToLower(p);
            const std::wstring& ned = opts.matchCase ? t.value : t.valueLower;
            hit = p.find(ned) != std::wstring::npos;
            break;
        }
        case TermKind::Regex:
            if (t.hasRegex) {
                if (opts.matchPath) {
                    std::wstring full = data.GetFullPath(idx);
                    hit = std::regex_search(full, t.regexObj);
                } else {
                    hit = std::regex_search(data.NamePtr(idx), t.regexObj);
                }
            }
            break;
        case TermKind::Wildcard: {
            if (!opts.matchPath) {
                const wchar_t* hay = opts.matchCase ? data.NamePtr(idx) : data.LowerPtr(idx);
                const wchar_t* pat = opts.matchCase ? t.value.c_str() : t.valueLower.c_str();
                hit = WildcardMatch(hay, pat);
            } else {
                std::wstring full = data.GetFullPath(idx);
                if (!opts.matchCase) full = SearchEngine::ToLower(full);
                const wchar_t* pat = opts.matchCase ? t.value.c_str() : t.valueLower.c_str();
                hit = WildcardMatch(full.c_str(), pat);
            }
            break;
        }
    }
    return t.negate ? !hit : hit;
}

inline bool IsCancelled(const std::atomic<uint64_t>* cancelGen, uint64_t myGen) {
    return cancelGen && cancelGen->load(std::memory_order_relaxed) != myGen;
}

} // namespace

std::vector<uint32_t> SearchEngine::Execute(
    const IndexData& data,
    const std::wstring& query,
    const SearchOptions& opts,
    const std::atomic<uint64_t>* cancelGen,
    uint64_t myGen
) {
    const uint32_t total = static_cast<uint32_t>(data.entries.size());
    std::vector<uint32_t> results;
    if (total == 0) return results;

    const bool useDefaultOrder = data.hasDefaultOrder && data.defaultOrder.size() == total;
    std::vector<OrGroup> groups = ParseQuery(query, opts);

    // Empty query + All filter: instant O(N) memcpy of indices (~2ms for 2M items!)
    if (groups.empty() && opts.filter == FilterCategory::All) {
        if (useDefaultOrder) {
            results = data.defaultOrder;
        } else {
            results.resize(total);
            for (uint32_t i = 0; i < total; ++i) results[i] = i;
        }
        if (opts.sortColumn == 0) {
            if (useDefaultOrder && !opts.sortAscending) std::reverse(results.begin(), results.end());
            return results;
        }
    } else {
        const bool isFastPlain =
            opts.filter == FilterCategory::All &&
            !opts.matchCase && !opts.matchWholeWord && !opts.matchPath && !opts.matchRegex &&
            groups.size() == 1 && groups[0].alts.size() == 1 &&
            groups[0].alts[0].kind == TermKind::Plain && !groups[0].alts[0].negate;

        const wchar_t* fastNeedle = isFastPlain ? groups[0].alts[0].valueLower.c_str() : nullptr;
        const wchar_t* lowerBase  = data.lowerArena.data();
        const auto*    ents       = data.entries.data();
        const uint32_t* orderPtr  = useDefaultOrder ? data.defaultOrder.data() : nullptr;

        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads < 1) numThreads = 1;
        if (numThreads > 8) numThreads = 8;
        if (total < 32768) numThreads = 1;

        if (numThreads == 1) {
            results.reserve(total < 8192 ? total : 8192);
            for (uint32_t pos = 0; pos < total; ++pos) {
                if ((pos & 8191) == 0 && IsCancelled(cancelGen, myGen)) return {};
                uint32_t i = orderPtr ? orderPtr[pos] : pos;
                if (isFastPlain) {
                    if (wcsstr(lowerBase + ents[i].nameOff, fastNeedle) != nullptr) results.push_back(i);
                    continue;
                }
                if (!MatchesCategory(data, i, opts.filter)) continue;
                bool allMatch = true;
                for (const auto& g : groups) {
                    bool anyAlt = false;
                    for (const auto& term : g.alts) {
                        if (EvalTerm(data, i, term, opts)) { anyAlt = true; break; }
                    }
                    if (!anyAlt) { allMatch = false; break; }
                }
                if (allMatch) results.push_back(i);
            }
        } else {
            std::vector<std::vector<uint32_t>> buckets(numThreads);
            std::vector<std::thread> workers;
            workers.reserve(numThreads);
            uint32_t chunk = (total + numThreads - 1) / numThreads;

            for (unsigned int t = 0; t < numThreads; ++t) {
                uint32_t start = t * chunk;
                uint32_t end = (start + chunk < total) ? (start + chunk) : total;
                if (start >= end) continue;

                workers.emplace_back([&, t, start, end]() {
                    auto& out = buckets[t];
                    out.reserve(512);
                    for (uint32_t pos = start; pos < end; ++pos) {
                        if ((pos & 8191) == 0 && IsCancelled(cancelGen, myGen)) return;
                        uint32_t i = orderPtr ? orderPtr[pos] : pos;
                        if (isFastPlain) {
                            if (wcsstr(lowerBase + ents[i].nameOff, fastNeedle) != nullptr) out.push_back(i);
                            continue;
                        }
                        if (!MatchesCategory(data, i, opts.filter)) continue;
                        bool allMatch = true;
                        for (const auto& g : groups) {
                            bool anyAlt = false;
                            for (const auto& term : g.alts) {
                                if (EvalTerm(data, i, term, opts)) { anyAlt = true; break; }
                            }
                            if (!anyAlt) { allMatch = false; break; }
                        }
                        if (allMatch) out.push_back(i);
                    }
                });
            }
            for (auto& w : workers) w.join();
            if (IsCancelled(cancelGen, myGen)) return {};

            size_t totalHits = 0;
            for (const auto& b : buckets) totalHits += b.size();
            results.reserve(totalHits);
            for (const auto& b : buckets) {
                results.insert(results.end(), b.begin(), b.end());
            }
        }
    }

    if (IsCancelled(cancelGen, myGen)) return {};

    // Fast path: already sorted by Name via defaultOrder!
    const int col = opts.sortColumn;
    const bool asc = opts.sortAscending;
    if (col == 0 && useDefaultOrder) {
        if (!asc) std::reverse(results.begin(), results.end());
        return results;
    }

    const auto* ents = data.entries.data();
    const wchar_t* arena = data.lowerArena.data();

    if (col == 1) {
        // Path sort: precompute paths only for matched items (never for 2M unmatched items)
        std::vector<std::wstring> paths(results.size());
        for (size_t k = 0; k < results.size(); ++k) {
            paths[k] = ToLower(data.GetParentPath(results[k]));
        }
        std::vector<uint32_t> perm(results.size());
        for (uint32_t k = 0; k < static_cast<uint32_t>(results.size()); ++k) perm[k] = k;
        std::sort(perm.begin(), perm.end(), [&](uint32_t a, uint32_t b) {
            return asc ? (paths[a] < paths[b]) : (paths[a] > paths[b]);
        });
        std::vector<uint32_t> sorted(results.size());
        for (size_t k = 0; k < results.size(); ++k) sorted[k] = results[perm[k]];
        return sorted;
    }

    std::sort(results.begin(), results.end(), [&](uint32_t a, uint32_t b) {
        if (col == 0) {
            if (ents[a].isFolder != ents[b].isFolder) {
                return asc ? (ents[a].isFolder > ents[b].isFolder) : (ents[a].isFolder < ents[b].isFolder);
            }
            int c = wcscmp(arena + ents[a].nameOff, arena + ents[b].nameOff);
            return asc ? (c < 0) : (c > 0);
        }
        if (col == 2) {
            return asc ? (ents[a].size < ents[b].size) : (ents[a].size > ents[b].size);
        }
        if (col == 3) {
            return asc ? (ents[a].dateModified < ents[b].dateModified) : (ents[a].dateModified > ents[b].dateModified);
        }
        return a < b;
    });

    return results;
}
