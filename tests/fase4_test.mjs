// fase4_test.mjs — uji otomatis Fase 4 (query & pemetaan error).

import { spawn } from 'node:child_process';
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

const MOCK_PORT = 9060;
const SRV_PORT = 9073;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.fase4.json');
fs.writeFileSync(CFG, JSON.stringify({ project: 'mock', base_url: `http://127.0.0.1:${MOCK_PORT}`, port: SRV_PORT }));

let pass = 0, fail = 0;
function check(name, cond, detail) {
    if (cond) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}${detail ? '  -> ' + detail : ''}`); }
}

async function waitReady(u, tries = 60) {
    for (let i = 0; i < tries; i++) {
        try { const r = await fetch(u); if (r.ok) return true; } catch {}
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}
async function post(op) {
    const r = await fetch(`http://127.0.0.1:${SRV_PORT}/`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(op),
    });
    let j = null; try { j = await r.json(); } catch {}
    return { status: r.status, json: j };
}
const keysOf = (j) => (j && j.data && typeof j.data === 'object' ? Object.keys(j.data).sort() : []);

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
const server = spawn(SERVER, ['--config', CFG], { stdio: 'ignore' });

if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) ||
    !(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
    console.error('[error] mock/server tidak siap');
    mock.kill(); server.kill(); process.exit(2);
}

console.log(`\nUji Fase 4 — query & pemetaan error (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

try {
    await post({ op: 'set', path: '/users', value: { budi: { age: 30 }, ani: { age: 25 }, cici: { age: 35 } } });

    let r = await post({ op: 'get', path: '/users', query: { orderBy: 'age', limitToFirst: 2 } });
    check('orderBy=age & limitToFirst=2 -> ani,budi',
        JSON.stringify(keysOf(r.json)) === JSON.stringify(['ani', 'budi']), JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users', query: { orderBy: 'age', startAt: 30 } });
    check('orderBy=age & startAt=30 -> budi,cici',
        JSON.stringify(keysOf(r.json)) === JSON.stringify(['budi', 'cici']), JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users', query: { orderBy: 'age', endAt: 30 } });
    check('orderBy=age & endAt=30 -> ani,budi',
        JSON.stringify(keysOf(r.json)) === JSON.stringify(['ani', 'budi']), JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users', query: { orderBy: 'age', equalTo: 25 } });
    check('orderBy=age & equalTo=25 -> ani',
        JSON.stringify(keysOf(r.json)) === JSON.stringify(['ani']), JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users', query: { orderBy: '$key', limitToLast: 1 } });
    check('orderBy=$key & limitToLast=1 -> cici',
        JSON.stringify(keysOf(r.json)) === JSON.stringify(['cici']), JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users', query: { shallow: true } });
    check('shallow=true -> { anak: true }',
        r.json && r.json.data && r.json.data.budi === true && Object.keys(r.json.data).length === 3,
        JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/users', query: { limitToFirst: 1 } });
    check('limitToFirst tanpa orderBy diabaikan (Firebase) -> 3 anak',
        keysOf(r.json).length === 3, JSON.stringify(r.json));

    // Pemetaan error.
    r = await post({ op: 'get', path: '/_deny' });
    check('403 -> permission_denied', r.json && r.json.ok === false && r.json.error === 'permission_denied',
        JSON.stringify(r.json));

    r = await post({ op: 'get', path: '/_unauth' });
    check('401 -> unauthorized', r.json && r.json.ok === false && r.json.error === 'unauthorized',
        JSON.stringify(r.json));
} finally {
    mock.kill();
    server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
