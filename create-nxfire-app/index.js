#!/usr/bin/env node
// create-nxfire-app — MEMASANG NxfireDB ke sistem.
//
//   npm create nxfire-app@latest
//   npm create nxfire-app@latest -- --sdk <folder|file|URL .tar.gz>
//
// NxfireDB dipasang ke folder data pengguna, lalu nxfire-server /
// nxfire-client bisa dipanggil dari mana saja. Tidak membuat folder proyek.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const https = require('https');
const { spawnSync } = require('child_process');

const pkg = require('./package.json');
const IS_WINDOWS = process.platform === 'win32';

// Folder pasang.
const INSTALL_DIR = IS_WINDOWS
    ? path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'NxfireDB')
    : path.join(os.homedir(), '.local', 'share', 'NxfireDB');

// Cache SDK (unduhan).
const CACHE_DIR = path.join(os.homedir(), '.nxfire', 'sdk');

const HELP = `create-nxfire-app ${pkg.version} - memasang NxfireDB ke sistem

Pemakaian:
  npm create nxfire-app@latest [-- opsi]

Opsi:
  --sdk <path|url>    SDK NxfireDB: folder SDK, file .tar.gz, atau URL .tar.gz
  --repo <owner/nama> repo GitHub sumber rilis SDK (default: ${pkg.nxfire.releaseRepo})
  --prefix <dir>      folder pemasangan (default: ${INSTALL_DIR})
  -h, --help          bantuan ini
  -v, --version       versi

Setelah terpasang, gunakan:
  nxfire                              (menu & setup)
  nxfire-server --config nxfire.json
  nxfire-client '{"op":"get","path":"/users"}' --port 9026`;

function parseArgs(argv) {
    const args = { sdk: null, repo: null, prefix: null };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        const value = () => {
            if (i + 1 >= argv.length) fail(`opsi ${a} butuh nilai`);
            return argv[++i];
        };
        if (a === '-h' || a === '--help') { console.log(HELP); process.exit(0); }
        else if (a === '-v' || a === '--version') { console.log(pkg.version); process.exit(0); }
        else if (a === '--sdk') args.sdk = value();
        else if (a === '--repo') args.repo = value();
        else if (a === '--prefix') args.prefix = value();
        else if (a.startsWith('-')) fail(`opsi tidak dikenal: ${a} (lihat --help)`);
        else fail(`argumen tidak dikenal: ${a} (tool ini tidak membuat proyek; lihat --help)`);
    }
    return args;
}

function fail(message) {
    console.error(`\n[error] ${message}`);
    process.exit(1);
}

// ---------------------------------------------------------------------------
// SDK
// ---------------------------------------------------------------------------
const SERVER_BIN = 'nxfire-server' + (IS_WINDOWS ? '.exe' : '');

function isSdk(dir) {
    return !!dir && fs.existsSync(path.join(dir, 'bin', SERVER_BIN));
}

function sdkOs() {
    const arch = process.arch === 'arm64' ? 'arm64' : 'x64';
    if (IS_WINDOWS) return `windows-${arch}`;
    if (process.platform === 'linux') return `linux-${arch}`;
    if (process.platform === 'darwin') return `macos-${arch}`;
    return process.platform;
}

const ASSET_PATTERN = /^nxfire-sdk-([\d.]+)-([a-z0-9-]+)\.tar\.gz$/;

function request(url, headers = {}) {
    return new Promise((resolve, reject) => {
        const token = process.env.GITHUB_TOKEN;
        const opts = {
            headers: {
                'User-Agent': `create-nxfire-app/${pkg.version}`,
                ...(token && url.includes('api.github.com') ? { Authorization: `Bearer ${token}` } : {}),
                ...headers,
            },
        };
        https.get(url, opts, (res) => {
            if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
                res.resume();
                resolve(request(new URL(res.headers.location, url).toString(), headers));
                return;
            }
            if (res.statusCode !== 200) {
                res.resume();
                reject(new Error(`HTTP ${res.statusCode} untuk ${url}`));
                return;
            }
            resolve(res);
        }).on('error', reject);
    });
}

async function fetchJson(url) {
    const res = await request(url, { Accept: 'application/vnd.github+json' });
    let body = '';
    res.setEncoding('utf8');
    for await (const chunk of res) body += chunk;
    return JSON.parse(body);
}

