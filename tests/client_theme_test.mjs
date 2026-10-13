// client_theme_test.mjs — uji tema warna terminal Ubuntu pada nxfire-client.
//  - output dialihkan (pipe)  -> JSON ringkas, TANPA warna (aman untuk skrip)
//  - --color always           -> memakai ANSI/truecolor
//  - --plain                  -> tanpa warna walau TTY

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const CLIENT = path.join(root, 'build', 'client', 'Release', exe('nxfire-client'));

if (!fs.existsSync(CLIENT)) {
    console.error(`[error] nxfire-client tidak ditemukan: ${CLIENT}\n        build dulu: scripts/build.bat`);
    process.exit(2);
}

const MOCK_PORT = 9069;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.theme.json');
fs.writeFileSync(CFG, JSON.stringify({ project: 'mock', base_url: `http://127.0.0.1:${MOCK_PORT}`, port: 9090 }));

let pass = 0, fail = 0;
function check(name, cond, detail) {
    if (cond) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}${detail ? '  -> ' + detail : ''}`); }
}
function run(args) {
    return spawnSync(CLIENT, args, { encoding: 'utf8' });
}
async function waitReady(u, tries = 40) {
    for (let i = 0; i < tries; i++) {
        try { const r = await fetch(u); if (r.ok) return true; } catch {}
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`))) {
    console.error('[error] mock tidak siap'); mock.kill(); process.exit(2);
}

console.log(`\nUji tema warna terminal Ubuntu (nxfire-client, mock :${MOCK_PORT})\n`);

try {
    await fetch(`http://127.0.0.1:${MOCK_PORT}/seed.json`, { method: 'PUT', body: JSON.stringify({ a: 1, b: 'dua' }) });

    // 1) default (piped) -> JSON ringkas tanpa ESC
    let r = run(['{"op":"get","path":"/seed"}', '--config', CFG]);
    let json = null; try { json = JSON.parse(r.stdout.trim()); } catch {}
    check('piped: JSON ringkas valid', json && json.ok === true && json.data && json.data.b === 'dua', r.stdout);
    check('piped: tanpa kode ANSI', !r.stdout.includes('\x1b['), JSON.stringify(r.stdout.slice(0, 40)));

    // 2) --color always -> ada ANSI + tetap memuat data
    r = run(['{"op":"get","path":"/seed"}', '--config', CFG, '--color', 'always']);
    check('--color always: memakai ANSI', r.stdout.includes('\x1b['), JSON.stringify(r.stdout.slice(0, 40)));
    check('--color always: header NXFIREDB oranye Ubuntu',
        r.stdout.includes('\x1b[1;38;2;233;84;32mNXFIREDB'), JSON.stringify(r.stdout.slice(0, 80)));
    check('--color always: label ok hijau', r.stdout.includes('\x1b[1;38;2;138;226;52mok'), '');
    check('--color always: string nilai kuning', r.stdout.includes('\x1b[38;2;252;233;79m"dua"'), '');

    // 3) --plain -> tanpa warna walau --color selalu diminta sebelumnya
    r = run(['{"op":"get","path":"/seed"}', '--config', CFG, '--plain']);
    check('--plain: tanpa ANSI', !r.stdout.includes('\x1b['), JSON.stringify(r.stdout.slice(0, 40)));

    // 4) error -> label error merah
    r = run(['{"op":"terbang","path":"/seed"}', '--config', CFG, '--color', 'always']);
    check('error: label merah', r.stdout.includes('\x1b[1;38;2;239;41;41merror'), '');
    check('error: "ok":false tetap benar di JSON',
        r.stdout.includes('\x1b[1;38;2;239;41;41mfalse'), '');

    // 5) JSON tidak valid -> tetap JSON ringkas di mode piped
    r = run(['{bukan json}', '--config', CFG]);
    let j2 = null; try { j2 = JSON.parse(r.stdout.trim()); } catch {}
    check('input salah: JSON error rapi', j2 && j2.ok === false && j2.error === 'bad_request', r.stdout);
} finally {
    mock.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
