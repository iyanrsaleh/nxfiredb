# NxfireDB — Panduan Pengguna

Panduan singkat memasang dan memakai NxfireDB. NxfireDB adalah **lapisan
lokal** yang meneruskan perintah JSON dari aplikasi Anda ke **Firebase
Realtime Database**.

> **Mengapa proyek ini ada?** Karena tidak semua orang mampu membiayai
> domain dan database berlangganan. NxfireDB membuat aplikasi cukup bicara
> ke `localhost` — gratis — dengan database gratis dari Firebase di
> belakangnya. Keterbatasan bukan penghalang untuk berkarya.

## 1. Pasang

```
npm create nxfire-app@latest
```

Lalu buka **terminal baru** dan ketik:

```
nxfire
```

## 2. Siapkan Firebase

Anda perlu sebuah project Firebase dengan **Realtime Database** aktif.

1. Buka [console.firebase.google.com](https://console.firebase.google.com),
   buat project, aktifkan **Realtime Database**.
2. Catat **Project ID** (mis. `my-app-default-rtdb`).
3. Untuk akses tanpa login, ambil **Database Secret** dari
   *Project settings → Service accounts → Database secrets* (legacy), atau
   gunakan aturan (rules) publik saat uji.

Simpan ke `nxfire.json`:

```
nxfire setup
```

Isi `project`, `region` (opsional), `secret` (atau `token`), dan `port`
(default **9026**).

## 3. Uji koneksi

```
nxfire test
```

Bila berhasil: `OK — terhubung. Latensi: ... ms`.

## 4. Jalankan server

Cara biasa — server nyala di latar belakang dan tetap jalan (tanpa jendela):

```
nxfire start
```

Server jalan di `http://127.0.0.1:9026`. Cek dan hentikan kapan saja:

```
nxfire status
nxfire stop
```

Agar server **otomatis nyala** tiap komputer menyala:

```
nxfire autostart on     # matikan: nxfire autostart off
```

Untuk debug (tatap muka, Ctrl+C untuk berhenti):

```
nxfire server
```

## 5. Kirim perintah

Dari terminal lain:

```
curl -X POST http://127.0.0.1:9026/ -H "Content-Type: application/json" \
     -d "{\"op\":\"set\",\"path\":\"/users/budi\",\"value\":{\"name\":\"Budi\",\"age\":30}}"

curl -X POST http://127.0.0.1:9026/ -H "Content-Type: application/json" \
     -d "{\"op\":\"get\",\"path\":\"/users/budi\"}"
```

Atau lewat klien resmi:

```
nxfire-client '{"op":"get","path":"/users"}' --port 9026
```

## 6. Realtime

Pantau perubahan sebuah path:

```
nxfire-watch /users --port 9026
```

## Opsi berguna

- **Cache** — tambahkan `"cache_ttl": 5` di `nxfire.json` agar `GET` yang
  sama cepat (dibaca dari memori) selama 5 detik.
- **Antrian offline** — bila Firebase sedang tak terjangkau, perintah tulis
  disimpan di `nxfire.queue.json` dan dikirim ulang otomatis. Aktif secara
  default (`"offline_queue": true`).

## Pemecahan masalah

| Gejala | Saran |
|---|---|
| `nxfire test` gagal | Periksa `project`/`secret` dan koneksi internet |
| `permission_denied` | Cek aturan (rules) Firebase atau token/secret |
| `network_error` | Firebase tak terjangkau; cek koneksi (tulis akan diantri) |
| Port 9026 terpakai | Ubah `"port"` di `nxfire.json` |
