// Tests the pure logic inside GigaEditrPlus.html (the <script id="core"> block) against a real base export.
// Usage: node tests/gigaeditr_plus_test.js [path to the folder made by gigapets --export-base]
const fs = require('fs'), path = require('path'), assert = require('assert');
const html = fs.readFileSync(path.join(__dirname, '..', 'GigaEditrPlus.html'), 'utf8');
const core = html.match(/<script id="core">([\s\S]*?)<\/script>/)[1];
const mod = { exports: {} };
new Function('module', core)(mod);
const G = mod.exports;

const exportDir = process.argv[2] || path.join(__dirname, '..', 'build', 'base_export');
const base = JSON.parse(fs.readFileSync(path.join(exportDir, 'base.json'), 'utf8'));
const cave = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'mods', 'cavemod', 'data', 'world.json'), 'utf8'));
let fails = 0;
const check = (name, fn) => { try { fn(); } catch (e) { fails++; console.error('FAIL', name, e.message); } };

check('base only', () => {
  const W = G.buildWorld(base, []);
  assert.strictEqual(Object.keys(W.areas).length, 41);
  assert(W.areas['base:area/0'].objects.length > 100);
});

check('cavemod adds an area, an object in base, an npc', () => {
  const W = G.buildWorld(base, [{ id: 'cavemod', files: [{ name: 'world.json', records: cave }] }]);
  const a = W.areas['cavemod:area/cave'];
  assert(a && a.width === 12 && a.height === 8);
  assert(W.areas['base:area/0'].objects.some(o => o.id === 'cavemod:obj/mouth' && o.added));
  assert(a.objects.length === 2);
  assert(W.npcs.some(n => n.id === 'cavemod:npc/keeper' && n.areaId === 'cavemod:area/cave'));
});

check('move, delete and place through the editor ops', () => {
  const recs = [];
  let W = G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  const o = W.areas['base:area/0'].objects[3];
  G.edMoveObject(recs, o, 10.4, 20.6);
  G.edMoveObject(recs, o, 11, 21); // second move updates the same patch
  assert.strictEqual(recs.length, 1);
  assert.deepStrictEqual(recs[0], { type: 'object', mode: 'patch', id: o.id, area: 'base:area/0', x: 11, y: 21 });
  W = G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  const moved = W.areas['base:area/0'].objects.find(x => x.id === o.id);
  assert(moved.x === 11 && moved.y === 21 && moved.mods[0].kind === 'patch');
  assert.strictEqual(W.log[0].diffs.length, 2);

  G.edDeleteObject(recs, moved); // delete drops the patch and adds a remove
  assert.strictEqual(recs.length, 1);
  assert.strictEqual(recs[0].mode, 'remove');
  W = G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  assert(!W.areas['base:area/0'].objects.some(x => x.id === o.id));
  assert.strictEqual(W.removed['base:area/0'].length, 1);

  const id = G.edAddObject(recs, W, 'm', 'base:area/0', o.objtype, 100, 200);
  assert.strictEqual(id, 'm:obj/e1');
  W = G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  const added = W.areas['base:area/0'].objects.find(x => x.id === id);
  assert(added && added.added);
  G.edMoveObject(recs, added, 5, 6); // moving an added object edits its add record
  assert(recs.filter(r => r.id === id).length === 1 && recs.find(r => r.id === id).x === 5);
  G.edDeleteObject(recs, added); // deleting it removes the record entirely
  assert(!recs.some(r => r.id === id));
});

check('paint patches a base area and edits an own area', () => {
  const recs = [];
  G.edPaint(recs, null, 'base:area/0', 2, 3, 7);
  G.edPaint(recs, null, 'base:area/0', 2, 3, 8);
  G.edPaint(recs, null, 'base:area/0', 4, 4, 9);
  assert.deepStrictEqual(recs[0].set_tiles, [[2, 3, 8], [4, 4, 9]]);
  const W = G.buildWorld(base, [{ id: 'm', files: [{ name: 'a.json', records: recs }] }]);
  assert.strictEqual(W.areas['base:area/0'].map[3][2], 8);
  const own = [{ type: 'area', mode: 'add', id: 'm:area/x', tileset: 't', collision: 'c', map: [[0, 0], [0, 0]] }];
  G.edPaint(own, null, 'm:area/x', 1, 1, 3);
  assert.strictEqual(own[0].map[1][1], 3);
});

check('two mods touching one object are contested; later wins', () => {
  const a = [{ type: 'object', mode: 'patch', id: 'base:area/0/obj/3', area: 'base:area/0', x: 1 }];
  const b = [{ type: 'object', mode: 'patch', id: 'base:area/0/obj/3', area: 'base:area/0', x: 2 }];
  const W = G.buildWorld(base, [{ id: 'a', files: [{ name: 'x.json', records: a }] }, { id: 'b', files: [{ name: 'x.json', records: b }] }]);
  const o = W.areas['base:area/0'].objects.find(o => o.id === 'base:area/0/obj/3');
  assert(o.x === 2 && o.contested);
  assert(W.log.every(e => e.contested && e.contested.length === 2));
});

