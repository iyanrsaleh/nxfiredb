# NxfireDB

**Lapisan lokal di depan Firebase Realtime Database.**

Aplikasi pengguna cukup mengirim **JSON ke `localhost`**; NxfireDB yang
meneruskannya ke **Firebase cloud** dan mengembalikan hasilnya. Aplikasi
tidak perlu tahu URL Firebase, format `.json`, atau header auth.

```
Aplikasi pengguna
      |
      |  POST http://127.0.0.1:9026/   { "op":"set", "path":"/users/budi", ... }
      v
 nxfire-server   (proses lokal, C++)
      |
      |  Firebase REST API  (HTTPS)
      v
 Firebase Realtime Database  (cloud Google)
```

NxfireDB **tidak menyimpan data sendiri** — semua data tetap di Firebase.
Ia adalah perantara yang menyederhanakan akses, plus fitur lokal: **cache**,
**antrian offline**, dan **realtime WebSocket lokal**.

## Mengapa NxfireDB ada

Ide ini lahir dari keterbatasan. Banyak programmer — terutama pemula dan
pelajar — **tidak mampu membiayai domain dan database berlangganan**.
Domain itu mahal, database berbayar itu mahal, dan tutorial di internet
sering menganggap semua orang punya itu.

NxfireDB adalah bentuk karya dari keterbatasan itu:

- **Tidak ada domain?** Tidak perlu. Aplikasi berbicara ke `localhost`
  (`http://127.0.0.1:9026`) — gratis, milik sendiri, dan selalu ada.
- **Tidak mampu database berbayar?** Cukup tier gratis Firebase. NxfireDB
  menyembunyikan kerumitan REST-nya: URL panjang, format `.json`, token
  auth, dan query parameter diganti satu perintah JSON yang sederhana.
- **Internet tidak stabil?** Antrian offline menyimpan operasi tulis lalu
  mengirimnya ulang saat koneksi kembali — data tidak hilang.
- **Komputer restart?** Auto-start menyalakan ulang server sendiri.

Keterbatasan bukan penghalang untuk berkarya — ia alasan untuk berkarya.
NxfireDB dibuat oleh orang yang merasakan keterbatasan itu, untuk sesama
yang mengalaminya.

## Cara Install

Cara termudah (lewat npm):

```
npm create nxfire-app@latest
```

Memasang NxfireDB ke folder data pengguna dan menambahkannya ke PATH.
Setelah selesai, buka terminal baru lalu jalankan `nxfire`.

Atau dari paket rilis: ekstrak `nxfire-sdk-<versi>-<os>.tar.gz`, salin isi
`bin/` ke folder yang ada di PATH.

## Cara Pakai (ringkas)

```
nxfire setup      # isi project Firebase, secret, port
nxfire test       # uji koneksi ke Firebase
nxfire start      # nyalakan server di latar belakang (selalu jalan)
nxfire stop       # hentikan server
nxfire status     # cek status server & autostart
nxfire autostart  # on/off auto-start saat komputer menyala
nxfire server     # jalankan server lokal (foreground, debug)
```

Setelah server jalan, kirim perintah dari aplikasi/terminal lain:

```
curl -X POST http://127.0.0.1:9026/ -H "Content-Type: application/json" \
     -d '{"op":"set","path":"/users/budi","value":{"name":"Budi","age":30}}'

curl -X POST http://127.0.0.1:9026/ -H "Content-Type: application/json" \
     -d '{"op":"get","path":"/users/budi"}'

# atau via CLI dengan tema Ubuntu
nxfire-client '{"op":"set","table":"users","key":"budi","data":{"name":"Budi"}}'
```

## Konfigurasi (`nxfire.json`)

```json
{
  "project": "my-app-default-rtdb",
  "region":  "asia-southeast1",
  "api_key": "AIzaSy...",
  "secret":  "...",
  "port":    9026,
  "cache_ttl": 0,
  "offline_queue": true
}
```

| Field | Arti | Default |
|---|---|---|
| `project` | ID project Firebase | — |
| `region` | Region database (kosong = `firebaseio.com`) | (kosong) |
| `api_key` | Web API key Firebase | — |
| `secret` | Database Secret (legacy) → `?auth=` | — |
| `token` | ID token → `?auth=` | — |
| `port` | Port server lokal | `9026` |
| `cache_ttl` | Cache GET (detik; 0 = mati) | `0` |
| `offline_queue` | Antrian tulis saat Firebase tak terjangkau | `true` |
| `base_url` | Override URL (Firebase Emulator / uji) | — |

## Operasi

Dua gaya, hasil sama:

```jsonc
// Gaya path (Firebase)
{ "op": "set", "path": "/users/dantrik", "value": { "name": "dantrik", "age": 30 } }

// Gaya tabel (ramah)
{ "op": "set", "table": "users", "key": "dantrik", "value": { "name": "dantrik", "age": 30 } }
```

