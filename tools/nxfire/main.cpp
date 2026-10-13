// nxfire — menu kelola NxfireDB.
//
//   nxfire              buka menu
//   nxfire test         uji koneksi ke Firebase
//   nxfire info         info API (untuk developer)
//   nxfire setup        buat/ubah nxfire.json
//   nxfire start        nyalakan server di latar belakang (selalu jalan)
//   nxfire stop         hentikan server yang sedang jalan
//   nxfire status       cek server jalan atau tidak
//   nxfire autostart    lihat/atur auto-start saat komputer menyala
//   nxfire server       jalankan nxfire-server (foreground, untuk debug)

#include <nxfire/Nxfire.h>
#include <nxfire/Theme.h>
#include "../server/tokens.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace T = nxfire::theme;

#ifndef NXFIRE_VERSION
#define NXFIRE_VERSION "0.1.0"
#endif

using namespace nxfire;

namespace {

std::string g_exeDir;   // folder nxfire.exe (untuk menemukan nxfire-server)
std::string g_config = "nxfire.json";

std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string() : path.substr(0, p + 1);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void banner() {
    std::string R = T::c(T::RESET);
    std::printf(
        "\n"
        "%sNXFIREDB%s\n"
        "%s(NXDOM) Nusantara eXtreme Development Object Model%s\n"
        "%sVersion :v%s%s\n"
        "%s===================================================%s\n",
        T::c(T::ORANGE).c_str(), R.c_str(),
        T::c(T::PURPLE).c_str(), R.c_str(),
        T::c(T::GRAY).c_str(), NXFIRE_VERSION, R.c_str(),
        T::c(T::GRAY).c_str(), R.c_str());
}

// Muat konfigurasi ke Nxfire; kembalikan true bila berhasil.
bool loadConfig(Nxfire& db) {
    if (db.open(g_config)) return true;
    std::printf("  (config not ready: %s)\n", db.lastError().c_str());
    return false;
}

// --------------------------------------------------------------------------
// Cek port, PID, dan auto-start
// --------------------------------------------------------------------------
bool portListening(int port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int s = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int r = connect(s, (sockaddr*)&addr, sizeof(addr));
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return r == 0;
}

std::string serverExePath() {
#ifdef _WIN32
    return g_exeDir + "nxfire-server.exe";
#else
    return g_exeDir + "nxfire-server";
#endif
}

std::string selfExePath() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::string(buf, n);
#else
    return "/proc/self/exe";
#endif
}

std::string pidFile() { return g_exeDir + "nxfire.pid"; }

std::string absConfig() {
    try {
        return std::filesystem::absolute(g_config).string();
    } catch (...) {
        return g_config;
    }
}

// Port dari konfigurasi (default 9026).
int configPort() {
    Nxfire db;
    if (db.open(g_config)) return db.port();
    return 9026;
}

// Jalankan "nxfire start ..." saat komputer menyala.
std::string autoStartPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    std::string base = appdata ? appdata
                               : (std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".");
    return base + "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\NxfireDB.bat";
#else
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
    return base + "/.config/autostart/nxfiredb.desktop";
#endif
}

bool autoStartEnabled() {
    std::error_code ec;
    return std::filesystem::exists(autoStartPath(), ec);
}

bool enableAutoStart() {
    std::error_code ec;
    std::filesystem::path p(autoStartPath());
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::trunc);
    if (!out) return false;
#ifndef _WIN32
    if (p.extension() == ".desktop") {
        out << "[Desktop Entry]\n";
        out << "Type=Application\n";
        out << "Name=NxfireDB\n";
        out << "Exec=\"" << selfExePath() << "\" start --config \"" << absConfig() << "\"\n";
        out << "X-GNOME-Autostart-enabled=true\n";
        return true;
    }
#endif
    out << "@echo off\r\n";
    out << "\"" << selfExePath() << "\" start --config \"" << absConfig() << "\"\r\n";
    return true;
}

bool disableAutoStart() {
    std::error_code ec;
    return std::filesystem::remove(autoStartPath(), ec);
}

// --------------------------------------------------------------------------
// Perintah
// --------------------------------------------------------------------------
std::string tokenStorePath();
int menuToken();

