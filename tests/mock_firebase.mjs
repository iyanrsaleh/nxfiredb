// mock_firebase.mjs — tiruan minimal Firebase Realtime Database REST API.
// Dipakai untuk menguji NxfireDB tanpa akun Firebase sungguhan.
//
// Jalankan mandiri:  node tests/mock_firebase.mjs [port]
// Sebagai modul   :  import { startMock } from './mock_firebase.mjs'

import http from 'node:http';

function startServer(port) {
    const root = {};

    const server = http.createServer((req, res) => {
        const u = new URL(req.url, 'http://localhost');
        let p = decodeURIComponent(u.pathname);
        if (p.endsWith('.json')) p = p.slice(0, -5);
        const segs = p.split('/').filter(Boolean);

        let body = '';
        req.on('data', (c) => (body += c));
        req.on('end', () => {
            const send = (code, val) => {
                res.writeHead(code, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(val));
            };
            const parentAndKey = () => {
                let node = root;
                for (let i = 0; i < segs.length - 1; i++) {
                    const s = segs[i];
                    if (node[s] === undefined || typeof node[s] !== 'object' || node[s] === null)
                        node[s] = {};
                    node = node[s];
                }
                return { node, key: segs.length ? segs[segs.length - 1] : null };
            };

            try {
                const method = req.method;
                if (method === 'GET') {
                    // Jalur khusus untuk menguji pemetaan error.
                    if (segs[0] === '_unauth') return send(401, { error: 'Unauthorized request' });
                    if (segs[0] === '_deny')   return send(403, { error: 'Permission denied' });

                    let node = root;
                    for (const s of segs) {
                        if (node === null || typeof node !== 'object') { node = null; break; }
                        node = node[s];
                    }
                    if (node === undefined) node = null;

                    const q = u.searchParams;

                    // shallow: objek -> { anak: true }
                    if (q.get('shallow') === 'true' && node && typeof node === 'object' && !Array.isArray(node)) {
                        const o = {};
                        for (const k of Object.keys(node)) o[k] = true;
                        return send(200, o);
                    }

                    // orderBy + filter + limit (hanya bila ada orderBy, seperti Firebase).
                    // Firebase mengirim orderBy="anak" (dengan kutip) atau orderBy="$key".
                    let orderBy = q.get('orderBy');
                    if (orderBy) orderBy = orderBy.replace(/^"|"$/g, '');
                    if (orderBy && node && typeof node === 'object' && !Array.isArray(node)) {
                        const cmp = (a, b) =>
                            (typeof a === 'number' && typeof b === 'number')
                                ? a - b
                                : String(a).localeCompare(String(b));
                        const parseVal = (s) => { try { return JSON.parse(s); } catch { return s; } };
                        const keyOf = ([k, v]) =>
                            orderBy === '$key' ? k : orderBy === '$value' ? v : (v ? v[orderBy] : undefined);

                        const startAt = q.has('startAt') ? parseVal(q.get('startAt')) : undefined;
                        const endAt   = q.has('endAt')   ? parseVal(q.get('endAt'))   : undefined;
                        const equalTo = q.has('equalTo') ? parseVal(q.get('equalTo')) : undefined;

                        let entries = Object.entries(node).filter(([k, v]) => {
                            const kv = keyOf([k, v]);
                            if (kv === undefined || kv === null) return false;
                            if (equalTo !== undefined) return cmp(kv, equalTo) === 0;
                            if (startAt !== undefined && cmp(kv, startAt) < 0) return false;
                            if (endAt !== undefined && cmp(kv, endAt) > 0) return false;
                            return true;
                        });
                        entries.sort((a, b) => cmp(keyOf(a), keyOf(b)));

                        const lf = q.has('limitToFirst') ? parseInt(q.get('limitToFirst'), 10) : 0;
                        const ll = q.has('limitToLast') ? parseInt(q.get('limitToLast'), 10) : 0;
                        if (lf > 0) entries = entries.slice(0, lf);
                        else if (ll > 0) entries = entries.slice(Math.max(0, entries.length - ll));

                        const out = {};
                        for (const [k, v] of entries) out[k] = v;
                        return send(200, out);
                    }

                    return send(200, node);
                }
                if (method === 'PUT') {
                    const val = body ? JSON.parse(body) : null;
                    if (segs.length === 0) {
                        for (const k of Object.keys(root)) delete root[k];
                        if (val && typeof val === 'object') Object.assign(root, val);
                        return send(200, val);
                    }
                    const { node, key } = parentAndKey();
                    node[key] = val;
                    return send(200, val);
                }
                if (method === 'PATCH') {
                    const patch = body ? JSON.parse(body) : {};
                    const { node, key } = parentAndKey();
                    let target = key === null ? root : node[key];
                    if (target === null || typeof target !== 'object') target = {};
                    Object.assign(target, patch);
                    if (key === null) Object.assign(root, target);
                    else node[key] = target;
                    return send(200, target);
                }
                if (method === 'POST') {
                    const val = body ? JSON.parse(body) : null;
                    // Firebase POST: tambah anak baru DI DALAM path (kunci auto).
                    let node = root;
                    for (const s of segs) {
                        if (node[s] === undefined || typeof node[s] !== 'object' || node[s] === null)
                            node[s] = {};
                        node = node[s];
                    }
                    const key = '-Mx' + Math.random().toString(36).slice(2, 12);
                    node[key] = val;
                    return send(200, { name: key });
                }
                if (method === 'DELETE') {
                    const { node, key } = parentAndKey();
                    if (key === null) for (const k of Object.keys(root)) delete root[k];
                    else delete node[key];
                    return send(200, null);
                }
                return send(405, { error: 'method not allowed' });
            } catch (e) {
                return send(400, { error: String((e && e.message) || e) });
            }
        });
    });

    return new Promise((resolve) => {
        server.listen(port, '127.0.0.1', () => {
            resolve({ server, root, close: () => server.close() });
        });
    });
}

export { startServer as startMock };

// Jalankan mandiri bila dipanggil langsung.
if (process.argv[1] && process.argv[1].endsWith('mock_firebase.mjs')) {
    const port = Number(process.argv[2] || 9000);
    startServer(port).then(() => {
        console.log(`mock Firebase berjalan di http://127.0.0.1:${port}`);
    });
}
