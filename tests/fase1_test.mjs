// fase1_test.mjs — uji otomatis Fase 1 (HTTP client ke Firebase REST).
//
// Mock Firebase dijalankan sebagai PROSES TERPISAH (bukan in-process), karena
// klien dipanggil dengan spawnSync yang memblokir event loop Node.

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exeName = process.platform === 'win32' ? 'nxfire-client.exe' : 'nxfire-client';
const CLIENT = path.join(root, 'build', 'client', 'Release', exeName);

if (!fs.existsSync(CLIENT)) {
    console.error(`[error] klien tidak ditemukan: ${CLIENT}\n        build dulu: scripts/build.bat`);
    process.exit(2);
}

const PORT = 9057;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.json');
fs.writeFileSync(CFG, JSON.stringify({
    project: 'mock',
    base_url: `http://127.0.0.1:${PORT}`,
    port: 9026,
}, null, 2));

let pass = 0, fail = 0;
function check(name, cond, detail) {
    if (cond) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}${detail ? '  -> ' + detail : ''}`); }
}

function run(op, cfg = CFG) {
    const r = spawnSync(CLIENT, [JSON.stringify(op), '--config', cfg], { encoding: 'utf8' });
    let parsed = null;
    try { parsed = JSON.parse(r.stdout.trim()); } catch { /* biarkan null */ }
    return { code: r.status, out: r.stdout.trim(), err: r.stderr, json: parsed };
}

// Nyalakan mock di proses terpisah.
const mockProc = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(PORT)], {
    stdio: 'ignore',
});

async function waitReady(tries = 40) {
    for (let i = 0; i < tries; i++) {
        try {
            const res = await fetch(`http://127.0.0.1:${PORT}/.json`);
            if (res.ok) return true;
        } catch { /* belum siap */ }
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}

const ready = await waitReady();
if (!ready) {
    console.error('[error] mock Firebase tidak siap');
    mockProc.kill();
    process.exit(2);
}

console.log(`\nUji Fase 1 — HTTP client ke Firebase REST (mock :${PORT})\n`);

try {
    // 1. set
    let r = run({ op: 'set', path: '/users/budi', value: { name: 'Budi', age: 30 } });
    check('set mengembalikan ok', r.json && r.json.ok === true, r.out);

    // 2. get
    r = run({ op: 'get', path: '/users/budi' });
    check('get mengembalikan data benar',
        r.json && r.json.ok === true && r.json.data && r.json.data.name === 'Budi' && r.json.data.age === 30,
        r.out);

    // 3. update (dangkal)
    r = run({ op: 'update', path: '/users/budi', value: { age: 31 } });
    check('update ok', r.json && r.json.ok === true, r.out);

    r = run({ op: 'get', path: '/users/budi' });
    check('update tidak menghapus field lain',
        r.json && r.json.data && r.json.data.name === 'Budi' && r.json.data.age === 31,
        r.out);

    // 4. push
    r = run({ op: 'push', path: '/chat', value: { text: 'halo' } });
    const pushedKey = r.json && r.json.data && r.json.data.name;
    check('push mengembalikan kunci auto', !!pushedKey, r.out);

    r = run({ op: 'get', path: '/chat' });
    check('push menambah anak',
        r.json && r.json.data && r.json.data[pushedKey] && r.json.data[pushedKey].text === 'halo',
        r.out);

    // 5. remove
    r = run({ op: 'remove', path: '/users/budi' });
    check('remove ok', r.json && r.json.ok === true, r.out);

    r = run({ op: 'get', path: '/users/budi' });
    check('remove menghapus node', r.json && r.json.ok === true && r.json.data === null, r.out);

    // 6. query (limitToLast) — pastikan tidak error
    run({ op: 'set', path: '/scores', value: { a: 10, b: 20, c: 30 } });
    r = run({ op: 'get', path: '/scores', query: { orderBy: '$key', limitToLast: 2 } });
    check('query limitToLast diteruskan tanpa error', r.json && r.json.ok === true, r.out);

    // 7. error: config bukan JSON
    const BAD = path.join(TMP, 'bad.json');
    fs.writeFileSync(BAD, '{ not json }');
    r = run({ op: 'get', path: '/' }, BAD);
    check('config tidak valid -> config_error',
        r.json && r.json.ok === false && r.json.error === 'config_error', r.out);

    // 8. error: transport (port mati)
    const DEAD = path.join(TMP, 'dead.json');
    fs.writeFileSync(DEAD, JSON.stringify({ project: 'x', base_url: 'http://127.0.0.1:1' }));
    r = run({ op: 'get', path: '/' }, DEAD);
    check('host mati -> network_error',
        r.json && r.json.ok === false && r.json.error === 'network_error', r.out);
} finally {
    mockProc.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