int cmdTest() {
    Nxfire db;
    if (!db.open(g_config)) {
        std::printf("Failed to load config '%s': %s\n", g_config.c_str(), db.lastError().c_str());
        return 1;
    }
    std::printf("Testing %s ...\n", db.firebaseUrl().c_str());
    QueryOpts q; q.shallow = true;
    Result r = db.get("/", q);
    if (r.ok) {
        std::printf("%sOK%s - connected. Latency: %d ms\n",
                    T::c(T::GREEN).c_str(), T::c(T::RESET).c_str(), db.lastLatencyMs());
        return 0;
    }
    std::printf("%sFAILED%s - %s", T::c(T::RED).c_str(), T::c(T::RESET).c_str(), r.error.c_str());
    if (!r.hint.empty()) std::printf(" %s(%s)%s", T::c(T::GRAY).c_str(), r.hint.c_str(),
                                     T::c(T::RESET).c_str());
    std::printf("\n");
    return 1;
}

int cmdInfo() {
    Nxfire db;
    bool ok = db.open(g_config);
    int port = ok ? db.port() : 9026;

    nxfiretokens::TokenStore store(tokenStorePath());
    bool authOn = !store.empty();

    std::printf("\nNxfireDB API (for developers)\n");
    std::printf("===================================================\n");
    std::printf("Firebase : %s\n", ok ? db.firebaseUrl().c_str() : "(config not ready)");
    std::printf("Port     : %d\n", port);
    std::printf("Config   : %s\n", g_config.c_str());
    std::printf("Auth     : %s\n",
                authOn ? T::c(T::GREEN, "ON (send a valid token)").c_str()
                       : T::c(T::YELLOW, "OFF (open). Enable: nxfire token init").c_str());
    std::printf("\nLocal endpoint:\n");
    std::printf("  POST http://127.0.0.1:%d/       send JSON command\n", port);
    std::printf("  GET  http://127.0.0.1:%d/health server status\n\n", port);
    std::printf("Authentication (when Auth is ON):\n");
    std::printf("  Header   : Authorization: Bearer nxf_...\n");
    std::printf("  Query    : ?token=nxf_...  (appended to the path)\n");
    std::printf("  Note     : GET /health needs no token\n\n");
    std::printf("Example (curl):\n");
    if (authOn) {
        std::printf("  curl -X POST http://127.0.0.1:%d/ \\\n", port);
        std::printf("       -H \"Content-Type: application/json\" \\\n");
        std::printf("       -H \"Authorization: Bearer nxf_...\" \\\n");
        std::printf("       -d \"{\\\"op\\\":\\\"set\\\",\\\"path\\\":\\\"/users/budi\\\",\\\"value\\\":{\\\"name\\\":\\\"Budi\\\"}}\"\n\n");
    } else {
        std::printf("  curl -X POST http://127.0.0.1:%d/ -H \"Content-Type: application/json\" \\\n", port);
        std::printf("       -d \"{\\\"op\\\":\\\"set\\\",\\\"path\\\":\\\"/users/budi\\\",\\\"value\\\":{\\\"name\\\":\\\"Budi\\\"}}\"\n\n");
    }
    std::printf("Operations: set, get, update, push, remove\n");
    std::printf("Token ops : token_list, token_create, token_rotate, token_delete (admin only)\n\n");
    if (authOn) {
        std::printf("Manage tokens: nxfire token init|create|rotate|delete|list\n\n");
    }
    return 0;
}

int cmdSetup() {
    Nxfire db;
    std::string project, region, secret, apiKey;
    int port = 9026;

    if (db.open(g_config)) {
        std::printf("Current config: %s (port %d)\n", db.firebaseUrl().c_str(), db.port());
    }
    std::printf("\nNxfireDB setup (Enter = keep default)\n");

    auto ask = [](const char* label, std::string& out) {
        std::printf("%s", label);
        std::string line;
        std::getline(std::cin, line);
        line = trim(line);
        if (!line.empty()) out = line;
    };

    ask("project (e.g. my-app-default-rtdb) : ", project);
    ask("region  (e.g. asia-southeast1)     : ", region);
    ask("api_key (optional)                 : ", apiKey);
    ask("secret  (optional)                 : ", secret);

    {
        std::printf("port    [%d] : ", port);
        std::string line;
        std::getline(std::cin, line);
        line = trim(line);
        if (!line.empty()) port = std::atoi(line.c_str());
    }

    if (project.empty() && secret.empty()) {
        std::printf("'project' is required. Aborted.\n");
        return 1;
    }

    std::ofstream out(g_config, std::ios::binary);
    if (!out) { std::printf("Cannot write '%s'\n", g_config.c_str()); return 1; }
    out << "{\n";
    out << "  \"project\": \"" << project << "\",\n";
    out << "  \"region\":  \"" << region << "\",\n";
    out << "  \"api_key\": \"" << apiKey << "\",\n";
    out << "  \"secret\":  \"" << secret << "\",\n";
    out << "  \"port\":    " << port << "\n";
    out << "}\n";
    std::printf("Saved to %s\n", g_config.c_str());
    return 0;
}

