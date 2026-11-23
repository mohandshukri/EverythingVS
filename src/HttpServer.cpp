#include "HttpServer.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "ws2_32.lib")

HttpServer::HttpServer(IndexEngine& engine) : m_engine(engine) {}

HttpServer::~HttpServer() {
    Stop();
}

static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), sz, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), sz);
    return out;
}

static std::string UrlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = { in[i + 1], in[i + 2], 0 };
            out.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

static std::string HtmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

bool HttpServer::Start(int port) {
    if (m_running.load()) return true;
    m_port = port;

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR || listen(s, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return false;
    }

    m_listenSock = static_cast<uintptr_t>(s);
    m_running.store(true);
    m_thread = std::thread(&HttpServer::ServerLoop, this);
    return true;
}

void HttpServer::Stop() {
    if (!m_running.exchange(false)) return;
    SOCKET s = static_cast<SOCKET>(m_listenSock);
    if (s != INVALID_SOCKET) {
        closesocket(s);
        m_listenSock = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    if (m_thread.joinable()) m_thread.join();
    WSACleanup();
}

void HttpServer::ServerLoop() {
    SOCKET listenSock = static_cast<SOCKET>(m_listenSock);
    while (m_running.load()) {
        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET) break;
        HandleClient(static_cast<uintptr_t>(client));
    }
}