check('text for a sign is a text record the editor owns', () => {
  const recs = [];
  let W = G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  const o = W.areas['base:area/0'].objects[0];
  G.edSetText(recs, W, 'm', o, 'sign', 'Hello');
  G.edSetText(recs, W, 'm', o, 'sign', 'Hello again'); // same text record is reused
  assert.strictEqual(recs.filter(r => r.type === 'text').length, 1);
  W = G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  const o2 = W.areas['base:area/0'].objects.find(x => x.id === o.id);
  assert.strictEqual(W.texts[o2.sign], 'Hello again');
});

const wr = recs => [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }];

check('new area, door with a way back, and an npc', () => {
  const recs = [];
  let W = G.buildWorld(base, wr(recs));
  const aid = G.edNewArea(recs, W, 'm', { slug: 'Shed!', name: 'The Shed', width: 10, height: 6, lookFrom: 'base:area/0', music: 3 });
  assert.strictEqual(aid, 'm:area/shed_');
  W = G.buildWorld(base, wr(recs));
  const a = W.areas[aid];
  assert(a && a.width === 10 && a.height === 6 && a.tileset === W.areas['base:area/0'].tileset && a.music === 3);
  assert.strictEqual(W.texts[a.name], 'The Shed');

  const door = W.areas['base:area/0'].objects.find(o => o.objtype && !o.reach);
  const r = G.edAddExit(recs, W, 'm', { from: 'base:area/0', object: door.id, to: aid, spawn: [100, 100], onTouch: true, wayBack: true });
  assert(r.back && recs.filter(x => x.type === 'exit').length === 2);
  // the door gets a reach, and the way back object comes before the exits that name it
  assert(recs.some(x => x.type === 'object' && x.mode === 'patch' && x.id === door.id && x.reach));
  assert(recs.findIndex(x => x.id === r.back) < recs.findIndex(x => x.type === 'exit'));
  G.edAddExit(recs, W, 'm', { from: 'base:area/0', object: door.id, to: aid, spawn: [120, 100] }); // same door again replaces its exit
  assert.strictEqual(recs.filter(x => x.type === 'exit' && x.from === 'base:area/0').length, 1);

  W = G.buildWorld(base, wr(recs));
  const id = G.edAddNpc(recs, W, 'm', { areaId: aid, x: 50, y: 60, name: 'Sam', lines: ['Hi'], png: 'assets/sam.png' });
  W = G.buildWorld(base, wr(recs));
  const n = W.npcs.find(x => x.id === id);
  assert(n && n.areaId === aid && n.lines.length === 3 && W.texts[n.name] === 'Sam' && W.anims[n.anim].frames[0].png === 'assets/sam.png');
  G.edMoveNpc(recs, n, 70, 80);
  assert(recs.find(x => x.id === id).x === 70);
  G.edDeleteNpc(recs, n); // npc, anim and its texts all go
  assert(!recs.some(x => x.id === id || x.type === 'anim' || (x.type === 'text' && x.text === 'Sam')));
});

check('base npcs: move is a replace, delete is a remove', () => {
  const recs = [];
  const W = G.buildWorld(base, wr(recs));
  const n = W.npcs[0];
  G.edMoveNpc(recs, n, 5, 6); G.edMoveNpc(recs, n, 7, 8);
  assert.deepStrictEqual(recs, [{ type: 'npc', mode: 'replace', id: n.id, x: 7, y: 8 }]);
  const W2 = G.buildWorld(base, wr(recs));
  const m = W2.npcs.find(x => x.id === n.id);
  assert(m.x === 7 && m.y === 8);
  G.edDeleteNpc(recs, m);
  assert.deepStrictEqual(recs, [{ type: 'npc', mode: 'remove', id: n.id }]);
  assert(!G.buildWorld(base, wr(recs)).npcs.some(x => x.id === n.id));
});

if (process.env.GE_DUMP) { // write a mod for the real loader to read: GE_DUMP=<folder> node tests/gigaeditr_plus_test.js
  const recs = [];
  let W = G.buildWorld(base, wr(recs));
  const aid = G.edNewArea(recs, W, 'edtest', { slug: 'shed', name: 'The Shed', width: 10, height: 6, lookFrom: 'base:area/0', music: 3 });
  W = G.buildWorld(base, wr(recs));
  const door = W.areas['base:area/0'].objects.find(o => o.objtype && !o.reach);
  G.edAddExit(recs, W, 'edtest', { from: 'base:area/0', object: door.id, to: aid, spawn: [160, 120], wayBack: true });
  G.edMoveObject(recs, W.areas['base:area/0'].objects[3], 300, 400);
  G.edPaint(recs, W, 'base:area/0', 5, 5, W.areas['base:area/0'].map[5][5]);
  G.edMoveNpc(recs, W.npcs[0], W.npcs[0].x + 10, W.npcs[0].y);
  G.edAddNpc(recs, W, 'edtest', { areaId: aid, x: 100, y: 100, name: 'Sam', lines: ['Hi'], png: 'assets/sam.png' });
  const dir = path.join(process.env.GE_DUMP, 'edtest');
  fs.mkdirSync(path.join(dir, 'data'), { recursive: true }); fs.mkdirSync(path.join(dir, 'assets'), { recursive: true });
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify({ id: 'edtest', version: '1.0', game_api: '1', requires: [], load_after: [] }));
  fs.writeFileSync(path.join(dir, 'data', G.EDFILE), JSON.stringify(recs, null, 2));
  fs.copyFileSync(path.join(__dirname, '..', 'mods', 'cavemod', 'assets', 'keeper.png'), path.join(dir, 'assets', 'sam.png'));
}

