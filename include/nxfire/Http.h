#ifndef NXFIRE_HTTP_H
#define NXFIRE_HTTP_H

#include <nxfire/Export.h>

#include <string>
#include <utility>
#include <vector>

// HTTP client NxfireDB (internal engine + tersedia untuk alat).
// Windows: WinHTTP (bawaan OS, tanpa dependensi eksternal).

namespace nxfire {
namespace http {

struct NXFIRE_EXPORT Response {
    int         status = 0;   // 0 = gagal sebelum dapat balasan
    std::string body;         // body balasan (UTF-8)
    std::string error;        // pesan error transport (kosong = tidak ada)
};

// Kirim satu request. url harus lengkap (skema + host + path + query).
NXFIRE_EXPORT Response request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers = {});

// Persen-encode nilai agar aman dipakai di query string.
NXFIRE_EXPORT std::string urlEncode(const std::string& value);

} // namespace http
} // namespace nxfire

#endif // NXFIRE_HTTP_H
