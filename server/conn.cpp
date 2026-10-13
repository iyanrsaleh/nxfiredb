#include "conn.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nxfireconn {

int Conn::read(char* buf, int n) {
    return (int)recv(sock, buf, n, 0);
}

bool Conn::writeAll(const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = (int)send(sock, data + sent, (int)(len - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

void Conn::close() {
#ifdef _WIN32
    closesocket(sock);
#else
    ::close(sock);
#endif
}

} // namespace nxfireconn
