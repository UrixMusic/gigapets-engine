// Drives GigaEditrPlus.html in headless Edge/Chrome against an in-memory fake mod folder (tests/gigaeditr_plus_browser_body.html):
// open base + mods, edit a tileset (art + walk-blocking), edit an object's art, and check the records and files the editor writes.
// Usage: node tests/gigaeditr_plus_browser.js        (set BROWSER=<path to msedge.exe/chrome.exe> if it is not found)
const fs = require('fs'), os = require('os'), path = require('path'), assert = require('assert'), { spawnSync } = require('child_process');
const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'GigaEditrPlus.html'), 'utf8');
const body = fs.readFileSync(path.join(__dirname, 'gigaeditr_plus_browser_body.html'), 'utf8');
const tmp = path.join(os.tmpdir(), 'gigaeditr_plus_browser.html');
fs.writeFileSync(tmp, html.replace('</body>', body + '</body>'));
const browser = process.env.BROWSER || ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(p => fs.existsSync(p));
if (!browser) { console.log('gigaeditr_plus_browser: no browser found, skipped'); process.exit(0); }
const r = spawnSync(browser, ['--headless=new', '--disable-gpu', '--user-data-dir=' + fs.mkdtempSync(path.join(os.tmpdir(), 'ge_profile_')), '--virtual-time-budget=60000', '--window-size=1500,900', '--dump-dom', 'file:///' + tmp.replace(/\\/g, '/')], { encoding: 'utf8', maxBuffer: 1 << 26 });
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
assert(out.shOpen, 'the shadow drawer opens with a starting ellipse');
assert.strictEqual(out.shadowRec, '{"type":"objtype","mode":"add","id":"m:objtype/e1","png":"assets/obj_e1.png","anchorX":8,"anchorY":15,"dx":0,"dy":0,"shadow_png":"assets/shadow_e1.png"}');
assert.strictEqual(out.shadowNow, 'picture:assets/shadow_e1.png');
assert(out.assets3.includes('shadow_e1.png'));
assert.strictEqual(out.shadowNone, 'none');
assert.strictEqual(out.resume0, false); assert(out.resume1, 'the "remember where the pet is" checkbox writes an area patch');
assert(out.petsLoaded, 'pets.json is read next to base.json');
assert.strictEqual(out.ceSwatches, 4); assert(out.ceChanged, 'the colour input changes the word');
assert.strictEqual(out.palRec, '{"type":"palette","mode":"add","id":"m:pal/ember","based_on":479,"set":{"1":[0,255,0]},"species":"dog"}');
assert.strictEqual(out.machineRec, '{"type":"minigame","mode":"patch","id":"base:minigame/colour_machine","list_add":{"presets":["m:pal/ember"]}}');
assert(out.palInWorld && out.pcList === 1 && out.ceEditWords && out.palGone);
assert.strictEqual(out.tbRows0, 1);
assert.strictEqual(out.findRows, '[{"type":"find","mode":"add","id":"m:find/e1","item":"base:item/2","weight":55},{"type":"find","mode":"patch","id":"base:find/1","weight":8}]');
assert(out.findText, 'a row shows the item by its name');
assert.strictEqual(out.qsRec, '[{"type":"quest_spawn","mode":"add","id":"m:quest_spawn/e1","quest":26,"area":"base:area/0","item":"base:item/1","state":"buried","random":true,"times":3}]');
assert(out.spotEditHidden, 'a base dig spot cannot be edited');
assert.strictEqual(out.spotRec, '[{"type":"spot","mode":"remove","id":"base:spot/2"}]'); assert.strictEqual(out.removedShown, 1);
assert.strictEqual(out.nqmin0, '17');
assert.strictEqual(out.npcRec, '{"type":"npc","mode":"replace","id":"base:npc/9","quest_min":0,"quest_max":40}');
assert.strictEqual(JSON.parse(out.digRec).diggable, true); assert(out.digGone);
console.log('gigaeditr_plus_browser: all passed');