void HttpServer::HandleClient(uintptr_t clientSocket) {
    SOCKET client = static_cast<SOCKET>(clientSocket);
    DWORD timeoutMs = 3000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    char reqBuf[4096] = {};
    int received = recv(client, reqBuf, sizeof(reqBuf) - 1, 0);
    if (received <= 0) {
        closesocket(client);
        return;
    }

    std::string req(reqBuf, received);
    std::string searchUtf8;
    bool wantJson = false;
    size_t maxResults = 200;

    size_t pos = req.find("GET /");
    if (pos != std::string::npos) {
        size_t endLine = req.find(" HTTP/", pos);
        if (endLine != std::string::npos) {
            std::string url = req.substr(pos + 4, endLine - (pos + 4));
            size_t qPos = url.find("search=");
            if (qPos != std::string::npos) {
                size_t amp = url.find('&', qPos);
                std::string raw = (amp == std::string::npos) ? url.substr(qPos + 7) : url.substr(qPos + 7, amp - (qPos + 7));
                searchUtf8 = UrlDecode(raw);
            }
            if (url.find("json=1") != std::string::npos) {
                wantJson = true;
            }
            size_t cPos = url.find("count=");
            if (cPos != std::string::npos) {
                size_t amp = url.find('&', cPos);
                std::string rawC = (amp == std::string::npos) ? url.substr(cPos + 6) : url.substr(cPos + 6, amp - (cPos + 6));
                int parsed = atoi(rawC.c_str());
                if (parsed > 0 && parsed <= 2000) maxResults = static_cast<size_t>(parsed);
            }
        }
    }

    SearchOptions opts;
    IndexDataPtr snap;
    std::vector<uint32_t> matches = m_engine.Query(Utf8ToW(searchUtf8), opts, snap);
    size_t limit = matches.size() > maxResults ? maxResults : matches.size();

    std::string body;
    std::string contentType;

    if (wantJson) {
        contentType = "application/json; charset=utf-8";
        std::ostringstream js;
        js << "{\"query\":\"" << JsonEscape(searchUtf8) << "\",\"total\":" << matches.size()
           << ",\"returned\":" << limit << ",\"results\":[";
        if (snap) {
            for (size_t i = 0; i < limit; ++i) {
                if (i > 0) js << ",";
                uint32_t idx = matches[i];
                const auto& e = snap->entries[idx];
                std::wstring nameW(snap->NamePtr(idx), e.nameLen);
                std::wstring pathW = snap->GetParentPath(idx);
                std::wstring fullW = snap->GetFullPath(idx);
                js << "{\"name\":\"" << JsonEscape(WToUtf8(nameW))
                   << "\",\"path\":\"" << JsonEscape(WToUtf8(pathW))
                   << "\",\"fullPath\":\"" << JsonEscape(WToUtf8(fullW))
                   << "\",\"size\":" << e.size
                   << ",\"isFolder\":" << (e.isFolder ? "true" : "false") << "}";
            }
        }
        js << "]}";
        body = js.str();
    } else {
        contentType = "text/html; charset=utf-8";
        std::ostringstream html;
        html << "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
             << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             << "<meta name=\"robots\" content=\"noindex, nofollow\">"
             << "<title>EverythingVS HTTP Search</title>"
             << "<style>"
             << ":root{--bg:#f8fafc;--card:#fff;--fg:#0f172a;--muted:#64748b;--bd:#e2e8f0;--accent:#2563eb;--hover:#f1f5f9}"
             << "@media(prefers-color-scheme:dark){:root{--bg:#0f172a;--card:#1e293b;--fg:#f1f5f9;--muted:#94a3b8;--bd:#334155;--accent:#3b82f6;--hover:#273549}}"
             << "body{font-family:Segoe UI,system-ui,sans-serif;margin:0;padding:24px;background:var(--bg);color:var(--fg)}"
             << ".wrap{max-width:1200px;margin:0 auto}"
             << "h2{margin:0 0 14px;font-weight:600}"
             << "form{display:flex;gap:8px;margin-bottom:14px}"
             << "input{flex:1;padding:9px 12px;font-size:15px;border:1px solid var(--bd);border-radius:8px;background:var(--card);color:var(--fg)}"
             << "button{padding:9px 18px;font-size:14px;border:0;border-radius:8px;background:var(--accent);color:#fff;cursor:pointer;font-weight:600}"
             << ".meta{color:var(--muted);font-size:13px;margin-bottom:12px}"
             << "table{border-collapse:collapse;width:100%;background:var(--card);border:1px solid var(--bd);border-radius:8px;overflow:hidden;font-size:13px}"
             << "th,td{text-align:left;padding:8px 10px;border-bottom:1px solid var(--bd);word-break:break-all}"
             << "th{background:var(--hover);font-weight:600}tr:hover td{background:var(--hover)}"
             << ".cp{padding:3px 8px;font-size:12px;background:transparent;color:var(--accent);border:1px solid var(--accent);border-radius:5px}"
             << "</style></head><body><div class=\"wrap\">"
             << "<h2>EverythingVS Local Search</h2>"
             << "<form method=\"GET\" action=\"/\">"
             << "<input type=\"text\" name=\"search\" autofocus value=\"" << HtmlEscape(searchUtf8) << "\" placeholder=\"Search files &amp; folders (ext:, size:, folder:, !, |, regex:)...\">"
             << "<button type=\"submit\">Search</button></form>"
             << "<div class=\"meta\">Showing " << limit << " of " << matches.size() << " results &middot; JSON API: <code>/?search=...&amp;json=1</code></div>"
             << "<table><thead><tr><th>Name</th><th>Path</th><th>Size</th><th>Action</th></tr></thead><tbody>";

        if (snap) {
            for (size_t i = 0; i < limit; ++i) {
                uint32_t idx = matches[i];
                const auto& e = snap->entries[idx];
                std::wstring nameW(snap->NamePtr(idx), e.nameLen);
                std::wstring pathW = snap->GetParentPath(idx);
                std::wstring fullW = snap->GetFullPath(idx);
                std::string fullUtf8 = WToUtf8(fullW);
                html << "<tr><td>" << HtmlEscape(WToUtf8(nameW)) << "</td><td>"
                     << HtmlEscape(WToUtf8(pathW)) << "</td><td>"
                     << (e.isFolder ? "&lt;DIR&gt;" : std::to_string(e.size)) << "</td><td>"
                     << "<button type=\"button\" class=\"cp\" data-p=\"" << HtmlEscape(fullUtf8) << "\" onclick=\"cp(this)\">Copy Path</button>"
                     << "</td></tr>";
            }
        }
        html << "</tbody></table></div>"
             << "<script>function cp(b){navigator.clipboard.writeText(b.dataset.p).then(()=>{const t=b.textContent;b.textContent='Copied!';setTimeout(()=>b.textContent=t,1200);});}</script>"
             << "</body></html>";
        body = html.str();
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: " << contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "X-Frame-Options: DENY\r\n"
         << "Connection: close\r\n\r\n"
         << body;

    std::string wire = resp.str();
    send(client, wire.c_str(), static_cast<int>(wire.size()), 0);
    closesocket(client);
}
