# NxfireDB — Sintaks & Protokol

Dokumen ini menjelaskan protokol lokal NxfireDB: bagaimana aplikasi
berkomunikasi dengan `nxfire-server`, dan bagaimana perintah itu dipetakan
ke Firebase Realtime Database REST API.

## 1. Endpoint

| Metode | Path | Guna |
|---|---|---|
| `POST` | `/` atau `/query` | Kirim perintah JSON |
| `GET`  | `/health` | Status server + metrik |
| `GET`  | `/ws` | WebSocket realtime (subscribe per path) |
| `OPTIONS` | (apa pun) | CORS preflight → 204 |

Semua `POST` memakai `Content-Type: application/json`.

## 2. Format perintah

```jsonc
{
  "op": "set",           // set | get | update | push | remove
  "path": "/users/budi", // path node (gaya Firebase)
  "value": { ... },      // untuk set/update/push
  "query": { ... }       // untuk get (opsional)
}
```

### 2.0 Dua gaya penulisan (hasil sama)

**Gaya path (gaya Firebase)** — tulis path lengkap:

```json
{ "op": "set", "path": "/users/dantrik", "value": { "name": "dantrik", "age": 30 } }
```

**Gaya tabel (ramah, seperti database umum)** — cukup nama tabel + kunci baris:

```json
{ "op": "set", "table": "users", "key": "dantrik", "value": { "name": "dantrik", "age": 30 } }
```

Keduanya menghasilkan pohon Firebase yang **persis sama** (`/users/dantrik`).

Aturan gaya tabel:

- `table` → node `/table`. Boleh bersarang: `"table": "users/aktif"` → `/users/aktif`.
- `key` (alias `id`) → anak baris. Bila ada: path = `/table/key`.
- Tanpa `key`/`id`: path = `/table` (seluruh tabel/node).
- `data` boleh dipakai sebagai **alias** `value`.
- Alias `op`: `put`/`upsert`→`set`, `select`→`get`, `patch`→`update`,
  `insert`/`add`→`push`, `delete`/`del`→`remove`.

Contoh lain:

```json
// 1) Kunci acak ala Firebase (default, "push" style)
{ "op": "insert", "table": "users", "data": { "name": "budi" } }  // → /users/-Mx... + {name:"budi"}

// 2) Ambil kunci dari sebuah field (keyFrom)
{ "op": "insert", "table": "users", "keyFrom": "name", "data": { "name": "budi", "age": 20 } }  // → /users/budi

// 3) Nomor urut otomatis (autoId)
{ "op": "insert", "table": "users", "autoId": true, "data": { "name": "budi" } }  // → /users/1

// 4) Tentukan kunci secara eksplisit
{ "op": "insert", "table": "users", "key": "citra", "data": { "name": "citra" } }  // → /users/citra
```

Catatan prioritas: `autoId` > `keyFrom` > `key`/`id` > (tanpa kunci) → kunci acak ala Firebase.
`insert` tanpa kunci tidak mengubah perilaku lama.

### 2.1 `set` — tulis penuh (replace)

```json
{ "op": "set", "path": "/users/budi", "value": { "name": "Budi", "age": 30 } }
```

Menimpa seluruh nilai pada path (anak lama yang tak disebut **hilang**).
Dipetakan ke `PUT <path>.json`.

### 2.2 `get` — baca

```json
{ "op": "get", "path": "/users/budi" }
```

Dipetakan ke `GET <path>.json`. Mengembalikan nilai node (atau `null`).

### 2.3 `update` — tulis dangkal (merge)

```json
{ "op": "update", "path": "/users/budi", "value": { "age": 31 } }
```

Mengganti hanya field yang disebut; field lain tetap. Dipetakan ke
`PATCH <path>.json`.

### 2.4 `push` — tambah anak dengan kunci otomatis

```json
{ "op": "push", "path": "/chat/room1", "value": { "text": "halo" } }
```

Membuat anak baru dengan kunci auto (mis. `-Mx...`) di dalam `path`.
Balasan memuat kuncinya:

```json
{ "ok": true, "data": { "name": "-MxAbC123" } }
```

Dipetakan ke `POST <path>.json`.