check('pixel helpers: fill, replace all, line, brush, rect, flip, resize, colours', () => {
  const w = 5, h = 4, a = new Uint32Array(w * h);
  const red = G.packRGBA(255, 0, 0, 255), blue = G.packRGBA(0, 0, 255, 255);
  for (let y = 0; y < h; y++) a[y * w + 2] = blue; // a vertical wall splits the picture in two
  assert.strictEqual(G.fillRegion(a, w, h, 0, 0, red, false), 8); // only the left side
  assert.strictEqual(a[3], 0); assert.strictEqual(a[0], red);
  assert.strictEqual(G.fillRegion(a, w, h, 3, 0, red, false), 8);
  assert.strictEqual(G.fillRegion(a, w, h, 2, 0, red, true), 4); // replace every blue pixel
  assert.strictEqual(G.fillRegion(a, w, h, 0, 0, red, false), 0); // same colour: nothing
  assert.strictEqual(G.fillRegion(a, w, h, 9, 9, red, false), 0);
  assert.deepStrictEqual(G.unpackRGBA(red), [255, 0, 0, 255]);
  const ln = G.lineCells(0, 0, 4, 2); assert.deepStrictEqual(ln[0], [0, 0]); assert.deepStrictEqual(ln[ln.length - 1], [4, 2]); assert.strictEqual(ln.length, 5);
  assert.strictEqual(G.brushCells(5, 5, 3, false).length, 9); assert(G.brushCells(5, 5, 5, true).length < 25);
  assert.strictEqual(G.rectCells(0, 0, 3, 2, true).length, 12); assert.strictEqual(G.rectCells(0, 0, 3, 2, false).length, 10);
  const f = Uint8Array.from([1, 2, 3, 4, 5, 6]); G.flipArr(f, 3, 2, true); assert.deepStrictEqual([...f], [3, 2, 1, 6, 5, 4]); G.flipArr(f, 3, 2, false); assert.deepStrictEqual([...f], [6, 5, 4, 3, 2, 1]);
  const r = G.resizeArr(Uint8Array.from([1, 2, 3, 4]), 2, 2, 3, 3); assert.deepStrictEqual([...r], [1, 2, 0, 3, 4, 0, 0, 0, 0]);
  const c = G.colorsOf(Uint32Array.from([red, red, blue, 0])); assert.deepStrictEqual(c, [[red, 2], [blue, 1]]);
});

check('collision layer matches how the game reads a collision sheet', () => {
  // a 64 px collision cell, tile 0 has only its top-left quarter blocked; two tiles side by side
  const rgba = new Uint8ClampedArray(128 * 64 * 4);
  for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) rgba[(y * 128 + x) * 4 + 3] = 255;
  const m = G.collisionLayer({ png: { rgba, w: 128, h: 64 }, tile: 64 }, 3, 1, 32); // 3 tiles wide on a 32 px sheet, the collision only covers 2
  const at = (t, x, y) => m[y * 96 + t * 32 + x];
  assert.strictEqual(at(0, 5, 5), 1); assert.strictEqual(at(0, 20, 5), 0); assert.strictEqual(at(0, 20, 20), 0);
  assert.strictEqual(at(1, 5, 5), 0);
  assert.strictEqual(at(2, 5, 5), 1); assert.strictEqual(at(2, 31, 31), 1); // past the collision's count: blocked
  const s = G.collisionLayer({ count: 2, blocked_tiles: [1] }, 2, 1, 32);
  assert.strictEqual(s[5], 0); assert.strictEqual(s[32 + 5], 1);
});

check('area look, full objtype records', () => {
  const recs = []; G.edSetAreaLook(recs, 'base:area/0', 'm:tileset/e1', 'm:collision/e1'); G.edSetAreaLook(recs, 'base:area/0', 'm:tileset/e2', null);
  assert.deepStrictEqual(recs, [{ type: 'area', mode: 'patch', id: 'base:area/0', tileset: 'm:tileset/e2', collision: 'm:collision/e1' }]);
  const W = G.buildWorld(base, wr(recs)); assert.strictEqual(W.areas['base:area/0'].tileset, 'm:tileset/e2');
  const id = G.edAddObjTypeFull(recs, W, 'm', 'tree', 'assets/tree.png', { anchorX: 3, anchorY: 9, dx: 0, dy: 1, ground: true });
  assert.deepStrictEqual(recs[1], { type: 'objtype', mode: 'add', id, png: 'assets/tree.png', anchorX: 3, anchorY: 9, dx: 0, dy: 1, ground: true });
});

