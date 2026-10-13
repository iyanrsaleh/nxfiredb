#ifndef NXFIRE_NXFIRE_H
#define NXFIRE_NXFIRE_H

#include <nxfire/Export.h>
#include <nxfire/Json.h>

#include <memory>
#include <string>

// NxfireDB — lapisan lokal di depan Firebase Realtime Database.
//
// Cara pakai:
//   nxfire::Nxfire db;
//   db.open("nxfire.json");          // muat konfigurasi (project, api_key, port)
//   auto r = db.set("/users/budi", Json::object());
//   auto r = db.get("/users/budi");
//
// Semua operasi diteruskan ke Firebase REST API.
namespace nxfire {

struct NXFIRE_EXPORT Result {
    bool        ok    = false;
    Json        data;           // data dari Firebase (null jika gagal)
    std::string error;          // kode error (mis. "permission_denied")
    std::string hint;           // saran perbaikan; boleh kosong
    int         httpStatus = 0; // HTTP status dari Firebase
};

struct NXFIRE_EXPORT QueryOpts {
    std::string orderBy;        // "$key", "$value", atau nama child
    Json        startAt;        // nilai awal (null = tidak dipakai)
    Json        endAt;
    Json        equalTo;
    int         limitToFirst = 0;
    int         limitToLast  = 0;
    bool        shallow      = false;
};

class NXFIRE_EXPORT Nxfire {
public:
    Nxfire();
    ~Nxfire();

    Nxfire(Nxfire&&) noexcept;
    Nxfire& operator=(Nxfire&&) noexcept;
    Nxfire(const Nxfire&) = delete;
    Nxfire& operator=(const Nxfire&) = delete;

    // Muat konfigurasi dari file JSON (project, api_key, secret, port, dst.)
    bool open(const std::string& configPath = "nxfire.json");
    void close();
    bool isOpen() const;
    std::string lastError() const;

    // Operasi CRUD — diteruskan ke Firebase REST API.
    Result set   (const std::string& path, const Json& value);   // PUT
    Result get   (const std::string& path,
                  const QueryOpts&  opts  = {});                  // GET
    Result update(const std::string& path, const Json& value);   // PATCH
    Result push  (const std::string& path, const Json& value);   // POST
    Result remove(const std::string& path);                       // DELETE

    // Jalankan perintah JSON (protokol lokal). Menerima dua gaya, hasil sama:
    //   {"op":"set","path":"/users/dantrik","value":{...}}
    //   {"op":"set","table":"users","key":"dantrik","value":{...}}   -> /users/dantrik
    //   {"op":"set","table":"users","value":{"dantrik":{...}}}       -> /users
    // op: set | get | update | push | remove
    //   (alias: put/upsert->set, select->get, insert->push, delete->remove)
    // "data" boleh dipakai sebagai alias "value".
    Result execute(const Json& request);

    // URL Firebase yang sedang dipakai (untuk diagnostik).
    std::string firebaseUrl() const;

    // Port server lokal (dari konfigurasi; default 9026).
    int port() const;

    // Masa cache GET dalam detik (0 = cache mati).
    int cacheTtl() const;

    // Apakah antrian offline aktif (tulis disimpan bila Firebase tak terjangkau).
    bool offlineQueue() const;


    // Latensi request terakhir ke Firebase (ms).
    int lastLatencyMs() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace nxfire

#endif // NXFIRE_NXFIRE_H
