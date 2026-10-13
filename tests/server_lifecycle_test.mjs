// server_lifecycle_test.mjs — uji `nxfire start / stop / status` (server latar belakang).
// Auto-start diuji dengan APPDATA sementara agar folder Startup asli tidak tersentuh.

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const NXFIRE = path.join(root, 'build', 'tools', 'nxfire', 'Release', exe('nxfire'));

if (!fs.existsSync(NXFIRE)) {
    console.error(`[error] nxfire tidak ditemukan: ${NXFIRE}\n        build dulu: scripts/build.bat`);
    process.exit(2);
}

const MOCK_PORT = 9068;
const SRV_PORT = 9081;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.lifecycle.json');
fs.writeFileSync(CFG, JSON.stringify({ project: 'mock', base_url: `http://127.0.0.1:${MOCK_PORT}`, port: SRV_PORT }));

// APPDATA palsu -> autostartPath() menunjuk ke folder sementara.
const FAKE_APPDATA = path.join(TMP, 'appdata');
fs.mkdirSync(FAKE_APPDATA, { recursive: true });
const ENV = { ...process.env, APPDATA: FAKE_APPDATA, HOME: FAKE_APPDATA };

let pass = 0, fail = 0;
function check(name, cond, detail) {
    if (cond) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}${detail ? '  -> ' + detail : ''}`); }
}
function run(args) {
    return spawnSync(NXFIRE, args, { encoding: 'utf8', env: ENV });
}
async function waitReady(u, tries = 60) {
    for (let i = 0; i < tries; i++) {
        try { const r = await fetch(u); if (r.ok) return true; } catch {}
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}
async function listening(port) {
    try { await fetch(`http://127.0.0.1:${port}/health`); return true; } catch { return false; }
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`))) {
    console.error('[error] mock tidak siap'); mock.kill(); process.exit(2);
}

console.log(`\nUji daur-hidup server (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

try {
    // 1) status awal -> stopped, exit code 1
    let r = run(['status', '--config', CFG]);
    check('status awal -> stopped', r.stdout.includes('Server    : stopped') && r.status === 1, r.stdout);

    // 2) start -> server jalan
    r = run(['start', '--config', CFG]);
    check('start -> server jalan', r.status === 0 && r.stdout.includes('Server running'), r.stdout);
    check('start: port benar-benar mendengarkan', await listening(SRV_PORT));

    // 3) health via HTTP
    const h = await (await fetch(`http://127.0.0.1:${SRV_PORT}/health`)).json().catch(() => ({}));
    check('health ok:true', h && h.ok === true && h.port === SRV_PORT, JSON.stringify(h));

    // 4) start lagi (idempoten)
    r = run(['start', '--config', CFG]);
    check('start kedua -> "already running"', r.status === 0 && r.stdout.includes('already running'), r.stdout);

    // 5) status -> RUNNING, exit code 0
    r = run(['status', '--config', CFG]);
    check('status -> RUNNING (+PID)', r.stdout.includes('Server    : RUNNING') && r.status === 0, r.stdout);

    // 6) autostart lihat/on/off
    r = run(['autostart', '--config', CFG]);
    check('autostart awal -> OFF', r.stdout.includes('Auto-start: OFF'), r.stdout);
    r = run(['autostart', 'on', '--config', CFG]);
    check('autostart on', r.status === 0 && r.stdout.includes('Auto-start ON'), r.stdout);
    const batPath = path.join(FAKE_APPDATA, 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'Startup', 'NxfireDB.bat');
    const created = fs.existsSync(batPath) ? fs.readFileSync(batPath, 'utf8') : '';
    check('autostart menulis file startup', created.includes('nxfire.exe') && created.includes(' start '), created);
    r = run(['autostart', 'off', '--config', CFG]);
    check('autostart off menghapus file', r.stdout.includes('Auto-start OFF') && !fs.existsSync(batPath), r.stdout);

    // 7) stop -> berhenti
    r = run(['stop', '--config', CFG]);
    check('stop -> dihentikan', r.status === 0 && r.stdout.includes('Server stopped'), r.stdout);
    check('stop: port tidak lagi mendengarkan', !(await listening(SRV_PORT)));
    r = run(['status', '--config', CFG]);
    check('status akhir -> stopped', r.stdout.includes('Server    : stopped'), r.stdout);

    // 8) stop saat sudah mati (aman)
    r = run(['stop', '--config', CFG]);
    check('stop kedua -> "not running"', r.status === 0 && r.stdout.includes('not running'), r.stdout);
} finally {
    run(['stop', '--config', CFG]);
    mock.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
