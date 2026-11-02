// Drives GigaEditrPlus.html in headless Edge/Chrome against an in-memory fake mod folder (tests/gigaeditr_plus_browser_body.html):
// open base + mods, edit a tileset (art + walk-blocking), edit an object's art, and check the records and files the editor writes.
// Usage: node tests/gigaeditr_plus_browser.js        (set BROWSER=<path to msedge.exe/chrome.exe> if it is not found)
const fs = require('fs'), os = require('os'), path = require('path'), assert = require('assert'), { spawnSync } = require('child_process');
const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'GigaEditrPlus.html'), 'utf8');
const body = fs.readFileSync(path.join(__dirname, 'gigaeditr_plus_browser_body.html'), 'utf8');
const tmp = path.join(os.tmpdir(), 'gigaeditr_plus_browser.html');
fs.writeFileSync(tmp, html.replace('</body>', body + '</body>'));
const browser = process.env.BROWSER || ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe'].find(p => fs.existsSync(p));
if (!browser) { console.log('gigaeditr_plus_browser: no browser found, skipped'); process.exit(0); }
const r = spawnSync(browser, ['--headless=new', '--disable-gpu', '--virtual-time-budget=60000', '--window-size=1500,900', '--dump-dom', 'file:///' + tmp.replace(/\\/g, '/')], { encoding: 'utf8', maxBuffer: 1 << 26 });
const m = /data-result="([^"]*)"/.exec(r.stdout || ''); assert(m, 'the page produced no result');
const out = JSON.parse(m[1].replace(/&quot;/g, '"').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>'));
assert(!out.error, out.error);
assert.deepStrictEqual(out.mods, ['m']);
assert(out.tsOpen && out.colInit, 'tileset editor opened with the base collision');
assert.deepStrictEqual(out.recs, ['tileset:add:m:tileset/e1@assets/tiles_e1.png', 'collision:add:m:collision/e1@assets/collision_e1.png', 'area:patch:base:area/0 ts=m:tileset/e1 col=m:collision/e1']);
assert.deepStrictEqual(out.areaNow, ['m:tileset/e1', 'm:collision/e1']);
assert(out.closed && out.objOpen);
assert.strictEqual(out.objNow, 'm:objtype/e1');
assert.deepStrictEqual(out.assets2, ['tiles_e1.png', 'collision_e1.png', 'obj_e1.png']);
console.log('gigaeditr_plus_browser: all passed');
