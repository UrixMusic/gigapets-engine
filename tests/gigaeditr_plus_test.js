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

check('mod order honours load_after', () => {
  const ms = [{ id: 'b', manifest: { load_after: ['z'] } }, { id: 'z', manifest: {} }, { id: 'a', manifest: {} }];
  assert.deepStrictEqual(G.orderMods(ms).map(m => m.id), ['a', 'z', 'b']);
});

if (fails) { console.error(fails + ' failed'); process.exit(1); }
console.log('gigaeditr_plus_test: all passed');
