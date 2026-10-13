// nxfire-watch — pantau sebuah path secara realtime lewat WebSocket lokal.
//
//   nxfire-watch /chat --port 9026
//
// Menyambung ke ws://127.0.0.1:<port>/ws, subscribe ke path, lalu mencetak
// setiap perubahan yang didorong server.

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>

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
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

int run(const std::string& host, int port, const std::string& path) {
    HINTERNET session = WinHttpOpen(L"nxfire-watch/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { std::fprintf(stderr, "WinHttpOpen gagal\n"); return 1; }

    HINTERNET connect = WinHttpConnect(session, toWide(host).c_str(), (INTERNET_PORT)port, 0);
    if (!connect) { std::fprintf(stderr, "tidak bisa terhubung ke %s:%d\n", host.c_str(), port); return 1; }

    HINTERNET req = WinHttpOpenRequest(connect, L"GET", L"/ws", nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!req) { std::fprintf(stderr, "OpenRequest gagal\n"); return 1; }

    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        std::fprintf(stderr, "server tidak mendukung WebSocket\n");
        return 1;
    }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        std::fprintf(stderr, "gagal upgrade ke WebSocket\n");
        return 1;
    }
    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    WinHttpCloseHandle(req);
    if (!ws) { std::fprintf(stderr, "upgrade WebSocket gagal\n"); return 1; }

    // Berlangganan.
    std::string subMsg = "{\"subscribe\":\"" + path + "\"}";
    if (WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                             (PVOID)subMsg.data(), (DWORD)subMsg.size()) != ERROR_SUCCESS) {
        std::fprintf(stderr, "gagal mengirim subscribe\n");
        return 1;
    }

    std::printf("Memantau %s (ws://%s:%d/ws) - tekan Ctrl+C untuk berhenti\n", path.c_str(), host.c_str(), port);
    std::fflush(stdout);

    char buf[65536];
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD rc = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &read, &type);
        if (rc != ERROR_SUCCESS) break;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        std::string msg(buf, read);
        std::printf("%s\n", msg.c_str());
        std::fflush(stdout);
    }

    WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    WinHttpCloseHandle(ws);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", path = "/";
    int port = 9026;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "-h" || a == "--help") {
            std::printf("nxfire-watch <path> [--host H] [--port N]\n");
            return 0;
        } else if (a[0] != '-') path = a;
    }
    return run(host, port, path);
}

#else

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::fprintf(stderr, "nxfire-watch belum tersedia di platform ini\n");
    return 1;
}

#endif
