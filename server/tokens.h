#ifndef NXFIRE_TOKENS_H
#define NXFIRE_TOKENS_H

// Manajemen token akses nxfire-server.
//
// - Token berformat "nxf_" + 32 karakter heksadesimal (acak).
// - Disimpan sebagai hash SHA-256; teks asli hanya ditampilkan SEKALI saat
//   dibuat/dirotasi.
// - Satu token = satu user dengan role "admin" atau "user".
//   admin  : boleh semua op, termasuk kelola token.
//   user   : boleh operasi data (set/get/update/push/remove/insert), tapi
//            tidak boleh kelola token.
// - Token admin pertama dibuat saat server pertama kali dijalankan dengan
//   auth aktif dan ditampilkan di log.
//
// Penyimpanan: file JSON di samping nxfire.json (nxfire.tokens.json).

#include <string>
#include <vector>

namespace nxfiretokens {

struct TokenInfo {
    std::string user;        // nama user/pemilik
    std::string role;        // "admin" | "user"
    std::string tokenHash;   // SHA-256 hex dari token asli
    long long   createdAt = 0; // unix time
};

// SHA-256 mandiri (hex lowercase).
std::string sha256Hex(const std::string& input);

// Token acak "nxf_" + 32 hex.
std::string generateToken();

class TokenStore {
public:
    // Muat file tokel; bila belum ada, store kosong.
    explicit TokenStore(const std::string& filePath);

    // Ada token terdaftar?
    bool empty() const;

    // Buat token baru. Mengembalikan teks token (HANYA sekali ini).
    // Mengembalikan "" bila user sudah ada.
    std::string create(const std::string& user, const std::string& role);

    // Ganti token seorang user (rotate). Teks baru dikembalikan sekali.
    std::string rotate(const std::string& user);

    // Hapus token seorang user. false bila tidak ditemukan.
    bool remove(const std::string& user);

    // Daftar user+role (tanpa hash).
    std::vector<TokenInfo> list() const;

    // Verifikasi token asli -> isi user & role. true bila sah.
    bool authenticate(const std::string& token, std::string& user, std::string& role) const;

    // Admin pertama: bila store kosong, buat user "__owner__" role admin.
    // Mengembalikan teks token; "" bila store tidak kosong.
    std::string ensureOwner();

    // Simpan ke disk.
    bool save() const;

private:
    std::string path_;
    std::vector<TokenInfo> tokens_;
};

} // namespace nxfiretokens

#endif // NXFIRE_TOKENS_H