async function download(url, file) {
    const res = await request(url);
    await new Promise((resolve, reject) => {
        const out = fs.createWriteStream(file);
        res.pipe(out);
        out.on('finish', resolve);
        res.on('error', reject);
        out.on('error', reject);
    });
}

function extract(archive, dest) {
    const tar = IS_WINDOWS
        ? path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe')
        : 'tar';
    const tmp = `${dest}.tmp-${process.pid}`;
    fs.rmSync(tmp, { recursive: true, force: true });
    fs.mkdirSync(tmp, { recursive: true });
    const r = spawnSync(tar, ['-xzf', archive, '-C', tmp], { stdio: 'inherit' });
    if (r.status !== 0) {
        fs.rmSync(tmp, { recursive: true, force: true });
        fail(`gagal mengekstrak ${archive}`);
    }
    let root = tmp;
    const entries = fs.readdirSync(tmp);
    if (entries.length === 1 && fs.statSync(path.join(tmp, entries[0])).isDirectory()) {
        root = path.join(tmp, entries[0]);
    }
    if (!isSdk(root)) {
        fs.rmSync(tmp, { recursive: true, force: true });
        fail(`${archive} bukan paket SDK NxfireDB (tidak ada bin/${SERVER_BIN})`);
    }
    fs.rmSync(dest, { recursive: true, force: true });
    fs.renameSync(root, dest);
    fs.rmSync(tmp, { recursive: true, force: true });
    return dest;
}

async function sdkFromArchive(source) {
    const isUrl = /^https?:\/\//.test(source);
    const file = isUrl ? new URL(source).pathname.split('/').pop() : path.basename(source);
    const base = file.replace(/\.(tar\.gz|tgz)$/, '');
    const dest = path.join(CACHE_DIR, base);
    if (isSdk(dest)) {
        console.log(`SDK dari cache: ${dest}`);
        return dest;
    }
    fs.mkdirSync(CACHE_DIR, { recursive: true });
    let archive = source;
    if (isUrl) {
        archive = path.join(CACHE_DIR, `${base}.tar.gz`);
        console.log(`Mengunduh SDK: ${source}`);
        await download(source, archive);
    }
    console.log(`Mengekstrak SDK ke ${dest}`);
    extract(path.resolve(archive), dest);
    if (archive !== source) fs.rmSync(archive, { force: true });
    return dest;
}

function sdkAssets(release) {
    return (release.assets || [])
        .map((a) => ({ url: a.browser_download_url, m: a.name.match(ASSET_PATTERN) }))
        .filter((a) => a.m && a.m[2] === sdkOs());
}

async function sdkFromRelease(repo) {
    console.log(`Mencari rilis SDK terbaru di github.com/${repo} ...`);
    let release = null;
    try {
        release = await fetchJson(`https://api.github.com/repos/${repo}/releases/latest`);
    } catch (e) {
        if (!/HTTP 404/.test(e.message)) fail(`tidak bisa membaca rilis ${repo}: ${e.message}`);
    }
    if (!release || !sdkAssets(release).length) {
        let releases;
        try {
            releases = await fetchJson(`https://api.github.com/repos/${repo}/releases?per_page=20`);
        } catch (e) {
            fail(`tidak bisa membaca rilis ${repo}: ${e.message}`);
        }
        const withSdk = (releases || []).find((r) => !r.draft && sdkAssets(r).length);
        if (!withSdk) fail(`belum ada rilis di ${repo} yang membawa SDK untuk ${sdkOs()}`);
        release = withSdk;
    }
    console.log(`Rilis: ${release.tag_name}${release.prerelease ? ' (pre-release)' : ''}`);
    return sdkFromArchive(sdkAssets(release)[0].url);
}

