#ifndef NXFIRE_SERVER_CONN_H
#define NXFIRE_SERVER_CONN_H

// Abstraksi koneksi socket untuk nxfire-server.

#include <string>

#ifdef _WIN32
#include <winsock2.h>
using sock_t = SOCKET;
#else
#include <sys/socket.h>
using sock_t = int;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#endif

namespace nxfireconn {

struct Conn {
    sock_t sock;

    explicit Conn(sock_t s) : sock(s) {}

    // Baca sampai `n` byte. Return jumlah byte (>0), atau <=0 bila ditutup.
    int read(char* buf, int n);

    // Kirim seluruh `data`. Return true bila sukses.
    bool writeAll(const char* data, size_t len);
    bool writeAll(const std::string& s) { return writeAll(s.data(), s.size()); }

    void close();
};

} // namespace nxfireconn

#endif // NXFIRE_SERVER_CONN_H
