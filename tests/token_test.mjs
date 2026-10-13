// token_test.mjs - uji autentikasi token nxfire-server.
//  - tanpa token -> 401 unauthorized
//  - token salah -> 401
//  - token admin -> ok
//  - token_create/rotate/delete/list (admin saja; user -> 403)
//  - token lama mati setelah rotate

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));

const MOCK_PORT = 9070;
const SRV_PORT = 9083;
const TMP = path.join(here, '.tmp', 'tokens');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.json');
const TOKFILE = path.join(TMP, 'nxfire.tokens.json');   // sesuai CFG = nxfire.json
fs.writeFileSync(CFG, JSON.stringify({ project: 'mock', base_url: `http://127.0.0.1:${MOCK_PORT}`, port: SRV_PORT }));
fs.rmSync(TOKFILE, { force: true });

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
async function post(op, token) {
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers['Authorization'] = `Bearer ${token}`;
    const r = await fetch(`http://127.0.0.1:${SRV_PORT}/`, {
        method: 'POST', headers, body: JSON.stringify(op),
    });
    return { status: r.status, json: await r.json() };
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });

// Auth opt-in: buat file token lewat CLI `nxfire token init` SEBELUM server start.
const NXFIRE = path.join(root, 'build', 'tools', 'nxfire', 'Release', exe('nxfire'));
const init = spawnSync(NXFIRE, ['token', 'init', '--config', CFG], { encoding: 'utf8' });
const adminTok = (init.stdout.match(/nxf_[0-9a-f]{32}/) || [])[0];

const server = spawn(SERVER, ['--config', CFG], { stdio: ['ignore', 'pipe', 'ignore'] });
let serverLog = '';
server.stdout.on('data', (d) => { serverLog += d.toString(); });

if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) ||
    !(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
    console.error('[error] mock/server tidak siap'); mock.kill(); server.kill(); process.exit(2);
}

console.log(`\nUji token server (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

try {
    check('nxfire token init membuat token admin', /^nxf_[0-9a-f]{32}$/.test(adminTok || ''), init.stdout + init.stderr);
    check('file token hanya berisi hash (tanpa teks nxf_)',
        !fs.readFileSync(TOKFILE, 'utf8').includes('nxf_'), '');

    // Tanpa token -> 401
    let r = await post({ op: 'get', path: '/x' });
    check('tanpa token -> 401 unauthorized', r.status === 401 && r.json.error === 'unauthorized', JSON.stringify(r.json));

    // Token salah -> 401
    r = await post({ op: 'get', path: '/x' }, 'nxf_0000000000000000000000000000ffff');
    check('token salah -> 401', r.status === 401, JSON.stringify(r.json));

    // Token admin -> ok
    r = await post({ op: 'get', path: '/x' }, adminTok);
    check('token admin -> ok:true', r.status === 200 && r.json.ok === true, JSON.stringify(r.json));

    // token_create user
    r = await post({ op: 'token_create', user: 'budi', role: 'user' }, adminTok);
    const userTok = r.json && r.json.data && r.json.data.token;
    check('token_create -> token sekali tampil', r.status === 200 && r.json.ok === true && /^nxf_[0-9a-f]{32}$/.test(userTok || ''), JSON.stringify(r.json));

    // token_create user yang sama -> already_exists
    r = await post({ op: 'token_create', user: 'budi' }, adminTok);
    check('token_create dobel -> already_exists', r.json && r.json.error === 'already_exists', JSON.stringify(r.json));

    // token_list
    r = await post({ op: 'token_list' }, adminTok);
    check('token_list -> __owner__ + budi',
        r.json && r.json.ok && r.json.data.length === 2 &&
        r.json.data.some((t) => t.user === '__owner__' && t.role === 'admin') &&
        r.json.data.some((t) => t.user === 'budi' && t.role === 'user'), JSON.stringify(r.json));

    // user tidak boleh kelola token -> 403
    r = await post({ op: 'token_create', user: 'jahat', role: 'admin' }, userTok);
    check('user buat token -> 403 forbidden', r.status === 403 && r.json.error === 'forbidden', JSON.stringify(r.json));

    // user boleh operasi data
    r = await post({ op: 'set', table: 'catatan', key: '1', value: { x: 1 } }, userTok);
    check('user set data -> ok', r.status === 200 && r.json.ok === true, JSON.stringify(r.json));

    // rotate -> token lama mati
    r = await post({ op: 'token_rotate', user: 'budi' }, adminTok);
    const newTok = r.json && r.json.data && r.json.data.token;
    check('token_rotate -> token baru', r.status === 200 && /^nxf_[0-9a-f]{32}$/.test(newTok || ''), JSON.stringify(r.json));
    r = await post({ op: 'get', path: '/catatan/1' }, userTok);
    check('token lama setelah rotate -> 401', r.status === 401, JSON.stringify(r.json));
    r = await post({ op: 'get', path: '/catatan/1' }, newTok);
    check('token baru setelah rotate -> ok', r.status === 200 && r.json.ok, JSON.stringify(r.json));

    // delete -> mati
    r = await post({ op: 'token_delete', user: 'budi' }, adminTok);
    check('token_delete -> ok', r.status === 200 && r.json.ok === true, JSON.stringify(r.json));
    r = await post({ op: 'get', path: '/catatan/1' }, newTok);
    check('token setelah delete -> 401', r.status === 401, JSON.stringify(r.json));

    // mode terbuka: tanpa file token, server lama tetap bebas
    fs.rmSync(TOKFILE, { force: true });
    server.kill();
    const open = spawn(SERVER, ['--config', CFG], { stdio: 'ignore' });
    if (!(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
        check('server terbuka menyala', false, '');
    } else {
        const rr = await fetch(`http://127.0.0.1:${SRV_PORT}/`, {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ op: 'get', path: '/x' }),
        });
        const jj = await rr.json();
        check('tanpa file token -> mode terbuka (ok:true)',
            rr.status === 200 && jj.ok === true, JSON.stringify(jj));
    }
    open.kill();
} finally {
    server.kill(); mock.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
