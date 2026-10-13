// run_all.mjs — jalankan semua uji fase secara berurutan.

import { spawnSync } from 'node:child_process';
import path from 'node:path';
import url from 'node:url';

const here = path.dirname(url.fileURLToPath(import.meta.url));
const tests = ['fase1_test.mjs', 'fase2_test.mjs', 'fase3_test.mjs', 'fase4_test.mjs', 'fase5_test.mjs', 'fase7_test.mjs', 'fase7b_offline_test.mjs', 'table_syntax_test.mjs', 'insert_key_test.mjs', 'server_lifecycle_test.mjs', 'client_theme_test.mjs', 'token_test.mjs', 'clients_test.mjs'];

let failed = 0;
for (const t of tests) {
    console.log(`\n================ ${t} ================`);
    const r = spawnSync(process.execPath, [path.join(here, t)], { stdio: 'inherit' });
    if (r.status !== 0) failed++;
}

console.log(`\n===========================================`);
console.log(failed ? `SELESAI: ${failed} berkas uji GAGAL` : 'SELESAI: semua uji LOLOS');
process.exit(failed ? 1 : 0);