check('shadows: patch a base type, edit an own type in place', () => {
  const recs = [];
  let W = G.buildWorld(base, wr(recs));
  const t = Object.keys(W.objtypes)[0];
  G.edSetShadow(recs, t, { mode: 'none' });
  assert.deepStrictEqual(recs, [{ type: 'objtype', mode: 'patch', id: t, shadow: 'none' }]);
  G.edSetShadow(recs, t, { png: 'assets/s.png', anchorY: 3 });
  assert.deepStrictEqual(recs, [{ type: 'objtype', mode: 'patch', id: t, shadow_png: 'assets/s.png', shadow_anchorY: 3 }]);
  W = G.buildWorld(base, wr(recs));
  assert(W.objtypes[t].shadow === 'picture' && W.objtypes[t].shadowPng === 'assets/s.png' && W.objtypes[t].shadowSrc === 'm' && W.objtypes[t].shadowAnchorY === 3);
  G.edSetShadow(recs, t, { mode: 'default' }); // back to the game's own: the patch says so explicitly
  assert.strictEqual(recs[0].shadow, 'default');
  const own = []; const id = G.edAddObjTypeFull(own, W, 'm', 'tree', 'assets/tree.png', {});
  G.edSetShadow(own, id, { mode: 'auto' }); assert.strictEqual(own.length, 1); assert.strictEqual(own[0].shadow, 'auto');
  G.edSetShadow(own, id, { mode: 'default' }); assert(!('shadow' in own[0]) && own.length === 1);
  G.edSetShadow(own, id, { png: 'assets/s.png' }); assert.strictEqual(own[0].shadow_png, 'assets/s.png');
});

check('area fields: resume and music', () => {
  const recs = [];
  G.edSetAreaField(recs, 'base:area/0', 'resume', true);
  assert.deepStrictEqual(recs, [{ type: 'area', mode: 'patch', id: 'base:area/0', resume: true }]);
  let W = G.buildWorld(base, wr(recs));
  assert.strictEqual(W.areas['base:area/0'].resume, true);
  assert(!W.areas['base:area/1'].resume);
  G.edSetAreaField(recs, 'base:area/0', 'music', 3);
  G.edSetAreaField(recs, 'base:area/0', 'resume', undefined);
  assert.deepStrictEqual(recs, [{ type: 'area', mode: 'patch', id: 'base:area/0', music: 3 }]);
  G.edSetAreaField(recs, 'base:area/0', 'music', undefined); // nothing left: the patch goes away
  assert.deepStrictEqual(recs, []);
  const own = [{ type: 'area', mode: 'add', id: 'm:area/x', tileset: 't', collision: 'c', map: [[0]] }];
  G.edSetAreaField(own, 'm:area/x', 'resume', true); // an area the editor made keeps it in its own record
  assert.strictEqual(own.length, 1); assert.strictEqual(own[0].resume, true);
});

check('mod order honours load_after', () => {
  const ms = [{ id: 'b', manifest: { load_after: ['z'] } }, { id: 'z', manifest: {} }, { id: 'a', manifest: {} }];
  assert.deepStrictEqual(G.orderMods(ms).map(m => m.id), ['a', 'z', 'b']);
});


