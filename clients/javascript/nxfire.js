// NxfireDB SDK (JavaScript / Node) — klien tipis untuk server lokal NxfireDB.
// Tanpa dependensi (pakai modul http bawaan).
//
// Contoh:
//   const { Nxfire } = require('./nxfire');
//   const db = new Nxfire('127.0.0.1', 9026);
//   await db.set('/users/budi', { name: 'Budi', age: 30 });
//   console.log(await db.get('/users/budi'));
//   await db.push('/chat/room1', { text: 'halo' });
//   await db.remove('/users/budi');
'use strict';

const http = require('http');

class Nxfire {
    constructor(host = '127.0.0.1', port = 9026, timeoutMs = 30000) {
        this.host = host;
        this.port = port;
        this.timeoutMs = timeoutMs;
    }

    // Kirim satu operasi; mengembalikan Promise<objek balasan>.
    call(op, path, value, query) {
        const req = { op };
        if (path !== undefined) req.path = path;
        if (value !== undefined) req.value = value;
        if (query !== undefined) req.query = query;
        const body = JSON.stringify(req);

        return new Promise((resolve, reject) => {
            const r = http.request(
                {
                    host: this.host,
                    port: this.port,
                    method: 'POST',
                    path: '/',
                    headers: {
                        'Content-Type': 'application/json',
                        'Content-Length': Buffer.byteLength(body),
                    },
                    timeout: this.timeoutMs,
                },
                (res) => {
                    let data = '';
                    res.setEncoding('utf8');
                    res.on('data', (c) => (data += c));
                    res.on('end', () => {
                        try { resolve(JSON.parse(data)); }
                        catch { reject(new Error(`balasan bukan JSON: ${data}`)); }
                    });
                }
            );
            r.on('timeout', () => r.destroy(new Error('timeout')));
            r.on('error', reject);
            r.write(body);
            r.end();
        });
    }

    set(path, value) { return this.call('set', path, value); }
    get(path, query) { return this.call('get', path, undefined, query); }
    update(path, value) { return this.call('update', path, value); }
    push(path, value) { return this.call('push', path, value); }
    remove(path) { return this.call('remove', path); }

    // Kembalikan `data` saja (atau null bila gagal).
    async value(path, query) {
        const res = await this.get(path, query);
        return res && res.ok ? res.data : null;
    }
}

module.exports = { Nxfire };

// CLI cepat bila dijalankan langsung.
if (require.main === module) {
    const args = process.argv.slice(2);
    let host = '127.0.0.1', port = 9026, opJson = null;
    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--host') host = args[++i];
        else if (args[i] === '--port') port = Number(args[++i]);
        else opJson = args[i];
    }
    if (!opJson) { console.log('pakai: node nxfire.js \'<json>\' [--port N]'); process.exit(2); }
    const req = JSON.parse(opJson);
    new Nxfire(host, port).call(req.op, req.path, req.value, req.query)
        .then((res) => { console.log(JSON.stringify(res)); process.exit(res.ok ? 0 : 1); })
        .catch((e) => { console.error(String(e.message || e)); process.exit(1); });
}