int cmdServer() {
    std::string exe = g_exeDir + "nxfire-server";
#ifdef _WIN32
    exe += ".exe";
#endif
    if (!std::filesystem::exists(exe)) {
        std::printf("Not found: '%s'.\n", exe.c_str());
        std::printf("Make sure nxfire-server is in the same folder as nxfire.\n");
        return 1;
    }

    std::string cfg;
    try {
        cfg = std::filesystem::absolute(g_config).string();
    } catch (...) {
        cfg = g_config;
    }

    std::printf("Running: %s --config %s\n", exe.c_str(), cfg.c_str());
    std::printf("(Ctrl+C to stop the server)\n\n");
    std::fflush(stdout);

#ifdef _WIN32
    // Jalankan langsung (tanpa shell) agar path berkutip aman.
    const char* argv[] = { exe.c_str(), "--config", cfg.c_str(), nullptr };
    intptr_t rc = _spawnv(_P_WAIT, exe.c_str(), argv);
    if (rc == -1) {
        std::printf("Failed to start server.\n");
        return 1;
    }
    return (int)rc;
#else
    std::string cmd = "\"" + exe + "\" --config \"" + cfg + "\"";
    return std::system(cmd.c_str());
#endif
}

// Start the server in the background (no window) so it keeps running.
int cmdStart() {
    const int port = configPort();
    if (portListening(port)) {
        std::printf("%sServer is already running%s on port %d.\n",
                    T::c(T::GREEN).c_str(), T::c(T::RESET).c_str(), port);
        return 0;
    }

    const std::string exe = serverExePath();
    if (!std::filesystem::exists(exe)) {
        std::printf("Not found: '%s'.\n", exe.c_str());
        std::printf("Make sure nxfire-server is in the same folder as nxfire.\n");
        return 1;
    }
    const std::string cfg = absConfig();

    std::printf("Starting server (background)...\n");
#ifdef _WIN32
    std::string cmd = "\"" + exe + "\" --config \"" + cfg + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> m(cmd.begin(), cmd.end());
    m.push_back('\0');
    if (!CreateProcessA(nullptr, m.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::printf("Failed to start server.\n");
        return 1;
    }
    std::ofstream(pidFile()) << pi.dwProcessId << "\n";
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        std::string cfgArg = cfg;
        const char* argv[] = { exe.c_str(), "--config", cfgArg.c_str(), nullptr };
        execv(exe.c_str(), const_cast<char* const*>(argv));
        _exit(1);
    }
    if (pid < 0) { std::printf("Failed to start server.\n"); return 1; }
    std::ofstream(pidFile()) << pid << "\n";
#endif

    for (int i = 0; i < 50; ++i) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
        if (portListening(port)) {
            std::printf("%sServer running%s at %shttp://127.0.0.1:%d%s  %s(config: %s)%s\n",
                        T::c(T::GREEN).c_str(), T::c(T::RESET).c_str(),
                        T::c(T::CYAN).c_str(), port, T::c(T::RESET).c_str(),
                        T::c(T::GRAY).c_str(), cfg.c_str(), T::c(T::RESET).c_str());
            return 0;
        }
    }
    std::printf("Server did not respond on port %d. Check the config.\n", port);
    return 1;
}