check('colour words round-trip and records', () => {
  for (let v = 0; v < 32; v++) { const w = (v << 10) | (v << 5) | v; assert.strictEqual(G.rgbToWord(G.wordToRgbRound(w)), w, 'round trip ' + v); }
  assert.deepStrictEqual(G.wordToRgb(31 << 10), [255, 0, 0]);
  assert.strictEqual(G.wordToRgb(0x8000), null); assert.strictEqual(G.rgbToWord(null), 0x8000);
  const baseWords = [0x8000, 31 << 10, 31 << 5, 31];
  const words = [0x8000, 31 << 10, 0x8000, 12];
  assert.deepStrictEqual(G.paletteSet(baseWords, words), { 2: null, 3: G.wordToRgbRound(12) });
  const recs = [{ type: 'npc', mode: 'replace', id: 'base:npc/1', palette: 'm:pal/a' }];
  const W = G.buildWorld(base, []);
  const id = G.edSavePalette(recs, W, 'm', { slug: 'a', basedOn: 481, baseWords, words, species: 1, offer: true });
  assert.strictEqual(id, 'm:pal/a');
  assert.deepStrictEqual(recs[1], { type: 'palette', mode: 'add', id: 'm:pal/a', based_on: 481, set: { 2: null, 3: G.wordToRgbRound(12) }, species: 'monkey' });
  assert.deepStrictEqual(recs[2], { type: 'minigame', mode: 'patch', id: 'base:minigame/colour_machine', list_add: { presets: ['m:pal/a'] } });
  const id2 = G.edSavePalette(recs, { palettes: { 'm:pal/a': {} } }, 'm', { slug: 'a', basedOn: 481, baseWords, words: baseWords, species: -1, offer: true }); // the name is taken
  assert.strictEqual(id2, 'm:pal/a2'); assert.deepStrictEqual(recs[2].list_add.presets, ['m:pal/a', 'm:pal/a2']);
  G.edSavePalette(recs, W, 'm', { id: 'm:pal/a', slug: 'a', basedOn: 481, baseWords, words: baseWords, species: -1, offer: false }); // edit in place, out of the machine
  assert.strictEqual(recs.filter(r => r.type === 'palette' && r.id === 'm:pal/a').length, 1);
  assert.deepStrictEqual(recs.find(r => r.id === 'm:pal/a'), { type: 'palette', mode: 'add', id: 'm:pal/a', based_on: 481 });
  assert.deepStrictEqual(G.offeredIds(recs), ['m:pal/a2']);
  assert.deepStrictEqual(G.paletteWords({ based_on: 481, set: { 2: null } }, { 481: baseWords }), [0x8000, 31 << 10, 0x8000, 31]);
  assert.strictEqual(G.paletteWords({ colors: [[1, 2, 3]] }, { 481: baseWords }), null);
  G.edDeletePalette(recs, 'm:pal/a');
  assert(!recs.some(r => r.id === 'm:pal/a' && r.type === 'palette') && recs[0].palette === undefined);
  G.edDeletePalette(recs, 'm:pal/a2'); // the machine record goes with its last entry
  assert(!recs.some(r => r.type === 'minigame' || r.type === 'palette'));
});

check('palette records show in the world', () => {
  const W = G.buildWorld(base, [{ id: 'm', files: [{ name: 'a.json', records: [{ type: 'palette', mode: 'add', id: 'm:pal/x', based_on: 481 }] }] }]);
  assert(W.palettes['m:pal/x'] && W.palettes['m:pal/x'].src === 'm');
});

const world = recs => G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
check('the export carries the tables and items', () => {
  const W = world([]);
  assert(W.items.length > 200 && W.items[0].src === 'base' && W.items[0].name);
  assert.strictEqual(W.tables.find.length, 15);
  assert.strictEqual(W.tables.gate.length, 5);
  assert.strictEqual(W.tables.quest_spawn.length, 16);
  const s = W.tables.spot[0]; assert(s.area.startsWith('base:area/') && s.state && s.item.startsWith('base:item/'), 'a spot names its area by id');
  const q = W.tables.quest_spawn.find(r => r.id === 'base:quest_spawn/15');
  assert(q.quest === 15 && q.area === 'base:area/3' && q.x === 2226 && q.state === 'buried');
  const rnd = W.tables.quest_spawn.filter(r => r.random); assert(rnd.length === 12 && rnd.every(r => r.times >= 1 && r.x === undefined));
  assert(W.tables.quest_spawn.find(r => r.id === 'base:quest_spawn/26/246').state === 'buried', 'a random buried reward');
  const g = W.tables.gate.find(r => r.id === 'base:gate/5/1'); assert(g.kind === 'stat' && g.stats.join() === 'stars,lightbulbs' && g.need === 500);
  assert(W.npcs.filter(n => n.quest_min === 17).length === 11 && W.areas['base:area/5'].diggable === true && Object.values(W.areas).some(a => a.src === 'base' && a.diggable === false));
});

check('rows: add, patch and remove, in the editor and in the world', () => {
  const recs = [];
  let W = world(recs);
  const item = W.items[3].id;
  const fid = G.edRowAdd(recs, W, 'm', 'find', { item, weight: 40 });
  assert.strictEqual(fid, 'm:find/e1');
  const base265 = W.tables.find.find(r => r.id === 'base:find/265');
  G.edRowPatch(recs, 'find', base265, { weight: 7 });
  assert.deepStrictEqual(recs[1], { type: 'find', mode: 'patch', id: 'base:find/265', weight: 7 });
  G.edRowPatch(recs, 'find', base265, { weight: 8 });
  assert.strictEqual(recs.length, 2); assert.strictEqual(recs[1].weight, 8); // the same patch
  const own = W.tables.find[0]; G.edRowRemove(recs, 'find', own);
  assert.deepStrictEqual(recs[2], { type: 'find', mode: 'remove', id: own.id });
  W = world(recs);
  assert.strictEqual(W.tables.find.find(r => r.id === fid).weight, 40);
  assert.strictEqual(W.tables.find.find(r => r.id === 'base:find/265').weight, 8);
  assert(!W.tables.find.some(r => r.id === own.id) && W.removedRows.find.some(r => r.id === own.id && r.removedBy === 'm'));
  G.edRowRestore(recs, 'find', own.id); assert(!recs.some(r => r.mode === 'remove'));
  G.edRowPatch(recs, 'find', base265, { weight: undefined }); assert(!recs.some(r => r.mode === 'patch'), 'an empty patch goes away');
  G.edRowRemove(recs, 'find', W.tables.find.find(r => r.id === fid)); assert(!recs.some(r => r.id === fid), 'removing your own row deletes its record');
});

