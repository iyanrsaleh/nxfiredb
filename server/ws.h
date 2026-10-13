#ifndef NXFIRE_SERVER_WS_H
#define NXFIRE_SERVER_WS_H

#include "conn.h"

#include <set>
#include <string>

// WebSocket lokal NxfireDB: klien subscribe ke sebuah PATH; server memantau
// path itu di Firebase (polling) dan mendorong perubahan ke klien.

namespace nxfirews {

using nxfireconn::Conn;

struct Client;
class Hub;

Hub& hub();

class Hub {
public:
    Hub();
    ~Hub();

    Client* add(Conn* conn);
    void remove(Client* c);

    void subscribe(Client* c, const std::string& path);
    void unsubscribe(Client* c, const std::string& path);

    // Kirim `message` ke semua klien yang subscribe ke `path`.
    void notify(const std::string& path, const std::string& message);

    // Gabungan semua path yang sedang dilanggan (untuk poller).
    std::set<std::string> paths();

    int clientCount();

private:
    struct Impl;
    Impl* d;
};

// Handshake WebSocket dari header HTTP yang sudah dibaca.
bool handshake(Conn* conn, const std::string& head);

// Kirim satu frame teks (server -> klien, tanpa mask).
bool sendText(Conn* conn, const std::string& text);

// Baca satu frame; mengisi `opcode` dan `payload`.
bool readFrame(Conn* conn, int& opcode, std::string& payload);

constexpr int OP_TEXT  = 0x1;
constexpr int OP_CLOSE = 0x8;
constexpr int OP_PING  = 0x9;
constexpr int OP_PONG  = 0xA;

} // namespace nxfirews

#endif // NXFIRE_SERVER_WS_H