// Stop the running server.
int cmdStop() {
    const int port = configPort();
    if (!portListening(port)) {
        std::error_code ec;
        std::filesystem::remove(pidFile(), ec);
        std::printf("Server is not running.\n");
        return 0;
    }

    std::ifstream in(pidFile());
    long pid = 0;
    if (!(in >> pid) || pid <= 0) {
        std::printf("Server is running but PID is unknown.\n");
        return 1;
    }

    std::printf("Stopping server (PID %ld)...\n", pid);
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (h) {
        TerminateProcess(h, 0);
        CloseHandle(h);
    }
#else
    kill((pid_t)pid, SIGTERM);
#endif

    for (int i = 0; i < 30; ++i) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
        if (!portListening(port)) break;
    }

    std::error_code ec;
    std::filesystem::remove(pidFile(), ec);
    if (portListening(port)) {
        std::printf("%sServer still responds%s. Try again.\n",
                    T::c(T::RED).c_str(), T::c(T::RESET).c_str());
        return 1;
    }
    std::printf("%sServer stopped%s.\n",
                T::c(T::GREEN).c_str(), T::c(T::RESET).c_str());
    return 0;
}

// Show server status.
int cmdStatus() {
    Nxfire db;
    bool ok = db.open(g_config);
    const int port = ok ? db.port() : 9026;
    const bool up = portListening(port);
    std::string R = T::c(T::RESET);
    std::printf("%sFirebase  :%s %s\n", T::c(T::GRAY).c_str(), R.c_str(),
                (ok ? db.firebaseUrl() : std::string("(not configured)")).c_str());
    std::printf("%sConfig    :%s %s\n", T::c(T::GRAY).c_str(), R.c_str(), g_config.c_str());
    std::printf("%sPort      :%s %d\n", T::c(T::GRAY).c_str(), R.c_str(), port);
    std::printf("%sServer    :%s %s\n", T::c(T::GRAY).c_str(), R.c_str(),
                (up ? T::c(T::GREEN, "RUNNING") : T::c(T::RED, "stopped")).c_str());
    std::printf("%sAuto-start:%s %s\n", T::c(T::GRAY).c_str(), R.c_str(),
                (autoStartEnabled() ? T::c(T::GREEN, "ON") : T::c(T::GRAY, "OFF")).c_str());

    if (up) {
        std::ifstream in(pidFile());
        long pid = 0;
        if (in >> pid) std::printf("%sPID       :%s %s\n", T::c(T::GRAY).c_str(), R.c_str(),
                                   std::to_string(pid).c_str());
    }
    return up ? 0 : 1;
}