check('spots cannot be patched; quest rewards and gates can', () => {
  const recs = [];
  const W = world(recs);
  const sp = W.tables.spot[0];
  assert.throws(() => G.edRowPatch(recs, 'spot', sp, { x: 1 }), /cannot be changed/);
  const id = G.edRowAdd(recs, W, 'm', 'spot', { area: 'base:area/0', item: W.items[0].id, x: 5, y: 6, state: 'ground' });
  G.edRowPatch(recs, 'spot', { id }, { x: 9 }); // your own spot is edited in its add record
  assert.deepStrictEqual(recs[0], { type: 'spot', mode: 'add', id: 'm:spot/e1', area: 'base:area/0', item: W.items[0].id, x: 9, y: 6, state: 'ground' });
  const W2 = world(recs); assert(W2.tables.spot.some(r => r.id === id && r.x === 9));
  const q = W2.tables.quest_spawn.find(r => r.id === 'base:quest_spawn/32');
  G.edRowPatch(recs, 'quest_spawn', q, { quest: 33, x: 10 });
  const g = W2.tables.gate.find(r => r.id === 'base:gate/5/22');
  G.edRowPatch(recs, 'gate', g, { need: 3000 });
  const W3 = world(recs);
  assert.strictEqual(W3.tables.quest_spawn.find(r => r.id === q.id).quest, 33);
  assert.strictEqual(W3.tables.quest_spawn.find(r => r.id === q.id).y, q.y);
  assert.strictEqual(W3.tables.gate.find(r => r.id === g.id).need, 3000);
  assert(W3.tables.gate.find(r => r.id === g.id).mods.some(m => m.kind === 'patch' && m.mod === 'm'));
});

check('items a mod adds can be picked', () => {
  const W = G.buildWorld(base, [{ id: 'm', files: [{ name: 'a.json', records: [{ type: 'item', mode: 'add', id: 'm:item/pie', name: 'm:str/pie' }] }] }]);
  assert(W.items.some(i => i.id === 'm:item/pie' && i.src === 'm'));
});

