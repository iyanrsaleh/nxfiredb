// insert_key_test.mjs — uji cara menentukan kunci pada "insert":
//   keyFrom (ambil dari sebuah field), autoId (nomor urut), key eksplisit, atau acak.

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));
const CLIENT = path.join(root, 'build', 'client', 'Release', exe('nxfire-client'));

const MOCK_PORT = 9067;
const SRV_PORT = 9080;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.insertkey.json');
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

console.log(`\nUji cara penentuan kunci pada insert (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

let r;
try {
    // 1) Default: kunci acak ala Firebase (perilaku lama tetap).
    //    Respons push dari Firebase: {"name":"<kunci-acak>"}.
    r = await post({ op: 'insert', table: 'raw', data: { name: 'budi' } });
    const autoKey = r && r.data && r.data.name;
    check('insert tanpa kunci -> kunci acak (terserah Firebase)', !!autoKey && autoKey.length > 10, JSON.stringify(r));
    r = await post({ op: 'get', path: `/raw/${autoKey}` });
    check('kunci acak: datanya tersimpan', r && r.ok && r.data && r.data.name === 'budi', JSON.stringify(r));

    // 2) keyFrom: field "name" menjadi kunci -> /users/budi
    r = await post({ op: 'insert', table: 'users', keyFrom: 'name', data: { name: 'budi', age: 20 } });
    check('insert keyFrom -> ok', r && r.ok === true, JSON.stringify(r));
    r = await post({ op: 'get', path: '/users/budi' });
    check('keyFrom: tersimpan di /users/budi', r && r.ok && r.data && r.data.age === 20, JSON.stringify(r));
    r = await post({ op: 'get', table: 'users', key: 'budi' });
    check('keyFrom: tabel+key "budi" ketemu', r && r.ok && r.data.name === 'budi', JSON.stringify(r));

    // 3) autoId: nomor urut otomatis 1, 2, 3...
    r = await post({ op: 'insert', table: 'urut', autoId: true, data: { nama: 'satu' } });
    check('autoId pertama -> /urut/1', !!(r && r.ok), JSON.stringify(r));
    r = await post({ op: 'get', path: '/urut/1' });
    check('autoId: /urut/1 = "satu"', r && r.data && r.data.nama === 'satu', JSON.stringify(r));

    r = await post({ op: 'insert', table: 'urut', autoId: true, data: { nama: 'dua' } });
    r = await post({ op: 'get', path: '/urut/2' });
    check('autoId: /urut/2 = "dua" (naik +1)', r && r.data && r.data.nama === 'dua', JSON.stringify(r));

    // autoId tetap menaik walau ada kunci non-angka
    await post({ op: 'set', table: 'urut', key: 'catatan', data: { x: 1 } });
    r = await post({ op: 'insert', table: 'urut', autoId: true, data: { nama: 'tiga' } });
    r = await post({ op: 'get', path: '/urut/3' });
    check('autoId: lompati kunci non-angka -> /urut/3', r && r.data && r.data.nama === 'tiga', JSON.stringify(r));

    // 4) key eksplisit pada insert = set di /table/key (bukan push lagi)
    r = await post({ op: 'insert', table: 'users', key: 'citra', data: { name: 'citra' } });
    check('insert + key eksplisit -> ok', r && r.ok === true, JSON.stringify(r));
    r = await post({ op: 'get', path: '/users/citra' });
    check('insert + key: langsung di /users/citra', r && r.data && r.data.name === 'citra', JSON.stringify(r));

    // 5) keyFrom yang tidak ada di data -> bad_request (tidak menulis sampah)
    r = await post({ op: 'insert', table: 'users', keyFrom: 'nim', data: { name: 'x' } });
    check('keyFrom tak ada -> bad_request', r && r.ok === false && r.error === 'bad_request', JSON.stringify(r));

    // 6) client embedded (tanpa server) ikut mendukung.
    const jr = runClient({ op: 'insert', table: 'raw', data: { name: 'eko' } });
    const jk = jr && jr.data && jr.data.name;
    check('client embedded: insert tanpa kunci -> acak', !!jk && jk.length > 10, JSON.stringify(jr));

    const j = runClient({ op: 'insert', table: 'users', keyFrom: 'name', data: { name: 'dewi', age: 40 } });
    check('client embedded: insert keyFrom', j && j.ok === true, JSON.stringify(j));
    const j2 = runClient({ op: 'get', path: '/users/dewi' });
    check('client embedded: /users/dewi ada', j2 && j2.data && j2.data.age === 40, JSON.stringify(j2));
} finally {
    mock.kill(); server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
