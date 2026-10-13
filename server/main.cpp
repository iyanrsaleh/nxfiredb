// nxfire-server
// Server lokal NxfireDB: terima JSON lokal (POST /), teruskan ke Firebase
// Realtime Database REST API, kembalikan balasan seragam. Plus GET /health.

#include <nxfire/Nxfire.h>

#include "conn.h"
#include "ws.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using nxfire::Json;
using nxfire::Nxfire;
using nxfire::QueryOpts;
using nxfire::Result;
using nxfireconn::Conn;

namespace {

// --------------------------------------------------------------------------
// Konfigurasi & metrik
// --------------------------------------------------------------------------
std::string g_configPath = "nxfire.json";
std::string g_firebaseUrl;
int         g_port = 9026;
std::string g_host = "127.0.0.1";

int  g_workers        = 0;    // 0 = otomatis
int  g_maxConnections = 256;
uint64_t g_maxBody    = 64ull * 1024ull * 1024ull;

std::atomic<uint64_t> g_requests{0};
std::atomic<uint64_t> g_ok{0};
std::atomic<uint64_t> g_errors{0};
long long g_startedAt = 0;
int       g_wsPollMs  = 1000;   // jeda polling Firebase untuk langganan WS
bool      g_offlineOn = true;   // antrian offline aktif?

// Nilai terakhir tiap path yang dilanggan (untuk mendeteksi perubahan).
std::mutex g_lastMu;
std::map<std::string, std::string> g_lastValue;

// --------------------------------------------------------------------------
// Cache GET di memori (opsional; aktif bila cache_ttl > 0).
// --------------------------------------------------------------------------
class Cache {
public:
    bool get(const std::string& key, std::string& body) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        if (it->second.expiry < std::time(nullptr)) { map_.erase(it); return false; }
        body = it->second.body;
        return true;
    }
    void put(const std::string& key, const std::string& body, int ttl) {
        std::lock_guard<std::mutex> lk(mu_);
        map_[key] = Entry{body, std::time(nullptr) + ttl};
    }
    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        map_.clear();
    }

private:
    struct Entry { std::string body; std::time_t expiry; };
    std::map<std::string, Entry> map_;
    std::mutex mu_;
};
Cache g_cache;

// --------------------------------------------------------------------------
// Antrian offline: tulis yang gagal karena Firebase tak terjangkau disimpan
// ke file dan dicoba ulang sampai berhasil.
// --------------------------------------------------------------------------
class OfflineQueue {
public:
    void load(const std::string& file) {
        std::lock_guard<std::mutex> lk(mu_);
        file_ = file;
        q_.clear();
        std::ifstream in(file, std::ios::binary);
        if (!in) return;
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Json arr = Json::parse(text);
        if (arr.isArray())
            for (const auto& e : arr.asArray()) q_.push_back(e);
    }

    void enqueue(const Json& op) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(op);
        saveLocked();
    }
    bool front(Json& out) {
        std::lock_guard<std::mutex> lk(mu_);
        if (q_.empty()) return false;
        out = q_.front();
        return true;
    }
    void dropFront() {
        std::lock_guard<std::mutex> lk(mu_);
        if (!q_.empty()) q_.pop_front();
        saveLocked();
    }
    size_t size() {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

private:
    void saveLocked() {
        if (file_.empty()) return;
        Json arr = Json::array();
        for (const auto& e : q_) arr.push_back(e);
        std::ofstream out(file_, std::ios::binary | std::ios::trunc);
        out << arr.dump();
    }
    std::string file_;
    std::deque<Json> q_;
    std::mutex mu_;
};
OfflineQueue g_queue;
std::string  g_queueFile = "nxfire.queue.json";
int          g_retryMs   = 2000;

// --------------------------------------------------------------------------
// Utilitas
// --------------------------------------------------------------------------

