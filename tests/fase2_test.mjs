// fase2_test.mjs — uji otomatis Fase 2 (server lokal nxfire-server).
//
// Alur: mock Firebase (proses A)  <-  nxfire-server (proses B)  <-  uji (fetch).

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));

if (!fs.existsSync(SERVER)) {
    console.error(`[error] server tidak ditemukan: ${SERVER}\n        build dulu: scripts/build.bat`);
    process.exit(2);
}

const MOCK_PORT = 9058;
const SRV_PORT  = 9071;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.fase2.json');
fs.writeFileSync(CFG, JSON.stringify({
    project: 'mock',
    base_url: `http://127.0.0.1:${MOCK_PORT}`,
    port: SRV_PORT,
}, null, 2));

let pass = 0, fail = 0;
function check(name, cond, detail) {
    if (cond) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}${detail ? '  -> ' + detail : ''}`); }
}

async function waitReady(urlStr, tries = 60) {
    for (let i = 0; i < tries; i++) {
        try { const r = await fetch(urlStr); if (r.ok) return true; } catch {}
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}

async function post(op) {
    const r = await fetch(`http://127.0.0.1:${SRV_PORT}/`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(op),
    });
    let json = null;
    try { json = await r.json(); } catch {}
    return { status: r.status, json };
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
const server = spawn(SERVER, ['--config', CFG], { stdio: 'ignore' });

let ok = (await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) &&
         (await waitReady(`http://127.0.0.1:${SRV_PORT}/health`));
if (!ok) {
    console.error('[error] mock/server tidak siap');
    mock.kill(); server.kill();
    process.exit(2);
}

console.log(`\nUji Fase 2 — server lokal nxfire-server (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

try {
    // GET /health
    {
        const r = await fetch(`http://127.0.0.1:${SRV_PORT}/health`);
        const j = await r.json();
        check('GET /health ok', r.status === 200 && j.ok === true && j.service === 'NxfireDB',
            JSON.stringify(j));
    }

    // set
    let r = await post({ op: 'set', path: '/users/budi', value: { name: 'Budi', age: 30 } });
    check('POST set ok', r.status === 200 && r.json && r.json.ok === true, JSON.stringify(r.json));

    // get
    r = await post({ op: 'get', path: '/users/budi' });
    check('POST get data benar',
        r.json && r.json.ok === true && r.json.data && r.json.data.name === 'Budi',
        JSON.stringify(r.json));

    // update
    r = await post({ op: 'update', path: '/users/budi', value: { age: 31 } });
    check('POST update ok', r.json && r.json.ok === true, JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users/budi' });
    check('update tidak menghapus field lain',
        r.json && r.json.data && r.json.data.name === 'Budi' && r.json.data.age === 31,
        JSON.stringify(r.json));

    // push
    r = await post({ op: 'push', path: '/chat', value: { text: 'halo' } });
    const key = r.json && r.json.data && r.json.data.name;
    check('POST push kunci auto', !!key, JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/chat' });
    check('push menambah anak',
        r.json && r.json.data && r.json.data[key] && r.json.data[key].text === 'halo',
        JSON.stringify(r.json));

    // remove
    r = await post({ op: 'remove', path: '/users/budi' });
    check('POST remove ok', r.json && r.json.ok === true, JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users/budi' });
    check('remove menghapus node', r.json && r.json.ok === true && r.json.data === null,
        JSON.stringify(r.json));

    // query
    await post({ op: 'set', path: '/scores', value: { a: 10, b: 20, c: 30 } });
    r = await post({ op: 'get', path: '/scores', query: { orderBy: '$key', limitToLast: 2 } });
    check('query diteruskan tanpa error', r.json && r.json.ok === true, JSON.stringify(r.json));

    // error: JSON tidak valid
    {
        const rr = await fetch(`http://127.0.0.1:${SRV_PORT}/`, {
            method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{ bukan json }',
        });
        const j = await rr.json();
        check('body JSON tidak valid -> 400 bad_request',
            rr.status === 400 && j.ok === false && j.error === 'bad_request', JSON.stringify(j));
    }

    // error: op tidak dikenal
    r = await post({ op: 'terbang', path: '/' });
    check('op tidak dikenal -> unknown_op', r.json && r.json.ok === false && r.json.error === 'unknown_op',
        JSON.stringify(r.json));

    // 404
    {
        const rr = await fetch(`http://127.0.0.1:${SRV_PORT}/tidak-ada`);
        const j = await rr.json();
        check('endpoint tak dikenal -> 404', rr.status === 404 && j.error === 'not_found', JSON.stringify(j));
    }

    // nxfire-client mode server (--port)
    {
        const CLIENT = path.join(root, 'build', 'client', 'Release', exe('nxfire-client'));
        let rr = spawnSync(CLIENT,
            [JSON.stringify({ op: 'set', path: '/via-client', value: { x: 1 } }),
             '--port', String(SRV_PORT), '--host', '127.0.0.1'],
            { encoding: 'utf8' });
        let j = null; try { j = JSON.parse(rr.stdout.trim()); } catch {}
        check('nxfire-client (mode server) set', j && j.ok === true, rr.stdout.trim());

        rr = spawnSync(CLIENT,
            [JSON.stringify({ op: 'get', path: '/via-client' }), '--port', String(SRV_PORT)],
            { encoding: 'utf8' });
        j = null; try { j = JSON.parse(rr.stdout.trim()); } catch {}
        check('nxfire-client (mode server) get',
            j && j.ok === true && j.data && j.data.x === 1, rr.stdout.trim());
    }
} finally {
    mock.kill();
    server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
