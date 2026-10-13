// fase7_test.mjs — uji otomatis Fase 7 (realtime WebSocket lokal).

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const exe = (n) => (process.platform === 'win32' ? `${n}.exe` : n);
const SERVER = path.join(root, 'build', 'server', 'Release', exe('nxfire-server'));

if (!fs.existsSync(SERVER)) {
    console.error(`[error] server tidak ditemukan: ${SERVER}`);
    process.exit(2);
}

const MOCK_PORT = 9063;
const SRV_PORT = 9077;
const TMP = path.join(here, '.tmp');
fs.mkdirSync(TMP, { recursive: true });
const CFG = path.join(TMP, 'nxfire.fase7.json');
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

const mock = spawn(process.execPath, [path.join(here, 'mock_firebase.mjs'), String(MOCK_PORT)], { stdio: 'ignore' });
const server = spawn(SERVER, ['--config', CFG, '--ws-poll-ms', '200'], { stdio: 'ignore' });

if (!(await waitReady(`http://127.0.0.1:${MOCK_PORT}/.json`)) ||
    !(await waitReady(`http://127.0.0.1:${SRV_PORT}/health`))) {
    console.error('[error] mock/server tidak siap');
    mock.kill(); server.kill(); process.exit(2);
}

console.log(`\nUji Fase 7 — realtime WebSocket (mock :${MOCK_PORT}, server :${SRV_PORT})\n`);

try {
    const msgs = [];
    const ws = new WebSocket(`ws://127.0.0.1:${SRV_PORT}/ws`);
    ws.onmessage = (ev) => { try { msgs.push(JSON.parse(ev.data)); } catch {} };
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });

    const waitFor = async (pred, ms = 4000) => {
        const t0 = Date.now();
        while (Date.now() - t0 < ms) {
            const m = msgs.find(pred);
            if (m) return m;
            await new Promise((r) => setTimeout(r, 50));
        }
        return null;
    };

    check('welcome diterima', !!(await waitFor((m) => m.type === 'welcome')), JSON.stringify(msgs));

    ws.send(JSON.stringify({ subscribe: '/live' }));
    check('subscribed diterima', !!(await waitFor((m) => m.type === 'subscribed' && m.path === '/live')), JSON.stringify(msgs));
    check('nilai awal (value) diterima', !!(await waitFor((m) => m.type === 'value' && m.path === '/live')), JSON.stringify(msgs));

    await post({ op: 'set', path: '/live', value: { v: 1 } });
    let m = await waitFor((x) => x.type === 'change' && x.path === '/live' && x.data && x.data.v === 1);
    check('perubahan v=1 didorong', !!m, JSON.stringify(msgs));

    await post({ op: 'set', path: '/live', value: { v: 2 } });
    m = await waitFor((x) => x.type === 'change' && x.path === '/live' && x.data && x.data.v === 2);
    check('perubahan v=2 didorong', !!m, JSON.stringify(msgs));

    ws.close();

    // nxfire-watch (klien WS C++)
    const WATCH = path.join(root, 'build', 'tools', 'watch', 'Release', exe('nxfire-watch'));
    if (fs.existsSync(WATCH)) {
        const watch = spawn(WATCH, ['/wpath', '--port', String(SRV_PORT)], { stdio: ['ignore', 'pipe', 'ignore'] });
        let out = '';
        watch.stdout.on('data', (d) => (out += d.toString()));
        await new Promise((r) => setTimeout(r, 700));
        await post({ op: 'set', path: '/wpath', value: { z: 42 } });
        await new Promise((r) => setTimeout(r, 900));
        watch.kill();
        check('nxfire-watch mencetak perubahan', out.includes('"change"') && out.includes('"z":42'), out);
    } else {
        check('nxfire-watch ada', false, WATCH);
    }
} finally {
    mock.kill();
    server.kill();
}

console.log(`\nHasil: ${pass} PASS, ${fail} FAIL\n`);
process.exit(fail ? 1 : 0);