// Jalankan satu perintah lokal terhadap Firebase; hasil sebagai Json.
Json dispatch(Nxfire& db, const Json& req) {
    Json out = Json::object();
    if (!req.isObject()) {
        out.set("ok", Json(false));
        out.set("error", Json("bad_request"));
        out.set("hint", Json("body harus berupa objek JSON"));
        return out;
    }

    Result r = db.execute(req);

    out.set("ok", Json(r.ok));
    if (r.ok) {
        out.set("data", r.data);
        Json meta = Json::object();
        meta.set("latency_ms", Json(int64_t(db.lastLatencyMs())));
        out.set("meta", meta);
    } else {
        out.set("error", Json(r.error));
        if (!r.hint.empty()) out.set("hint", Json(r.hint));
        out.set("http_status", Json(int64_t(r.httpStatus)));
    }
    return out;
}

void sendResponse(Conn* conn, int status, const std::string& statusText,
                  const std::string& contentType, const std::string& body) {
    std::string resp = "HTTP/1.1 " + std::to_string(status) + " " + statusText + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "Access-Control-Allow-Headers: Content-Type\r\n";
    resp += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    conn->writeAll(resp);
}

void sendJson(Conn* conn, int status, const std::string& statusText, const Json& j) {
    sendResponse(conn, status, statusText, "application/json", j.dump());
}

// --------------------------------------------------------------------------
// WebSocket lokal: klien subscribe ke path; server memantau Firebase dan
// mendorong perubahan.
// --------------------------------------------------------------------------
void handleWebSocket(Conn* conn, const std::string& head) {
    if (!nxfirews::handshake(conn, head)) return;

    nxfirews::Client* c = nxfirews::hub().add(conn);
    nxfirews::sendText(conn, R"({"type":"welcome","service":"NxfireDB"})");

    Nxfire db;
    db.open(g_configPath);

    int opcode = 0;
    std::string payload;
    while (nxfirews::readFrame(conn, opcode, payload)) {
        if (opcode == nxfirews::OP_CLOSE) break;
        if (opcode == nxfirews::OP_PING) { nxfirews::sendText(conn, payload); continue; }
        if (opcode != nxfirews::OP_TEXT) continue;

        std::string perr;
        Json msg = Json::parse(payload, &perr);
        if (!perr.empty() || !msg.isObject()) continue;

        const std::string sub   = msg.at("subscribe").asString();
        const std::string unsub = msg.at("unsubscribe").asString();

        if (!sub.empty()) {
            nxfirews::hub().subscribe(c, sub);
            nxfirews::sendText(conn, R"({"type":"subscribed","path":")" + sub + R"("})");
            // Kirim nilai awal (seperti on('value') Firebase yang langsung menyala).
            Result r = db.get(sub);
            Json m = Json::object();
            m.set("type", Json("value"));
            m.set("path", Json(sub));
            if (r.ok) m.set("data", r.data);
            else { m.set("ok", Json(false)); m.set("error", Json(r.error)); }
            nxfirews::sendText(conn, m.dump());
            // Semai nilai terakhir = nilai awal yang baru dikirim, agar poller
            // membandingkan terhadap yang sudah dilihat klien (hindari race).
            {
                std::lock_guard<std::mutex> lk(g_lastMu);
                g_lastValue[sub] = r.ok ? r.data.dump() : std::string("null");
            }
        } else if (!unsub.empty()) {
            nxfirews::hub().unsubscribe(c, unsub);
            nxfirews::sendText(conn, R"({"type":"unsubscribed","path":")" + unsub + R"("})");
        }
    }
    nxfirews::hub().remove(c);
}

// Poller: tiap g_wsPollMs, ambil tiap path yang dilanggan dari Firebase dan
// kirim notifikasi bila nilainya berubah.
void pollLoop() {
    Nxfire db;
    db.open(g_configPath);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_wsPollMs));
        std::set<std::string> paths = nxfirews::hub().paths();
        if (paths.empty()) continue;
        for (const auto& p : paths) {
            Result r = db.get(p);
            if (!r.ok) continue;
            const std::string cur = r.data.dump();
            bool changed = false;
            {
                std::lock_guard<std::mutex> lk(g_lastMu);
                auto it = g_lastValue.find(p);
                if (it == g_lastValue.end()) {
                    g_lastValue[p] = cur;   // seed; nilai awal sudah dikirim saat subscribe
                    continue;
                }
                if (it->second != cur) { it->second = cur; changed = true; }
            }
            if (changed) {
                Json m = Json::object();
                m.set("type", Json("change"));
                m.set("path", Json(p));
                m.set("data", Json::parse(cur));
                nxfirews::hub().notify(p, m.dump());
            }
        }
    }
}