// Show/toggle auto-start at boot.
int cmdAutostart(const std::string& arg) {
    if (arg == "on" || arg == "enable" || arg == "1") {
        if (enableAutoStart()) {
            std::printf("%sAuto-start ON%s: %s\n",
                        T::c(T::GREEN).c_str(), T::c(T::RESET).c_str(), autoStartPath().c_str());
            return 0;
        }
        std::printf("Failed to enable auto-start.\n");
        return 1;
    }
    if (arg == "off" || arg == "disable" || arg == "0") {
        disableAutoStart();
        std::printf("%sAuto-start OFF%s.\n", T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        return 0;
    }
    std::printf("Auto-start: %s\n", autoStartEnabled() ? "ON" : "OFF");
    std::printf("Path: %s\n", autoStartPath().c_str());
    std::printf("Change: nxfire autostart on|off\n");
    return 0;
}

int cmdConfig() {
    Nxfire db;
    if (!db.open(g_config)) {
        std::printf("Config '%s' missing or invalid: %s\n",
                    g_config.c_str(), db.lastError().c_str());
        return 1;
    }
    std::printf("Config   : %s\n", g_config.c_str());
    std::printf("Firebase : %s\n", db.firebaseUrl().c_str());
    std::printf("Port     : %d\n", db.port());
    return 0;
}

// --------------------------------------------------------------------------
// Kelola token akses (nxfire token ...)
// --------------------------------------------------------------------------
std::string tokenStorePath() {
    size_t p = g_config.find_last_of("/\\");
    std::string dir = (p == std::string::npos) ? std::string() : g_config.substr(0, p + 1);
    std::string name = (p == std::string::npos) ? g_config : g_config.substr(p + 1);
    size_t dot = name.find_last_of('.');
    std::string base = (dot == std::string::npos) ? name : name.substr(0, dot);
    return dir + base + ".tokens.json";
}

int cmdToken(const std::string& sub, const std::string& arg) {
    nxfiretokens::TokenStore store(tokenStorePath());

    if (sub == "init") {
        std::string tok = store.ensureOwner();
        if (tok.empty()) {
            std::printf("Auth is already enabled (%s).\n", tokenStorePath().c_str());
            return 1;
        }
        if (!store.save()) {
            std::printf("Cannot write '%s'\n", tokenStorePath().c_str());
            return 1;
        }
        std::printf("%sAuth enabled.%s Admin token (save it! shown only once):\n",
                    T::c(T::GREEN).c_str(), T::c(T::RESET).c_str());
        std::printf("  %s\n", tok.c_str());
        std::printf("Usage: -H \"Authorization: Bearer <token>\"\n");
        std::printf("Restart the server to apply.\n");
        return 0;
    }

    if (sub == "create") {
        if (arg.empty()) { std::printf("Usage: nxfire token create <user> [admin|user]\n"); return 2; }
        std::string role = "user";
        // arg bisa "<user> admin" - pakai arg kedua bila ada.
        std::string user = arg;
        size_t sp = arg.find(' ');
        if (sp != std::string::npos) {
            user = arg.substr(0, sp);
            std::string r = arg.substr(sp + 1);
            if (r == "admin") role = "admin";
        }
        std::string tok = store.create(user, role);
        if (tok.empty()) {
            std::printf("User '%s' already has a token (use: nxfire token rotate %s)\n",
                        user.c_str(), user.c_str());
            return 1;
        }
        store.save();
        std::printf("Token for %s%s (%s shown only once):\n", user.c_str(),
                    (role == "admin" ? " [admin]" : ""), "save it");
        std::printf("  %s\n", tok.c_str());
        std::printf("Restart the server to apply.\n");
        return 0;
    }

    if (sub == "rotate") {
        if (arg.empty()) { std::printf("Usage: nxfire token rotate <user>\n"); return 2; }
        std::string tok = store.rotate(arg);
        if (tok.empty()) { std::printf("No token for user '%s'\n", arg.c_str()); return 1; }
        store.save();
        std::printf("New token for %s (old one is now invalid, save it!):\n", arg.c_str());
        std::printf("  %s\n", tok.c_str());
        std::printf("Restart the server to apply.\n");
        return 0;
    }

    if (sub == "delete") {
        if (arg.empty()) { std::printf("Usage: nxfire token delete <user>\n"); return 2; }
        if (!store.remove(arg)) { std::printf("No token for user '%s'\n", arg.c_str()); return 1; }
        store.save();
        std::printf("%sToken for %s deleted.%s Restart the server to apply.\n",
                    T::c(T::GREEN).c_str(), arg.c_str(), T::c(T::RESET).c_str());
        return 0;
    }

    if (sub == "list" || sub.empty()) {
        auto all = store.list();
        if (all.empty()) {
            std::printf("Auth is OFF (no token file). Enable: nxfire token init\n");
            return 0;
        }
        std::printf("%-20s %-6s %s\n", "USER", "ROLE", "CREATED");
        for (const auto& t : all) {
            char buf[32];
            std::time_t tt = (std::time_t)t.createdAt;
            std::tm* tmv = std::localtime(&tt);
            if (tmv) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tmv);
            else buf[0] = 0;
            std::printf("%-20s %-6s %s\n", t.user.c_str(), t.role.c_str(), buf);
        }
        return 0;
    }

    std::printf("Unknown token subcommand: '%s'\n", sub.c_str());
    std::printf("Usage: nxfire token init|create|rotate|delete|list\n");
    return 2;
}

