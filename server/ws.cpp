#include "ws.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#endif

namespace nxfirews {

// ---------------------------------------------------------------------------
// Base64 (untuk handshake Sec-WebSocket-Accept).
// ---------------------------------------------------------------------------
static std::string base64(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (size_t i = 0; i < len; ++i) {
        val = (val << 8) + data[i];
        bits += 8;
        while (bits >= 0) {
            out.push_back(tbl[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(tbl[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// SHA-1 (khusus handshake WebSocket).
namespace {
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    void process(const unsigned char* block) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
                   (block[i * 4 + 2] << 8) | block[i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    std::string digest(const std::string& msg) {
        std::vector<unsigned char> data(msg.begin(), msg.end());
        uint64_t bitlen = (uint64_t)data.size() * 8;
        data.push_back(0x80);
        while (data.size() % 64 != 56) data.push_back(0x00);
        for (int i = 7; i >= 0; --i)
            data.push_back((unsigned char)((bitlen >> (i * 8)) & 0xFF));
        for (size_t i = 0; i < data.size(); i += 64) process(&data[i]);
        std::string out;
        for (int i = 0; i < 5; ++i) {
            out.push_back((char)((h[i] >> 24) & 0xFF));
            out.push_back((char)((h[i] >> 16) & 0xFF));
            out.push_back((char)((h[i] >> 8) & 0xFF));
            out.push_back((char)(h[i] & 0xFF));
        }
        return out;
    }
};
} // namespace

// ---------------------------------------------------------------------------
bool handshake(Conn* conn, const std::string& head) {
    std::string lower = head;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    size_t p = lower.find("sec-websocket-key:");
    if (p == std::string::npos) return false;
    size_t s = p + 18;
    while (s < head.size() && (head[s] == ' ' || head[s] == '\t')) ++s;
    size_t e = head.find("\r\n", s);
    std::string key = head.substr(s, e - s);

    Sha1 sha;
    std::string accept = base64(
        (const unsigned char*)sha.digest(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(),
        20);

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    conn->writeAll(resp);
    return true;
}

// ---------------------------------------------------------------------------
static bool recvAll(Conn* conn, char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        int r = conn->read(buf + got, (int)(n - got));
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

bool sendText(Conn* conn, const std::string& text) {
    std::string frame;
    frame.push_back((char)0x81);  // FIN + opcode text
    size_t len = text.size();
    if (len < 126) {
        frame.push_back((char)len);
    } else if (len < 65536) {
        frame.push_back((char)126);
        frame.push_back((char)((len >> 8) & 0xFF));
        frame.push_back((char)(len & 0xFF));
    } else {
        frame.push_back((char)127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((char)((len >> (i * 8)) & 0xFF));
    }
    frame += text;
    return conn->writeAll(frame.data(), frame.size());
}

bool readFrame(Conn* conn, int& opcode, std::string& payload) {
    unsigned char hdr[2];
    if (!recvAll(conn, (char*)hdr, 2)) return false;
    opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126) {
        unsigned char b[2];
        if (!recvAll(conn, (char*)b, 2)) return false;
        len = ((uint64_t)b[0] << 8) | b[1];
    } else if (len == 127) {
        unsigned char b[8];
        if (!recvAll(conn, (char*)b, 8)) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | b[i];
    }
    if (len > 16 * 1024 * 1024) return false;

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked && !recvAll(conn, (char*)mask, 4)) return false;

    payload.resize((size_t)len);
    if (len > 0 && !recvAll(conn, &payload[0], (size_t)len)) return false;
    if (masked) {
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] ^= mask[i % 4];
    }
    return true;
}

// ---------------------------------------------------------------------------
struct Client {
    Conn* conn;
    std::set<std::string> paths;   // path yang dilanggan
};

struct Hub::Impl {
    std::mutex mtx;
    std::vector<Client*> clients;
};

Hub::Hub() : d(new Impl()) {}
Hub::~Hub() {
    for (Client* c : d->clients) delete c;
    delete d;
}

Client* Hub::add(Conn* conn) {
    std::lock_guard<std::mutex> lk(d->mtx);
    Client* c = new Client();
    c->conn = conn;
    d->clients.push_back(c);
    return c;
}

void Hub::remove(Client* c) {
    std::lock_guard<std::mutex> lk(d->mtx);
    for (auto it = d->clients.begin(); it != d->clients.end(); ++it) {
        if (*it == c) {
            d->clients.erase(it);
            delete c;
            return;
        }
    }
}

void Hub::subscribe(Client* c, const std::string& path) {
    std::lock_guard<std::mutex> lk(d->mtx);
    c->paths.insert(path);
}

void Hub::unsubscribe(Client* c, const std::string& path) {
    std::lock_guard<std::mutex> lk(d->mtx);
    c->paths.erase(path);
}

void Hub::notify(const std::string& path, const std::string& message) {
    std::lock_guard<std::mutex> lk(d->mtx);
    for (Client* c : d->clients) {
        if (c->paths.count(path)) sendText(c->conn, message);
    }
}

std::set<std::string> Hub::paths() {
    std::lock_guard<std::mutex> lk(d->mtx);
    std::set<std::string> all;
    for (Client* c : d->clients)
        for (const auto& p : c->paths) all.insert(p);
    return all;
}

int Hub::clientCount() {
    std::lock_guard<std::mutex> lk(d->mtx);
    return (int)d->clients.size();
}

Hub& hub() {
    static Hub h;
    return h;
}

} // namespace nxfirews