// Retry antrian offline: coba kirim ulang tulis yang tertunda.
void retryLoop() {
    Nxfire db;
    db.open(g_configPath);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_retryMs));
        for (;;) {
            Json op;
            if (!g_queue.front(op)) break;
            Json res = dispatch(db, op);
            if (res.at("ok").asBool()) { g_queue.dropFront(); continue; }
            if (res.at("error").asString() == "network_error") break;  // coba lagi nanti
            g_queue.dropFront();  // error permanen (mis. permission): buang agar tak macet
        }
    }
}

// --------------------------------------------------------------------------
// Tangani satu koneksi HTTP.
// --------------------------------------------------------------------------
void handleConnection(Conn* conn) {
    std::string buffer;
    char chunk[4096];

    size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        int n = conn->read(chunk, sizeof(chunk));
        if (n <= 0) return;
        buffer.append(chunk, n);
        if (buffer.size() > 64 * 1024) return;  // header kebesaran
    }

    std::string head = buffer.substr(0, headerEnd);
    std::string body = buffer.substr(headerEnd + 4);

    std::string method, path;
    {
        size_t sp1 = head.find(' ');
        size_t sp2 = head.find(' ', sp1 + 1);
        method = head.substr(0, sp1);
        path = (sp1 != std::string::npos)
                   ? head.substr(sp1 + 1, (sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1))
                   : "/";
    }

    // WebSocket di /ws
    {
        std::string pl = path.substr(0, path.find('?'));
        if (pl == "/ws") {
            handleWebSocket(conn, head);
            return;
        }
    }

    size_t contentLength = 0;
    {
        std::string lower = head;
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        size_t p = lower.find("content-length:");
        if (p != std::string::npos)
            contentLength = std::strtoul(head.c_str() + p + 15, nullptr, 10);
    }
    if (g_maxBody > 0 && contentLength > g_maxBody) {
        Json out = Json::object();
        out.set("ok", Json(false));
        out.set("error", Json("payload_too_large"));
        sendJson(conn, 413, "Payload Too Large", out);
        return;
    }
    while (body.size() < contentLength) {
        int n = conn->read(chunk, sizeof(chunk));
        if (n <= 0) break;
        buffer.append(chunk, n);
        body = buffer.substr(headerEnd + 4);
    }
    if (body.size() > contentLength) body.resize(contentLength);

    // OPTIONS (CORS preflight).
    if (method == "OPTIONS") {
        sendResponse(conn, 204, "No Content", "text/plain", "");
        return;
    }

    // GET /health
    if (method == "GET" && path == "/health") {
        Json out = Json::object();
        out.set("ok", Json(true));
        out.set("service", Json("NxfireDB"));
        out.set("firebase", Json(g_firebaseUrl));
        out.set("port", Json(int64_t(g_port)));
        out.set("uptime_sec", Json(int64_t(std::time(nullptr) - g_startedAt)));
        Json m = Json::object();
        m.set("requests", Json(int64_t(g_requests.load())));
        m.set("ok", Json(int64_t(g_ok.load())));
        m.set("errors", Json(int64_t(g_errors.load())));
        m.set("ws_clients", Json(int64_t(nxfirews::hub().clientCount())));
        out.set("metrics", m);
        sendJson(conn, 200, "OK", out);
        return;
    }

    // POST /  dan  POST /query
    if (method == "POST" && (path == "/" || path == "/query")) {
        g_requests.fetch_add(1);

        std::string perr;
        Json req = Json::parse(body, &perr);
        if (!perr.empty()) {
            g_errors.fetch_add(1);
            Json out = Json::object();
            out.set("ok", Json(false));
            out.set("error", Json("bad_request"));
            out.set("hint", Json("JSON tidak valid: " + perr));
            sendJson(conn, 400, "Bad Request", out);
            return;
        }

        Nxfire db;
        if (!db.open(g_configPath)) {
            g_errors.fetch_add(1);
            Json out = Json::object();
            out.set("ok", Json(false));
            out.set("error", Json("config_error"));
            out.set("hint", Json(db.lastError()));
            sendJson(conn, 500, "Internal Server Error", out);
            return;
        }

        const std::string op = req.at("op").asString();
        const int  ttl     = db.cacheTtl();
        const bool isGet   = (op == "get" || op == "select");
        const bool isWrite = (op == "set"    || op == "put"    || op == "upsert" ||
                              op == "update" || op == "patch"  ||
                              op == "push"   || op == "insert" || op == "add" ||
                              op == "remove" || op == "delete" || op == "del");

        // Cache GET (bila cache_ttl > 0). Kunci = seluruh isi perintah.
        std::string ckey;
        if (ttl > 0 && isGet) {
            ckey = req.dump();
            std::string cached;
            if (g_cache.get(ckey, cached)) {
                g_ok.fetch_add(1);
                sendResponse(conn, 200, "OK", "application/json", cached);
                return;
            }
        }

        Json res = dispatch(db, req);

        // Antrian offline: tulis yang gagal karena Firebase tak terjangkau
        // disimpan lalu dicoba ulang (bukan sekadar error).
        if (isWrite && db.offlineQueue() &&
            !res.at("ok").asBool() &&
            res.at("error").asString() == "network_error") {
            g_queue.enqueue(req);
            Json out = Json::object();
            out.set("ok", Json(true));
            out.set("queued", Json(true));
            out.set("data", Json());
            Json meta = Json::object();
            meta.set("queued", Json(true));
            meta.set("queue_size", Json(int64_t(g_queue.size())));
            meta.set("hint", Json("Firebase tak terjangkau; perintah masuk antrian & akan dikirim ulang"));
            out.set("meta", meta);
            g_ok.fetch_add(1);
            sendResponse(conn, 200, "OK", "application/json", out.dump());
            return;
        }

        const bool ok = res.at("ok").asBool();
        if (ok) g_ok.fetch_add(1); else g_errors.fetch_add(1);

        const std::string outBody = res.dump();
        if (ttl > 0 && isGet && ok) g_cache.put(ckey, outBody, ttl);
        if (ok && isWrite) g_cache.clear();   // tulis membatalkan cache

        sendResponse(conn, ok ? 200 : 400, ok ? "OK" : "Bad Request",
                     "application/json", outBody);
        return;
    }

    // Selain itu: 404.
    Json out = Json::object();
    out.set("ok", Json(false));
    out.set("error", Json("not_found"));
    out.set("hint", Json("endpoint tidak dikenal: " + method + " " + path));
    sendJson(conn, 404, "Not Found", out);
}