int menuToken() {
    for (;;) {
        nxfiretokens::TokenStore store(tokenStorePath());
        const bool authOn = !store.empty();
        std::printf("\n%sAccess tokens%s  (auth: %s)\n",
                    T::c(T::WHITE).c_str(), T::c(T::RESET).c_str(),
                    authOn ? T::c(T::GREEN, "ON").c_str()
                           : T::c(T::YELLOW, "OFF").c_str());
        std::printf("  Store: %s\n", tokenStorePath().c_str());
        std::printf("%s---------------------------------------------------%s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        auto opt = [](const char* n, const char* text, const char* hint = "") {
            std::printf("  %s%s%s. %-34s %s%s%s\n",
                        T::c(T::CYAN).c_str(), n, T::c(T::RESET).c_str(), text,
                        T::c(T::GRAY).c_str(), hint, T::c(T::RESET).c_str());
        };
        opt("1", "List tokens", "show all users + roles");
        if (!authOn) {
            opt("2", "Enable auth", "create the first admin token");
        } else {
            std::printf("  %s2. %-34s %s\n",
                        T::c(T::GRAY).c_str(), "Enable auth (already ON)",
                        T::c(T::RESET).c_str());
        }
        opt("3", "Create token for a user", "e.g. budi, or budi admin");
        opt("4", "Rotate a user's token", "invalidate old, issue new");
        opt("5", "Delete a user's token", "revoke access");
        opt("0", "Back to main menu");
        std::printf("%s---------------------------------------------------%s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        if (!authOn) {
            std::printf("  %sTip: pick 2 first to turn on authentication, then add users with 3.%s\n",
                        T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        } else {
            std::printf("  %sTip: tokens are shown only ONCE - save them. Restart the server to apply.%s\n",
                        T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        }
        std::printf("  %s%sChoose:%s ", T::c(T::ORANGE).c_str(),
                    T::c(T::WHITE).c_str(), T::c(T::RESET).c_str());

        std::string line;
        if (!std::getline(std::cin, line)) return 0;
        line = trim(line);
        if (line.empty()) continue;

        if (line == "0" || line == "q") return 0;
        else if (line == "1") cmdToken("list", "");
        else if (line == "2") {
            if (authOn) {
                std::printf("  Auth is already ON (%s).\n", tokenStorePath().c_str());
                continue;
            }
            std::printf("  Enable auth now? This creates the first admin token. (y/n): ");
            std::string ans;
            std::getline(std::cin, ans);
            if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) cmdToken("init", "");
        }
        else if (line == "3") {
            std::printf("  user name: ");
            std::string user;
            std::getline(std::cin, user);
            user = trim(user);
            if (user.empty()) { std::printf("  (cancelled)\n"); continue; }
            std::printf("  role [user|admin] (Enter = user): ");
            std::string role;
            std::getline(std::cin, role);
            role = trim(role);
            cmdToken("create", user + (role.empty() ? "" : " " + role));
        }
        else if (line == "4") {
            std::printf("  user name: ");
            std::string user;
            std::getline(std::cin, user);
            user = trim(user);
            if (user.empty()) { std::printf("  (cancelled)\n"); continue; }
            cmdToken("rotate", user);
        }
        else if (line == "5") {
            std::printf("  user name: ");
            std::string user;
            std::getline(std::cin, user);
            user = trim(user);
            if (user.empty()) { std::printf("  (cancelled)\n"); continue; }
            std::printf("  Delete token for '%s'? (y/n): ", user.c_str());
            std::string ans;
            std::getline(std::cin, ans);
            if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) cmdToken("delete", user);
        }
    }
}

