// table_syntax_test.mjs — uji sintaks ramah-tabel (table/key/data) dan alias op.
// Membuktikan hasilnya SAMA dengan sintaks path gaya Firebase.

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));
const CLIENT = path.join(root, 'build', 'client', 'Release', exe('nxfire-client'));

const MOCK_PORT = 9066;
const SRV_PORT = 9079;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.table.json');
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
    return r.json();
}
function runClient(op) {
    const r = spawnSync(CLIENT, [JSON.stringify(op), '--config', CFG], { encoding: 'utf8' });
    let j = null; try { j = JSON.parse(r.stdout.trim()); } catch {}
    return j;
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
const server = spawn(SERVER, ['--config', CFG], { stdio: 'ignore' });
if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) ||
    !(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
    console.error('[error] mock/server tidak siap'); mock.kill(); server.kill(); process.exit(2);
}

console.log(`\nUji sintaks ramah-tabel (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

try {
    // table + key (persis contoh pengguna)
    let r = await post({ op: 'set', table: 'users', key: 'dantrik', value: { name: 'dantrik', age: 30 } });
    check('set table+key -> ok', r && r.ok === true, JSON.stringify(r));

    // Hasil sama dengan path Firebase /users/dantrik
    r = await post({ op: 'get', path: '/users/dantrik' });
    check('hasil sama dengan path /users/dantrik',
        r && r.ok && r.data && r.data.name === 'dantrik' && r.data.age === 30, JSON.stringify(r));

    // Baca dengan table+key
    r = await post({ op: 'get', table: 'users', key: 'dantrik' });
    check('get table+key -> data benar', r && r.ok && r.data.name === 'dantrik', JSON.stringify(r));

    // Alias "data" menggantikan "value"
    r = await post({ op: 'set', table: 'users', key: 'ani', data: { name: 'ani', age: 25 } });
    check('set memakai alias "data"', r && r.ok === true, JSON.stringify(r));

    // insert -> push (kunci auto) di dalam tabel
    r = await post({ op: 'insert', table: 'users', data: { name: 'budi' } });
    const autoKey = r && r.data && r.data.name;
    check('insert -> push (kunci auto)', !!autoKey, JSON.stringify(r));

    // select -> get
    r = await post({ op: 'select', table: 'users' });
    check('select -> data tabel (dantrik, ani, +auto)',
        r && r.ok && r.data && r.data.dantrik && r.data.ani, JSON.stringify(r));

    // update table+key
    r = await post({ op: 'update', table: 'users', key: 'dantrik', data: { age: 31 } });
    r = await post({ op: 'get', table: 'users', key: 'dantrik' });
    check('update -> merge (age=31, name tetap)',
        r && r.data && r.data.age === 31 && r.data.name === 'dantrik', JSON.stringify(r));

    // delete -> remove
    r = await post({ op: 'delete', table: 'users', key: 'dantrik' });
    r = await post({ op: 'get', table: 'users', key: 'dantrik' });
    check('delete -> node hilang', r && r.ok && r.data === null, JSON.stringify(r));

    // table tanpa key -> seluruh tabel
    r = await post({ op: 'set', table: 'config', data: { theme: 'dark' } });
    r = await post({ op: 'get', path: '/config' });
    check('table tanpa key -> node /config', r && r.ok && r.data.theme === 'dark', JSON.stringify(r));

    // client embedded (tanpa server) juga mendukung table+key
    const j = runClient({ op: 'set', table: 'users', key: 'citra', value: { name: 'citra' } });
    check('client embedded: set table+key', j && j.ok === true, JSON.stringify(j));
    const j2 = runClient({ op: 'get', path: '/users/citra' });
    check('client embedded: hasil sama dengan path', j2 && j2.ok && j2.data.name === 'citra', JSON.stringify(j2));

    // op tak dikenal tetap error
    r = await post({ op: 'terbang', table: 'users' });
    check('op tak dikenal -> unknown_op', r && r.ok === false && r.error === 'unknown_op', JSON.stringify(r));

    // tanpa path & tanpa table -> bad_request
    r = await post({ op: 'get' });
    check('tanpa path/table -> bad_request', r && r.ok === false && r.error === 'bad_request', JSON.stringify(r));
} finally {
    mock.kill(); server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