// --------------------------------------------------------------------------
// Thread pool.
// --------------------------------------------------------------------------
class ConnectionPool {
public:
    explicit ConnectionPool(int workers, int maxConnections)
        : maxConnections_(maxConnections) {
        if (workers < 1) workers = 1;
        for (int i = 0; i < workers; ++i)
            threads_.emplace_back([this] { workerLoop(); });
    }

    ~ConnectionPool() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) if (t.joinable()) t.join();
    }

    void submit(sock_t sock) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if ((int)queue_.size() + active_ >= maxConnections_) {
                Conn tmp(sock);
                tmp.writeAll(
                    "HTTP/1.1 503 Service Unavailable\r\n"
                    "Content-Type: application/json\r\n"
                    "Connection: close\r\n\r\n"
                    R"({"ok":false,"error":"server_busy"})");
                tmp.close();
                return;
            }
            queue_.push_back(sock);
        }
        cv_.notify_one();
    }

private:
    void workerLoop() {
        for (;;) {
            sock_t s;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                s = queue_.front();
                queue_.pop_front();
                ++active_;
            }
            Conn conn(s);
            handleConnection(&conn);
            conn.close();
            {
                std::lock_guard<std::mutex> lk(mu_);
                --active_;
            }
        }
    }

    int maxConnections_;
    std::vector<std::thread> threads_;
    std::deque<sock_t> queue_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    int active_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    bool portFromArg = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) g_configPath = argv[++i];
        else if (a == "--port" && i + 1 < argc) { g_port = std::atoi(argv[++i]); portFromArg = true; }
        else if (a == "--host" && i + 1 < argc) g_host = argv[++i];
        else if (a == "--workers" && i + 1 < argc) g_workers = std::atoi(argv[++i]);
        else if (a == "--max-connections" && i + 1 < argc) g_maxConnections = std::atoi(argv[++i]);
        else if (a == "--max-body" && i + 1 < argc) g_maxBody = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--ws-poll-ms" && i + 1 < argc) g_wsPollMs = std::atoi(argv[++i]);
        else if (a == "--retry-ms" && i + 1 < argc) g_retryMs = std::atoi(argv[++i]);
        else if (a == "-h" || a == "--help") {
            std::printf(
                "nxfire-server - server lokal NxfireDB\n\n"
                "Opsi:\n"
                "  --config FILE       konfigurasi (default nxfire.json)\n"
                "  --port N            port server lokal (default 9026 / dari config)\n"
                "  --host H            alamat bind (default 127.0.0.1)\n"
                "  --workers N         jumlah thread pekerja (0 = otomatis)\n"
                "  --max-connections N batas koneksi bersamaan (default 256)\n"
                "  --max-body N        batas ukuran body (byte)\n"
                "  --ws-poll-ms N      jeda polling realtime (ms; 0 = mati, default 1000)\n"
                "  --retry-ms N        jeda kirim ulang antrian offline (ms, default 2000)\n");
            return 0;
        }
    }

    // Muat konfigurasi sekali untuk validasi & info.
    {
        Nxfire db;
        if (!db.open(g_configPath)) {
            std::fprintf(stderr, "Gagal memuat konfigurasi '%s': %s\n",
                         g_configPath.c_str(), db.lastError().c_str());
            return 1;
        }
        g_firebaseUrl = db.firebaseUrl();
        if (!portFromArg) g_port = db.port();
        g_offlineOn = db.offlineQueue();
    }

    // Berkas antrian offline (di samping berkas konfigurasi).
    {
        size_t p = g_configPath.find_last_of("/\\");
        std::string dir = (p == std::string::npos) ? std::string() : g_configPath.substr(0, p + 1);
        g_queueFile = dir + "nxfire.queue.json";
        g_queue.load(g_queueFile);
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup gagal\n");
        return 1;
    }
