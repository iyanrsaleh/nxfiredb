# create-nxfire-app

Memasang **NxfireDB** ke sistem — lapisan lokal di depan Firebase Realtime
Database.

## Prasyarat

- **Node.js 18+** (dengan `npm`)

Itu saja. Node.js diperlukan karena tool ini dijalankan lewat `npm`.

## Pakai

```
npm create nxfire-app@latest
```

Perintah ini:
- mengunduh NxfireDB (server + client + menu) dari rilis resmi,
- memasangnya ke folder data pengguna,
- menambahkannya ke PATH otomatis.

Setelah selesai, buka terminal baru lalu jalankan:

```
nxfire setup         # isi project Firebase, secret, port
nxfire test          # uji koneksi
nxfire start         # jalankan server di latar belakang
nxfire status        # cek status server
nxfire stop          # hentikan server
nxfire autostart on  # nyalakan otomatis saat komputer menyala
```

Perintah yang tersedia setelah pemasangan:

```
nxfire           # menu & setup
nxfire-server    # server lokal HTTP (teruskan ke Firebase)
nxfire-client    # kirim perintah JSON
nxfire-watch     # pantau perubahan node (realtime)
```

## Opsi

```
npm create nxfire-app@latest -- --sdk <folder|file|URL .tar.gz>
npm create nxfire-app@latest -- --repo <owner/nama>
npm create nxfire-app@latest -- --prefix <folder>
```