async function resolveSdk(args) {
    if (args.sdk) {
        if (/^https?:\/\//.test(args.sdk) || /\.(tar\.gz|tgz)$/.test(args.sdk)) {
            return sdkFromArchive(args.sdk);
        }
        if (!isSdk(args.sdk)) fail(`--sdk "${args.sdk}" bukan folder SDK NxfireDB`);
        return path.resolve(args.sdk);
    }
    if (process.env.NXFIRE_SDK) {
        if (!isSdk(process.env.NXFIRE_SDK)) fail(`NXFIRE_SDK="${process.env.NXFIRE_SDK}" bukan folder SDK`);
        return path.resolve(process.env.NXFIRE_SDK);
    }
    const repo = args.repo || (pkg.nxfire && pkg.nxfire.releaseRepo);
    if (repo) return sdkFromRelease(repo);
    fail('SDK NxfireDB tidak ditemukan. Pakai --sdk <folder|file|URL> atau --repo <owner/nama>.');
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
function addToPath(dir) {
    if (IS_WINDOWS) {
        const r = spawnSync('powershell', ['-NoProfile', '-Command',
            `$d='${dir}'; $p=[Environment]::GetEnvironmentVariable('Path','User'); ` +
            `if ($p -notlike '*'+$d+'*') { [Environment]::SetEnvironmentVariable('Path', ($p.TrimEnd(';')+';'+$d), 'User') }`
        ], { stdio: 'inherit' });
        return r.status === 0;
    }
    const bindir = path.join(os.homedir(), '.local', 'bin');
    fs.mkdirSync(bindir, { recursive: true });
    for (const name of ['nxfire', 'nxfire-server', 'nxfire-client', 'nxfire-watch']) {
        const target = path.join(dir, name);
        if (!fs.existsSync(target)) continue;
        const link = path.join(bindir, name);
        try {
            fs.rmSync(link, { force: true });
            fs.symlinkSync(target, link);
            fs.chmodSync(target, 0o755);
        } catch (e) {
            console.warn(`[peringatan] gagal symlink ${name}: ${e.message}`);
        }
    }
    const profile = process.env.SHELL && process.env.SHELL.includes('zsh') ? '.zshrc' : '.bashrc';
    const rcPath = path.join(os.homedir(), profile);
    let rc = '';
    try { rc = fs.readFileSync(rcPath, 'utf8'); } catch { /* belum ada */ }
    if (!rc.includes(bindir)) fs.appendFileSync(rcPath, `\nexport PATH="${bindir}:$PATH"\n`);
    return true;
}

async function install(sdk, prefix) {
    const dest = prefix || INSTALL_DIR;
    const binDest = path.join(dest, 'bin');
    fs.mkdirSync(binDest, { recursive: true });

    const srcBin = path.join(sdk, 'bin');
    for (const name of fs.readdirSync(srcBin)) {
        fs.copyFileSync(path.join(srcBin, name), path.join(binDest, name));
        if (!IS_WINDOWS) fs.chmodSync(path.join(binDest, name), 0o755);
    }

    if (IS_WINDOWS) createLaunchers(binDest);

    console.log(`Memasang ke: ${dest}`);
    if (prefix) {
        console.log('(--prefix dipakai: PATH tidak diubah)');
    } else {
        console.log('Menambahkan ke PATH ...');
        addToPath(binDest);
    }
    return dest;
}

// Launcher .bat supaya perintah bisa dipanggil dari CMD maupun PowerShell.
function createLaunchers(binDest) {
    for (const name of ['nxfire', 'nxfire-server', 'nxfire-client', 'nxfire-watch']) {
        const bat = ['@echo off', `"%~dp0${name}.exe" %*`].join('\r\n') + '\r\n';
        fs.writeFileSync(path.join(binDest, `${name}.bat`), bat);
    }
}

async function main() {
    const args = parseArgs(process.argv.slice(2));

    console.log(`\nNxfireDB installer v${pkg.version}\n`);

    const sdk = await resolveSdk(args);
    const dest = await install(sdk, args.prefix);

    console.log(`\nSelesai. NxfireDB terpasang di: ${dest}`);
    console.log('Buka terminal BARU, lalu jalankan:');
    console.log('  nxfire                              (menu & setup)');
    console.log('  nxfire setup                        (isi project & secret)');
    console.log('  nxfire start                        (server latar belakang, selalu jalan)');
    console.log('  nxfire status / nxfire stop         (cek / hentikan server)');
    console.log('  nxfire autostart on                 (nyala otomatis saat komputer menyala)');
    console.log('  nxfire-client \'{"op":"get","path":"/users"}\' --port 9026');
    console.log('\nKonfigurasi disimpan di nxfire.json (project, secret, port).');
}

main().catch((e) => fail(e.stack || String(e)));