#endif

    sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::fprintf(stderr, "Gagal membuat socket\n");
        return 1;
    }
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)g_port);
    if (g_host.empty() || g_host == "0.0.0.0")
        addr.sin_addr.s_addr = INADDR_ANY;
    else
        inet_pton(AF_INET, g_host.c_str(), &addr.sin_addr);

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::fprintf(stderr, "Gagal bind ke %s:%d\n", g_host.c_str(), g_port);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    if (listen(server, 32) != 0) {
        std::fprintf(stderr, "Gagal listen\n");
        return 1;
    }

    g_startedAt = std::time(nullptr);
    std::printf("NxfireDB server (HTTP) di http://%s:%d\n", g_host.c_str(), g_port);
    std::printf("Firebase: %s\n", g_firebaseUrl.c_str());
    std::printf("Config  : %s\n", g_configPath.c_str());
    std::printf("Antrian offline: %s\n", g_offlineOn ? "ON" : "OFF");
    std::printf("POST /  |  POST /query  |  GET /health  |  GET /ws (realtime)\n");
    std::fflush(stdout);

    int workers = g_workers > 0 ? g_workers
                                : (int)std::max<unsigned>(4u, std::thread::hardware_concurrency());
    ConnectionPool pool(workers, g_maxConnections);

    // Poller realtime: pantau path yang dilanggan & dorong perubahan.
    if (g_wsPollMs > 0) std::thread(pollLoop).detach();

    // Retry antrian offline.
    if (g_offlineOn) std::thread(retryLoop).detach();

    while (true) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peerLen = sizeof(peer);
#else
        socklen_t peerLen = sizeof(peer);
#endif
        sock_t client = accept(server, (sockaddr*)&peer, &peerLen);
        if (client == INVALID_SOCKET) continue;
        pool.submit(client);
    }

#ifdef _WIN32
    closesocket(server);
    WSACleanup();
#else
    ::close(server);
#endif
    return 0;
}