// --------------------------------------------------------------------------
// Menu interaktif
// --------------------------------------------------------------------------
int menu() {
    for (;;) {
        Nxfire db;
        bool ok = db.open(g_config);

        banner();
        const int p = ok ? db.port() : 9026;
        const bool up = portListening(p);
        std::printf("  %s Firebase:%s %s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str(),
                    (ok ? db.firebaseUrl() : std::string("(not configured)")).c_str());
        std::printf("  %s Config  :%s %s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str(), g_config.c_str());
        std::printf("  %s Server  :%s %s   %s(port %d, auto-start %s)%s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str(),
                    (up ? T::c(T::GREEN, "RUNNING") : T::c(T::RED, "stopped")).c_str(),
                    T::c(T::GRAY).c_str(), p,
                    (autoStartEnabled() ? T::c(T::GREEN, "ON") : T::c(T::GRAY, "OFF")).c_str(),
                    T::c(T::RESET).c_str());
        std::printf("%s---------------------------------------------------%s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        auto opt = [](const char* n, const char* text) {
            std::printf("  %s%s%s. %s\n", T::c(T::CYAN).c_str(), n, T::c(T::RESET).c_str(), text);
        };
        opt("1", "Start server (background, keeps running)");
        opt("2", "Stop server");
        opt("3", "Server status");
        opt("4", "Auto-start on boot (ON/OFF)");
        opt("5", "Setup (create/edit nxfire.json)");
        opt("6", "Test Firebase connection");
        opt("7", "Run server (foreground, debug)");
        opt("8", "API info (for developers)");
        opt("9", "Show config");
        opt("10", "Tokens (manage access tokens)");
        opt("0", "Exit");
        std::printf("%s---------------------------------------------------%s\n",
                    T::c(T::GRAY).c_str(), T::c(T::RESET).c_str());
        std::printf("  %s%sChoose:%s ", T::c(T::ORANGE).c_str(),
                    T::c(T::WHITE).c_str(), T::c(T::RESET).c_str());

        std::string line;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);

        if (line == "1") cmdStart();
        else if (line == "2") cmdStop();
        else if (line == "3") cmdStatus();
        else if (line == "4") {
            if (autoStartEnabled()) {
                std::printf("  Disable auto-start? (y/n): ");
                std::string ans;
                std::getline(std::cin, ans);
                if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) cmdAutostart("off");
            } else {
                std::printf("  Enable auto-start (server starts on boot)? (y/n): ");
                std::string ans;
                std::getline(std::cin, ans);
                if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) cmdAutostart("on");
            }
        }
        else if (line == "5") cmdSetup();
        else if (line == "6") cmdTest();
        else if (line == "7") cmdServer();
        else if (line == "8") cmdInfo();
        else if (line == "9") cmdConfig();
        else if (line == "10" || line == "t" || line == "T") menuToken();
        else if (line == "0" || line == "q") break;

        if (line != "7") {
            std::printf("\nPress Enter to continue...");
            std::getline(std::cin, line);
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    g_exeDir = dirOf(argv[0] ? argv[0] : "");

    // --config
    int colorSet = 0; // 0=auto, 1=always, 2=never
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) g_config = argv[++i];
        else if (a == "--no-color" || a == "--plain") colorSet = 2;
        else if (a == "--color") colorSet = 1;
    }
    if (colorSet == 1) T::set(true);
    else if (colorSet == 2) T::set(false);
    else T::autoDetect();

    std::string cmd = (argc > 1 && argv[1][0] != '-') ? argv[1] : "";

    if (cmd == "test")      return cmdTest();
    if (cmd == "info")      return cmdInfo();
    if (cmd == "setup")     return cmdSetup();
    if (cmd == "start")     return cmdStart();
    if (cmd == "stop")      return cmdStop();
    if (cmd == "status")    return cmdStatus();
    if (cmd == "autostart") return cmdAutostart(argc > 2 ? argv[2] : "");
    if (cmd == "token")     return cmdToken(argc > 2 ? argv[2] : "",
                                            argc > 3 ? argv[3] : "");
    if (cmd == "server")    return cmdServer();
    if (cmd == "config")    return cmdConfig();
    if (cmd == "-h" || cmd == "--help") {
        std::printf(
            "nxfire - NxfireDB menu & tools\n\n"
            "Usage:\n"
            "  nxfire              open menu\n"
            "  nxfire start        start server in the background (keeps running)\n"
            "  nxfire stop         stop the running server\n"
            "  nxfire status       show whether the server is running\n"
            "  nxfire autostart    show/toggle auto-start (nxfire autostart on|off)\n"
            "  nxfire token        manage access tokens: init|create|rotate|delete|list\n"
            "                      (or press 't' in the menu)\n"
            "  nxfire test         test the Firebase connection\n"
            "  nxfire info         API info (for developers)\n"
            "  nxfire setup        create/edit nxfire.json\n"
            "  nxfire config       show config\n"
            "  nxfire server       run nxfire-server (foreground, debug)\n\n"
            "Options: --config FILE (default nxfire.json)\n");
        return 0;
    }

    if (!cmd.empty()) {
        std::printf("%s[?]%s unknown command: '%s'\n",
                    T::c(T::YELLOW).c_str(), T::c(T::RESET).c_str(), cmd.c_str());
        if (cmd.rfind("start", 0) == 0)        std::printf("    did you mean 'nxfire start'?\n");
        else if (cmd.rfind("stop", 0) == 0)    std::printf("    did you mean 'nxfire stop'?\n");
        else if (cmd.rfind("status", 0) == 0)  std::printf("    did you mean 'nxfire status'?\n");
        else if (cmd.rfind("server", 0) == 0)  std::printf("    did you mean 'nxfire server'?\n");
        else if (cmd.rfind("setup", 0) == 0)   std::printf("    did you mean 'nxfire setup'?\n");
        else if (cmd.rfind("test", 0) == 0)    std::printf("    did you mean 'nxfire test'?\n");
        else if (cmd.rfind("info", 0) == 0)    std::printf("    did you mean 'nxfire info'?\n");
        std::printf("\n");
    }

    return menu();
}
