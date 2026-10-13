#include <nxfire/Http.h>

#include <cstdio>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#endif

namespace nxfire {
namespace http {

std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

#ifdef _WIN32

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), len);
    return out;
}

std::string fromWide(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace

Response request(const std::string& method,
                 const std::string& url,
                 const std::string& body,
                 const std::vector<std::pair<std::string, std::string>>& headers) {
    Response res;

    std::wstring wurl = toWide(url);
    std::wstring wmethod = toWide(method);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    uc.lpszHostName    = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath     = path;
    uc.dwUrlPathLength = 4095;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        res.error = "URL tidak valid";
        return res;
    }

    HINTERNET session = WinHttpOpen(L"NxfireDB/0.1",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = "WinHttpOpen gagal";
        return res;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000);

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        res.error = "tidak bisa terhubung ke host";
        return res;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(connect, wmethod.c_str(), path,
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        res.error = "WinHttpOpenRequest gagal";
        return res;
    }

    // Header.
    for (const auto& h : headers) {
        std::wstring line = toWide(h.first + ": " + h.second + "\r\n");
        WinHttpAddRequestHeaders(req, line.c_str(), DWORD(line.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    {
        std::wstring ct = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(req, ct.c_str(), DWORD(ct.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    BOOL sent = WinHttpSendRequest(
        req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
        DWORD(body.size()), DWORD(body.size()), 0);
    if (!sent) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        res.error = "failed to send request";
        return res;
    }

    if (!WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        res.error = "tidak ada balasan dari server";
        return res;
    }

    DWORD status = 0;
    DWORD statusLen = sizeof(status);
    WinHttpQueryHeaders(req,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen,
                        WINHTTP_NO_HEADER_INDEX);
    res.status = int(status);

    // Baca body.
    std::string raw;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string chunk(size_t(avail), '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
        chunk.resize(read);
        raw += chunk;
        if (read < avail) break;
    }

    // Firebase membalas UTF-8; kalau ternyata UTF-16 (jarang), konversi.
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        std::wstring w(raw.begin() + 2, raw.end());
        // reinterpret 2-byte pairs
        w.clear();
        for (size_t i = 2; i + 1 < raw.size(); i += 2) {
            w.push_back(wchar_t((unsigned char)raw[i] | ((unsigned char)raw[i + 1] << 8)));
        }
        res.body = fromWide(w);
    } else {
        res.body = std::move(raw);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return res;
}

#else // !_WIN32

Response request(const std::string&, const std::string&, const std::string&,
                 const std::vector<std::pair<std::string, std::string>>&) {
    Response res;
    res.error = "HTTP client belum tersedia di platform ini (pakai libcurl di Fase 6)";
    return res;
}

#endif

} // namespace http
} // namespace nxfire