check('npc quest window and colour, area diggable, object scene', () => {
  const recs = [];
  let W = world(recs);
  const n = W.npcs.find(x => x.quest_min === 17);
  G.edSetNpcFields(recs, n, { quest_min: 20, palette: 'm:pal/x' });
  assert.deepStrictEqual(recs[0], { type: 'npc', mode: 'replace', id: n.id, quest_min: 20, palette: 'm:pal/x' });
  W = world(recs); assert.strictEqual(W.npcs.find(x => x.id === n.id).quest_min, 20); assert.strictEqual(W.npcs.find(x => x.id === n.id).palette, 'm:pal/x');
  G.edSetNpcFields(recs, n, { quest_min: undefined, palette: undefined }); assert.strictEqual(recs.length, 0, 'an empty replace goes away');
  G.edSetAreaField(recs, 'base:area/3', 'diggable', true);
  W = world(recs); assert.strictEqual(W.areas['base:area/3'].diggable, true);
  const o = W.areas['base:area/0'].objects[2];
  G.edSetObjectScene(recs, o, 'm:scene/hello');
  assert.deepStrictEqual(recs[1], { type: 'object', mode: 'patch', id: o.id, area: 'base:area/0', scene: 'm:scene/hello' });
  W = world(recs); assert.strictEqual(W.areas['base:area/0'].objects[2].scene, 'm:scene/hello');
  G.edSetObjectScene(recs, o, ''); assert(!recs.some(r => r.type === 'object'));
});
check('shops and stock rules: the base export, add / patch / remove, and the load order', () => {
  const world = recs => G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  let W = world([]);
  assert.strictEqual(W.tables.shop.length, 22); assert.strictEqual(W.tables.shop_stock.length, 18);
  const s10 = W.tables.shop.find(x => x.id === 'base:shop/10');
  assert(s10 && s10.area === 'base:area/10' && s10.rolls === 10 && s10.items.length === 10 && s10.items[0].weight === 60 && s10.items[0].cap === 15);
  assert(W.tables.shop_stock.some(r => r.id === 'base:stock/193' && r.shop === 'base:shop/12' && r.unless_held === true));
  assert(G.TABLE_TYPES.includes('shop') && G.TABLE_TYPES.includes('shop_stock'));
  // a new shop in a free area, then a rule that stocks it
  const recs = [];
  const sid = G.edRowAdd(recs, W, 'm', 'shop', { area: 'base:area/0', rolls: 4, items: [{ item: 'base:item/30', weight: 7, cap: 2 }] });
  assert.strictEqual(sid, 'm:shop/e1');
  const rid = G.edRowAdd(recs, W, 'm', 'shop_stock', { shop: sid, on: 'illness', from: 'cure' });
  assert.deepStrictEqual(recs[1], { type: 'shop_stock', mode: 'add', id: 'm:shop_stock/e1', shop: 'm:shop/e1', on: 'illness', from: 'cure' });
  W = world(recs.slice().reverse()); // the rule comes first in the file; it still loads after the shop
  const mine = W.tables.shop.find(x => x.id === sid);
  assert(mine && mine.added && mine.area === 'base:area/0' && mine.rolls === 4 && mine.items[0].item === 'base:item/30');
  assert(W.tables.shop_stock.some(r => r.id === rid && r.added));
  // patching a base shop with a whole list, then with add_items / remove_items written by hand
  const p = [];
  G.edRowPatch(p, 'shop', s10, { rolls: 12, items: [{ item: 'base:item/41', weight: 1, cap: 1 }] });
  assert.deepStrictEqual(p, [{ type: 'shop', mode: 'patch', id: 'base:shop/10', rolls: 12, items: [{ item: 'base:item/41', weight: 1, cap: 1 }] }]);
  W = world(p); let s = W.tables.shop.find(x => x.id === 'base:shop/10');
  assert.strictEqual(s.rolls, 12); assert.strictEqual(s.items.length, 1); assert(s.mods.some(x => x.kind === 'patch'));
  W = world([{ type: 'shop', mode: 'patch', id: 'base:shop/10', remove_items: ['base:item/42'], add_items: [{ item: 'base:item/99', weight: 3 }, { item: 'base:item/41', weight: 2, cap: 9 }] }]);
  s = W.tables.shop.find(x => x.id === 'base:shop/10');
  assert.strictEqual(s.items.length, 10); assert(!s.items.some(i => i.item === 'base:item/42'));
  assert.deepStrictEqual(s.items.find(i => i.item === 'base:item/99'), { item: 'base:item/99', weight: 3, cap: 5 }); // the loader's defaults
  assert.deepStrictEqual(s.items[0], { item: 'base:item/41', weight: 2, cap: 9 }); // already listed: changed in place
  // remove, restore
  const r = []; G.edRowRemove(r, 'shop', s10);
  W = world(r); assert.strictEqual(W.tables.shop.length, 21); assert.strictEqual(W.removedRows.shop.length, 1);
  G.edRowRestore(r, 'shop', 'base:shop/10'); assert.strictEqual(r.length, 0);
  // a base rule patched: only what changed
  const rule = world([]).tables.shop_stock.find(x => x.id === 'base:stock/quest20');
  const q = []; G.edRowPatch(q, 'shop_stock', rule, { quest: 21 });
  assert.deepStrictEqual(q, [{ type: 'shop_stock', mode: 'patch', id: 'base:stock/quest20', quest: 21 }]);
  W = world(q); assert.strictEqual(W.tables.shop_stock.find(x => x.id === 'base:stock/quest20').quest, 21);
  // a mod's own rows go with their mod
  const gone = world([...recs, { type: 'shop', mode: 'remove', id: sid }]); assert(!gone.tables.shop.some(x => x.id === sid));
});
check('sounds, songs, instruments and drums: the ROM entries, add / replace like the loader, and the editor ops', () => {
  const world = recs => G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  let W = world([]);
  assert.strictEqual(W.audioInfo.sounds, 96);
  assert.strictEqual(Object.keys(W.audio.instrument).length, 14); assert.strictEqual(Object.keys(W.audio.drum).length, 12); assert.strictEqual(Object.keys(W.audio.music).length, 9);
  assert(W.audio.instrument['base:instrument/13'].name === 'Trumpet' && W.audio.drum['base:drum/1'].name === 'Bass Drum' && W.audio.music['base:music/4'].menu === true);
  const recs = [];
  const sid = G.edAudioAdd(recs, W, 'm', 'sound', 'Meow Meow!', { file: 'audio/meow.ogg', volume: 150, pitch: -3 });
  assert.strictEqual(sid, 'm:sound/meow_meow');
  assert.strictEqual(G.edAudioAdd(recs, W, 'm', 'sound', 'meow meow', { file: 'audio/b.ogg' }), 'm:sound/meow_meow_2'); // taken: numbered
  const iid = G.edAudioAdd(recs, world(recs), 'm', 'instrument', 'harp', { file: 'audio/harp.wav', root: 62, loop_start: 0.4, loop_end: 1.1, attack: 0.01, ignored: 1 });
  assert.deepStrictEqual(recs.find(r => r.id === iid), { type: 'instrument', mode: 'add', id: 'm:instrument/harp', file: 'audio/harp.wav', root: 62, loop_start: 0.4, loop_end: 1.1, attack: 0.01 });
  W = world(recs);
  assert.deepStrictEqual(W.audio.sound[sid].rec, { file: 'audio/meow.ogg', volume: 150, pitch: -3 }); assert(W.audio.sound[sid].added && W.audio.sound[sid].src === 'm');
  // a ROM sound / song / instrument / drum replaced: a replace record; the ROM's entries gain a replacement
  G.edAudioSet(recs, 'sound', G.romAudioId('sound', 84), { file: 'audio/pop.wav', volume: 50 });
  G.edAudioSet(recs, 'drum', 'base:drum/5', { file: 'audio/clap.wav' });
  G.edAudioSet(recs, 'music', 'base:music/4', { channels: { '1': 'm:instrument/harp' } }); // no file: only the instruments change
  W = world(recs);
  assert.deepStrictEqual(W.audio.sound['base:sound/84'].rec, { file: 'audio/pop.wav', volume: 50 }); assert(W.audio.sound['base:sound/84'].base && W.audio.sound['base:sound/84'].replaced);
  assert.strictEqual(W.audio.drum['base:drum/5'].rec.file, 'audio/clap.wav'); assert.strictEqual(W.audio.drum['base:drum/5'].name, 'Side Stick'); // still the ROM's drum 5
  assert(!W.audio.music['base:music/4'].rec.file && W.audio.music['base:music/4'].rec.channels['1'] === 'm:instrument/harp' && W.audio.music['base:music/4'].menu);
  // another patch of the same song merges (no file); a file replaces the song as a whole
  const more = [...recs, { type: 'music', mode: 'replace', id: 'base:music/4', programs: { '2': 'base:instrument/5' } }];
  W = world(more); assert(W.audio.music['base:music/4'].rec.channels && W.audio.music['base:music/4'].rec.programs);
  W = world([...more, { type: 'music', mode: 'replace', id: 'base:music/4', file: 'audio/s.mid' }]);
  assert.deepStrictEqual(W.audio.music['base:music/4'].rec, { file: 'audio/s.mid' });
  // editing an own record changes it in place; clearing a field takes it out; replace of a missing own id is skipped
  G.edAudioSet(recs, 'sound', sid, { volume: undefined, pitch: 2 });
  assert.deepStrictEqual(recs.find(r => r.id === sid), { type: 'sound', mode: 'add', id: sid, file: 'audio/meow.ogg', pitch: 2 });
  assert.strictEqual(world([{ type: 'sound', mode: 'replace', id: 'm:sound/nope', file: 'x.ogg' }]).audio.sound['m:sound/nope'], undefined);
  assert.strictEqual(world([...recs, { type: 'sound', mode: 'add', id: sid, file: 'x.ogg' }]).audio.sound[sid].rec.file, 'audio/meow.ogg'); // a duplicate add is skipped
  // delete takes every record of the id
  G.edAudioDelete(recs, 'sound', 'base:sound/84'); assert(!recs.some(r => r.id === 'base:sound/84'));
  G.edAudioDelete(recs, 'instrument', iid); assert(!recs.some(r => r.id === iid));
  W = world(recs); assert(!W.audio.instrument[iid] && !W.audio.sound['base:sound/84']);
});
check('named ids: "base:sound/kaboing" is the game\'s sound 91 and "base:music/ufo" song 8', () => {
  const world = recs => G.buildWorld(base, [{ id: 'm', files: [{ name: G.EDFILE, records: recs }] }]);
  let W = world([]);
  assert.strictEqual(W.audioInfo.sound_names['91'], 'kaboing'); assert.strictEqual(W.audio.music['base:music/8'].name, 'ufo');
  assert.strictEqual(G.canonAudioId('sound', 'base:sound/kaboing'), 'base:sound/91'); assert.strictEqual(G.canonAudioId('sound', 'base:sound/91'), 'base:sound/91');
  assert.strictEqual(G.canonAudioId('sound', 'base:sound/nope'), 'base:sound/nope'); assert.strictEqual(G.canonAudioId('music', 'base:music/ufo'), 'base:music/8'); assert.strictEqual(G.canonAudioId('music', 'base:sound/ufo'), 'base:sound/ufo');
  // a hand-written named record shows up on the same row as the number
  W = world([{ type: 'sound', mode: 'replace', id: 'base:sound/kaboing', file: 'audio/k.wav' }, { type: 'music', mode: 'replace', id: 'base:music/ufo', channels: { '1': 3 } }]);
  assert.strictEqual(W.audio.sound['base:sound/91'].rec.file, 'audio/k.wav'); assert(W.audio.music['base:music/8'].replaced && W.audio.music['base:music/8'].rec.channels['1'] === 3);
  // editing it changes that record, it does not write a second one; delete finds it too
  const recs = [{ type: 'sound', mode: 'replace', id: 'base:sound/kaboing', file: 'audio/k.wav' }];
  G.edAudioSet(recs, 'sound', 'base:sound/91', { volume: 50 }); assert.strictEqual(recs.length, 1); assert.strictEqual(recs[0].volume, 50);
  G.edAudioDelete(recs, 'sound', 'base:sound/91'); assert.strictEqual(recs.length, 0);
});
if (fails) { console.error(fails + ' failed'); process.exit(1); }
console.log('gigaeditr_plus_test: all passed');
