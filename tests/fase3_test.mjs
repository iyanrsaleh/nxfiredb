// fase3_test.mjs — uji otomatis Fase 3 (menu & alat nxfire).

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

const MOCK_PORT = 9059;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.fase3.json');
fs.writeFileSync(CFG, JSON.stringify({ project: 'mock', base_url: `http://127.0.0.1:${MOCK_PORT}`, port: 9072 }));
const DEAD = path.join(TMP, 'nxfire.dead.json');
fs.writeFileSync(DEAD, JSON.stringify({ project: 'x', base_url: 'http://127.0.0.1:1' }));

let pass = 0, fail = 0;
function check(name, cond, detail) {
    if (cond) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}${detail ? '  -> ' + detail : ''}`); }
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
async function waitReady(u, tries = 40) {
    for (let i = 0; i < tries; i++) {
        try { const r = await fetch(u); if (r.ok) return true; } catch {}
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}
if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`))) {
    console.error('[error] mock tidak siap'); mock.kill(); process.exit(2);
}

console.log(`\nUji Fase 3 — menu & alat nxfire (mock :${MOCK_PORT})\n`);

try {
    // nxfire info
    let r = spawnSync(NXFIRE, ['info', '--config', CFG], { encoding: 'utf8' });
    check('info menampilkan API', r.stdout.includes('NxfireDB API') && r.stdout.includes('POST'), r.stdout);

    // nxfire config
    r = spawnSync(NXFIRE, ['config', '--config', CFG], { encoding: 'utf8' });
    check('config menampilkan port 9072', r.stdout.includes('9072'), r.stdout);

    // nxfire test (sukses)
    r = spawnSync(NXFIRE, ['test', '--config', CFG], { encoding: 'utf8' });
    check('test sukses -> OK', r.status === 0 && r.stdout.includes('OK'), r.stdout);

    // nxfire test (gagal)
    r = spawnSync(NXFIRE, ['test', '--config', DEAD], { encoding: 'utf8' });
    check('test gagal -> status 1 & FAILED', r.status === 1 && r.stdout.includes('FAILED'), r.stdout);

    // menu: tampilkan banner lalu keluar (input "0")
    r = spawnSync(NXFIRE, ['--config', CFG], { encoding: 'utf8', input: '0\n' });
    check('menu menampilkan banner & opsi',
        r.stdout.includes('NXFIREDB') && r.stdout.includes('1. Start server') && r.stdout.includes('0. Exit'),
        r.stdout);

    // menu berwarna: --color memakai palet Ubuntu, --plain mematikan
    r = spawnSync(NXFIRE, ['--color', '--config', CFG], { encoding: 'utf8', input: '0\n' });
    check('menu --color: ANSI oranye Ubuntu',
        r.stdout.includes('\x1b[1;38;2;233;84;32mNXFIREDB'), JSON.stringify(r.stdout.slice(0, 60)));
    check('menu --color: nomor opsi cyan',
        r.stdout.includes('\x1b[1;38;2;52;226;226m1'), '');
    r = spawnSync(NXFIRE, ['--plain', '--config', CFG], { encoding: 'utf8', input: '0\n' });
    check('menu --plain: tanpa ANSI', !r.stdout.includes('\x1b['), '');
    // piped (tanpa flag) -> otomatis tanpa warna
    r = spawnSync(NXFIRE, ['--config', CFG], { encoding: 'utf8', input: '0\n' });
    check('menu piped: otomatis tanpa ANSI', !r.stdout.includes('\x1b['), '');

    // setup: buat config baru lewat input menu
    const NEWCFG = path.join(TMP, 'nxfire.setup.json');
    r = spawnSync(NXFIRE, ['setup', '--config', NEWCFG], {
        encoding: 'utf8',
        input: 'proyek-uji\nasia-southeast1\n\n\n9999\n',
    });
    const made = fs.existsSync(NEWCFG) ? fs.readFileSync(NEWCFG, 'utf8') : '';
    check('setup menulis nxfire.json', made.includes('proyek-uji') && made.includes('9999'), made);
} finally {
    mock.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
