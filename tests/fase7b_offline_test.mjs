// fase7b_offline_test.mjs — uji antrian offline (sisa Fase 7).
//
// Skenario: Firebase (mock) MATI saat server jalan. Tulis masuk antrian,
// lalu mock dinyalakan -> antrian dikirim ulang otomatis.

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));

const MOCK_PORT = 9065;
const SRV_PORT = 9078;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.offline.json');
const QUEUE = path.join(TMP, 'nxfire.queue.json');
fs.rmSync(QUEUE, { force: true });
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

// Server start TANPA mock (Firebase mati).
const server = spawn(SERVER, ['--config', CFG, '--retry-ms', '300'], { stdio: 'ignore' });
if (!(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
    console.error('[error] server tidak siap'); server.kill(); process.exit(2);
}

console.log(`\nUji antrian offline (server :${SRV_PORT}, mock :${MOCK_PORT} mati dulu)\n`);

let mock = null;
try {
    // Tulis saat Firebase mati -> masuk antrian, balasan ok+queued.
    let r = await post({ op: 'set', path: '/q', value: { v: 1 } });
    check('tulis saat offline -> queued:true', r && r.ok === true && r.queued === true, JSON.stringify(r));

    // Baca tetap gagal (tidak diantri).
    r = await post({ op: 'get', path: '/q' });
    check('baca saat offline -> network_error', r && r.ok === false && r.error === 'network_error', JSON.stringify(r));

    // Berkas antrian berisi 1 entri.
    await new Promise((res) => setTimeout(res, 300));
    const qtext = fs.existsSync(QUEUE) ? fs.readFileSync(QUEUE, 'utf8') : '';
    let qarr = null; try { qarr = JSON.parse(qtext); } catch {}
    check('berkas antrian berisi 1 entri', Array.isArray(qarr) && qarr.length === 1, qtext);

    // Nyalakan Firebase -> antrian harus terkirim ulang otomatis.
    mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
    if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`))) throw new Error('mock tidak siap');

    // Tunggu beberapa siklus retry.
    let flushed = false;
    for (let i = 0; i < 40; i++) {
        await new Promise((res) => setTimeout(res, 200));
        const rr = await post({ op: 'get', path: '/q' });
        if (rr && rr.ok && rr.data && rr.data.v === 1) { flushed = true; break; }
    }
    check('antrian terkirim ulang setelah Firebase hidup', flushed, 'data tidak muncul');

    // Berkas antrian kembali kosong.
    await new Promise((res) => setTimeout(res, 300));
    const qtext2 = fs.existsSync(QUEUE) ? fs.readFileSync(QUEUE, 'utf8') : '';
    let qarr2 = null; try { qarr2 = JSON.parse(qtext2); } catch {}
    check('berkas antrian kosong setelah terkirim', Array.isArray(qarr2) && qarr2.length === 0, qtext2);
} catch (e) {
    check('skenario berjalan', false, String(e.message || e));
} finally {
    if (mock) mock.kill();
    server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
