// fase5_test.mjs — uji otomatis Fase 5 (cache GET lokal).

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

const MOCK_PORT = 9061;
const ON_PORT = 9074;
const OFF_PORT = 9075;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG_ON = path.join(TMP, 'nxfire.cache-on.json');
const CFG_OFF = path.join(TMP, 'nxfire.cache-off.json');
const base = { project: 'mock', base_url: `http://127.0.0.1:${MOCK_PORT}` };
fs.writeFileSync(CFG_ON, JSON.stringify({ ...base, port: ON_PORT, cache_ttl: 5 }));
fs.writeFileSync(CFG_OFF, JSON.stringify({ ...base, port: OFF_PORT, cache_ttl: 0 }));

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
async function post(port, op) {
    const r = await fetch(`http://127.0.0.1:${port}/`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(op),
    });
    let j = null; try { j = await r.json(); } catch {}
    return j;
}
async function directPut(p, val) {
    await fetch(`http://127.0.0.1:${MOCK_PORT}${p}.json`, {
        method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(val),
    });
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
const srvOn = spawn(SERVER, ['--config', CFG_ON], { stdio: 'ignore' });
const srvOff = spawn(SERVER, ['--config', CFG_OFF], { stdio: 'ignore' });

if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) ||
    !(await waitReady(`http://127.0.0.1:${ON_PORT}/health`)) ||
    !(await waitReady(`http://127.0.0.1:${OFF_PORT}/health`))) {
    console.error('[error] mock/server tidak siap');
    mock.kill(); srvOn.kill(); srvOff.kill(); process.exit(2);
}

console.log(`\nUji Fase 5 — cache GET (mock :${MOCK_PORT}, on :${ON_PORT}, off :${OFF_PORT})\n`);

try {
    // --- Cache ON ---
    await post(ON_PORT, { op: 'set', path: '/c1', value: { v: 1 } });
    let j = await post(ON_PORT, { op: 'get', path: '/c1' });
    check('cache-on: get awal -> v=1', j && j.data && j.data.v === 1, JSON.stringify(j));

    await directPut('/c1', { v: 2 });  // ubah di Firebase tanpa lewat server
    j = await post(ON_PORT, { op: 'get', path: '/c1' });
    check('cache-on: get kedua dari cache (tetap v=1)', j && j.data && j.data.v === 1, JSON.stringify(j));

    await post(ON_PORT, { op: 'set', path: '/c1', value: { v: 3 } });  // tulis -> cache dibatalkan
    j = await post(ON_PORT, { op: 'get', path: '/c1' });
    check('cache-on: tulis membatalkan cache -> v=3', j && j.data && j.data.v === 3, JSON.stringify(j));

    // --- Cache OFF ---
    await post(OFF_PORT, { op: 'set', path: '/c2', value: { v: 1 } });
    j = await post(OFF_PORT, { op: 'get', path: '/c2' });
    check('cache-off: get awal -> v=1', j && j.data && j.data.v === 1, JSON.stringify(j));

    await directPut('/c2', { v: 9 });
    j = await post(OFF_PORT, { op: 'get', path: '/c2' });
    check('cache-off: get kedua tidak di-cache -> v=9', j && j.data && j.data.v === 9, JSON.stringify(j));
} finally {
    mock.kill(); srvOn.kill(); srvOff.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
