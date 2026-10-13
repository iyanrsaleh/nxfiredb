// clients_test.mjs — uji SDK klien (Python & Node) terhadap server lokal.

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));

const MOCK_PORT = 9062;
const SRV_PORT = 9076;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.clients.json');
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

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
const server = spawn(SERVER, ['--config', CFG], { stdio: 'ignore' });

if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) ||
    !(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
    console.error('[error] mock/server tidak siap');
    mock.kill(); server.kill(); process.exit(2);
}

console.log(`\nUji SDK klien (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

function runJson(cmd, args) {
    const r = spawnSync(cmd, args, { encoding: 'utf8' });
    let j = null; try { j = JSON.parse(r.stdout.trim()); } catch {}
    return { status: r.status, out: r.stdout.trim(), err: r.stderr, json: j };
}

try {
    // ---------------- Python ----------------
    const py = path.join(root, 'clients', 'python', 'nxfire.py');
    const pyCmd = 'py';  // Python Launcher (Windows)
    let r = runJson(pyCmd, [py, JSON.stringify({ op: 'set', path: '/py', value: { v: 1 } }), '--port', String(SRV_PORT)]);
    check('python: set', r.json && r.json.ok === true, r.out || r.err);

    r = runJson(pyCmd, [py, JSON.stringify({ op: 'get', path: '/py' }), '--port', String(SRV_PORT)]);
    check('python: get', r.json && r.json.data && r.json.data.v === 1, r.out || r.err);

    r = runJson(pyCmd, [py, JSON.stringify({ op: 'update', path: '/py', value: { v: 2 } }), '--port', String(SRV_PORT)]);
    r = runJson(pyCmd, [py, JSON.stringify({ op: 'get', path: '/py' }), '--port', String(SRV_PORT)]);
    check('python: update', r.json && r.json.data && r.json.data.v === 2, r.out || r.err);

    r = runJson(pyCmd, [py, JSON.stringify({ op: 'remove', path: '/py' }), '--port', String(SRV_PORT)]);
    r = runJson(pyCmd, [py, JSON.stringify({ op: 'get', path: '/py' }), '--port', String(SRV_PORT)]);
    check('python: remove', r.json && r.json.ok === true && r.json.data === null, r.out || r.err);

    // ---------------- Node ----------------
    const js = path.join(root, 'clients', 'javascript', 'nxfire.js');
    r = runJson(process.execPath, [js, JSON.stringify({ op: 'set', path: '/js', value: { v: 1 } }), '--port', String(SRV_PORT)]);
    check('node: set', r.json && r.json.ok === true, r.out || r.err);

    r = runJson(process.execPath, [js, JSON.stringify({ op: 'get', path: '/js' }), '--port', String(SRV_PORT)]);
    check('node: get', r.json && r.json.data && r.json.data.v === 1, r.out || r.err);

    r = runJson(process.execPath, [js, JSON.stringify({ op: 'push', path: '/js/chat', value: { text: 'hi' } }), '--port', String(SRV_PORT)]);
    const key = r.json && r.json.data && r.json.data.name;
    check('node: push', !!key, r.out || r.err);

    r = runJson(process.execPath, [js, JSON.stringify({ op: 'remove', path: '/js' }), '--port', String(SRV_PORT)]);
    check('node: remove', r.json && r.json.ok === true, r.out || r.err);
} finally {
    mock.kill();
    server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