### 2.5 `remove` — hapus

```json
{ "op": "remove", "path": "/users/budi" }
```

Dipetakan ke `DELETE <path>.json`.

## 3. Query (`get`)

```jsonc
{
  "op": "get",
  "path": "/users",
  "query": {
    "orderBy": "age",     // "$key" | "$value" | nama child
    "startAt": 20,
    "endAt": 40,
    "equalTo": 30,
    "limitToFirst": 5,
    "limitToLast": 5,
    "shallow": false
  }
}
```

Parameter diteruskan ke Firebase (`orderBy`, `startAt`, `endAt`, `equalTo`,
`limitToFirst`, `limitToLast`, `shallow`). Catatan Firebase: `limitToFirst`/
`limitToLast`/`startAt`/`endAt`/`equalTo` hanya berlaku bila `orderBy` diisi.

## 4. Balasan

Sukses:

```json
{ "ok": true, "data": <nilai>, "meta": { "latency_ms": 42 } }
```

Gagal:

```json
{ "ok": false, "error": "<kode>", "hint": "<pesan>", "http_status": <kode> }
```

### Kode error

| `error` | Arti |
|---|---|
| `bad_request` | Body bukan JSON valid / bukan objek |
| `unknown_op` | `op` tidak dikenal |
| `config_error` | `nxfire.json` tak bisa dimuat |
| `unauthorized` | 401 dari Firebase |
| `permission_denied` | 403 (rules menolak) |
| `not_found` | 404 |
| `precondition_failed` | 412 |
| `too_many_requests` | 429 |
| `network_error` | Firebase tak terjangkau |
| `payload_too_large` | Body melebihi batas |
| `not_found` (server) | Endpoint lokal tak dikenal |

## 5. Cache (`cache_ttl`)

Bila `cache_ttl > 0`, respons `GET` disimpan di memori selama N detik. Setiap
tulis (`set`/`update`/`push`/`remove`) yang sukses **membatalkan** cache.

## 6. Antrian offline (`offline_queue`)

Bila `true` (default), perintah tulis yang gagal karena `network_error`
**disimpan** ke `nxfire.queue.json` dan dicoba ulang berkala. Balasan:

```json
{ "ok": true, "queued": true, "meta": { "queue_size": 1 } }
```

Baca (`get`) **tidak** diantri (langsung `network_error`).

## 7. WebSocket realtime

Sambung ke `ws://127.0.0.1:<port>/ws`.

Berlangganan:

```json
{ "subscribe": "/chat" }
```

Balasan & dorongan:

```json
{ "type": "welcome", "service": "NxfireDB" }
{ "type": "subscribed", "path": "/chat" }
{ "type": "value", "path": "/chat", "data": { ... } }     // nilai awal
{ "type": "change", "path": "/chat", "data": { ... } }    // saat berubah
```

Berhenti berlangganan:

```json
{ "unsubscribe": "/chat" }
```

Server memantau tiap path yang dilanggan di Firebase (default tiap 1000 ms;
atur `--ws-poll-ms`).

## 8. Opsi `nxfire-server`

| Opsi | Arti | Default |
|---|---|---|
| `--config FILE` | Konfigurasi | `nxfire.json` |
| `--port N` | Port server lokal | `9026` / dari config |
| `--host H` | Alamat bind | `127.0.0.1` |
| `--workers N` | Jumlah thread pekerja | otomatis |
| `--max-connections N` | Batas koneksi bersamaan | 256 |
| `--max-body N` | Batas ukuran body (byte) | 64 MB |
| `--ws-poll-ms N` | Jeda polling realtime (ms; 0 = mati) | 1000 |
| `--retry-ms N` | Jeda kirim ulang antrian offline (ms) | 2000 |

## 9. Peta ke Firebase REST

| op lokal | Firebase REST |
|---|---|
| `set` | `PUT    https://<project>/<path>.json` |
| `get` | `GET    https://<project>/<path>.json` |
| `update` | `PATCH  https://<project>/<path>.json` |
| `push` | `POST   https://<project>/<path>.json` |
| `remove` | `DELETE https://<project>/<path>.json` |

Autentikasi: `?auth=<secret|token>` bila dikonfigurasi.