```jsonc
{ "op": "get",    "path": "/users/budi" }
{ "op": "update", "path": "/users/budi", "value": { "age": 31 } }
{ "op": "push",   "path": "/chat/room1", "value": { "text": "halo" } }
{ "op": "remove", "path": "/users/budi" }
```

Gaya tabel: `table` (+ `key`/`id` opsional), `data` = alias `value`, dan alias
op `insert`/`select`/`delete`. Detail: [`docs/Sintaks-Nxfire.md`](./docs/Sintaks-Nxfire.md).

Dipetakan ke Firebase REST: `set`→PUT, `get`→GET, `update`→PATCH, `push`→POST,
`remove`→DELETE.

**Query** (`get`): `orderBy` (`$key`/`$value`/nama child), `startAt`, `endAt`,
`equalTo`, `limitToFirst`, `limitToLast`, `shallow`.

Balasan seragam:

```json
{ "ok": true,  "data": { "name": "Budi" }, "meta": { "latency_ms": 42 } }
{ "ok": false, "error": "permission_denied", "hint": "Permission denied" }
```

## Realtime (WebSocket lokal)

```
ws://127.0.0.1:9026/ws
```

```json
{ "subscribe": "/chat" }
```

Server memantau path itu di Firebase dan mendorong:

```json
{ "type": "change", "path": "/chat", "data": { "room1": { "text": "halo" } } }
```

Alat siap pakai:

```
nxfire-watch /chat --port 9026
```

## Fitur Lokal

- **Cache** (`cache_ttl`) — respons `GET` disimpan sebentar; dibatalkan saat ada tulis.
- **Antrian offline** — tulis yang gagal karena Firebase tak terjangkau disimpan
  ke `nxfire.queue.json` dan dikirim ulang otomatis.
- **Realtime WebSocket** — subscribe per path (lihat di atas).
- **Token akses** — sejak token pertama dibuat, semua perintah wajib
  `Authorization: Bearer nxf_...`:

  ```jsonc
  // kelola token (admin saja)
  { "op": "token_create", "user": "budi", "role": "user" }
  { "op": "token_rotate", "user": "budi" }   // perbarui; token lama mati
  { "op": "token_delete", "user": "budi" }
  { "op": "token_list" }
  ```

  Token disimpan sebagai **hash SHA-256** di `nxfire.tokens.json`; teks asli
  hanya tampil sekali saat dibuat/dirotasi. Role `admin` boleh semua operasi
  termasuk kelola token; role `user` hanya operasi data. Server **tanpa**
  file token berjalan terbuka (kompatibel perilaku lama).

  `nxfire-client` (mode server) ikut mengirim token:

  ```
  nxfire-client '{"op":"get","path":"/users"}' --port 9026 --token nxf_...
  nxfire-client '{"op":"get","path":"/users"}' --port 9026 --token-file tok.txt
  # atau lewat lingkungan: set NXFIRE_TOKEN=nxf_...
  ```

## Struktur

```
include/nxfire/         header publik (Nxfire.h, Json.h, Http.h, Export.h)
src/                    mesin: Nxfire.cpp, Json.cpp, Http.cpp (WinHTTP)
server/                 nxfire-server(.exe)  server lokal HTTP + WebSocket
client/                 nxfire-client(.exe)  pengirim JSON (embedded / server)
tools/nxfire/           nxfire(.exe)         menu & setup
tools/watch/            nxfire-watch(.exe)   pantau path realtime
clients/                SDK: Python, JS, C#, Go, PHP
create-nxfire-app/      installer lewat npm
scripts/                build & paket rilis
tests/                  uji otomatis (mock Firebase + uji per fase)
docs/                   dokumentasi user & sintaks
```

## Build

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Atau: `scripts\build.bat` (Windows) / `scripts\build.sh` (Linux).

Hasil:
- `build/server/Release/nxfire-server.exe`
- `build/client/Release/nxfire-client.exe`
- `build/tools/nxfire/Release/nxfire.exe`
- `build/tools/watch/Release/nxfire-watch.exe`

## Uji

```
node tests/run_all.mjs
```

Menjalankan uji tiap fase terhadap **mock Firebase** lokal (tanpa kredensial
Firebase asli).

## Klien (SDK)

| Bahasa | Berkas |
|---|---|
| Python | `clients/python/nxfire.py` |
| JavaScript (Node) | `clients/javascript/nxfire.js` |
| C# (.NET 6+) | `clients/csharp/NxfireDb.cs` |
| Go | `clients/go/nxfire.go` |
| PHP | `clients/php/nxfire.php` |

Detail: [`clients/README.md`](./clients/README.md).

## Catatan

- Firebase Realtime Database adalah layanan **cloud**; NxfireDB **meniru**
  cara aksesnya tetapi berjalan **lokal** sebagai perantara.
- Protokol lengkap: [`docs/Sintaks-Nxfire.md`](./docs/Sintaks-Nxfire.md).
- Panduan user: [`docs/user-README.md`](./docs/user-README.md).
