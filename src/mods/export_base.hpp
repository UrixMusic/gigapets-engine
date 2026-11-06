// export_base.hpp - writes the content database as plain JSON and PNG files, so a tool (the planned GigaEditr+) can show the base
// game without having to read the ROM itself. Run it with `gigapets rom.u7 --export-base <folder>`. The folder gets:
//   base.json            areas (size, map, objects, spawn points), object types, npcs, texts, and where every picture is
//   tilesets/*.png       one sheet per tileset, tiles in a grid 16 wide, numbered left to right then down (the mod `tileset` layout)
//   collision/*.png      the same layout; opaque = blocked (the mod `collision` layout)
//   objtypes/*.png       the art of each object type (the feet are at `anchorX, anchorY` in the picture)
//   pets.json            (exportPets) the pets' animations as colour INDICES plus the ROM's pet palettes, for the editor's colour maker
// What is written comes from the ROM, so it is for the user's own use: it is not part of the game's download.
#pragma once
#include "../content/db.hpp"
#include "../game/pet/color_machine.hpp"
#include "../engine/audio_names.hpp"
#include "../engine/gm_map.hpp"
#include "../engine/rom_audio.hpp"
#include "../game/npc/npc_anim_ids.hpp"
#include "../loaders/png.hpp"
#include "../loaders/sprite.hpp"
#include "manifest.hpp"

namespace gp
{
    namespace exportimpl
    {
        // An id as a file name: letters and digits stay, everything else becomes '_'.
        inline std::string fileStem(const std::string &id)
        {
            std::string s = id;
            for (char &c : s)
                if (!std::isalnum((unsigned char)c))
                    c = '_';
            return s;
        }
        constexpr int kSheetCols = 16;

        // tiles stacked in `src` (count tiles of tw x th, one after the other) as a grid sheet; `alpha` turns a 0/1 mask into opaque / clear
        inline void sheet(const std::vector<uint8_t> &src, int tw, int th, int count, bool mask, int &outW, int &outH, std::vector<uint8_t> &out)
        {
            const int cols = std::min(std::max(count, 1), kSheetCols), rows = (std::max(count, 1) + cols - 1) / cols;
            outW = cols * tw;
            outH = rows * th;
            out.assign(size_t(outW) * outH * 4, 0);
            for (int t = 0; t < count; ++t)
                for (int y = 0; y < th; ++y)
                    for (int x = 0; x < tw; ++x)
                    {
                        uint8_t *d = &out[(size_t((t / cols) * th + y) * outW + size_t(t % cols) * tw + x) * 4];
                        if (mask)
                        {
                            if (src[(size_t(t) * th + y) * tw + x])
                            {
                                d[0] = 255;
                                d[1] = 0;
                                d[2] = 0;
                                d[3] = 255;
                            }
                        }
                        else
                            for (int k = 0; k < 4; ++k)
                                d[k] = src[((size_t(t) * th + y) * tw + x) * 4 + k];
                    }
        }
    } // namespace exportimpl

    // Returns "" on success, otherwise what went wrong.
    inline std::string exportBase(const ContentDB &db, const fs::path &outDir)
    {
        using namespace exportimpl;
        std::error_code ec;
        for (const char *d : {"", "tilesets", "collision", "objtypes"})
            if (fs::create_directories(outDir / d, ec); ec && !fs::is_directory(outDir / d))
                return "cannot create " + (outDir / d).string();
        json out;
        out["format"] = 1;

        json tilesets = json::object();
        for (const auto &[id, ts] : db.allTilesets())
        {
            if (ts.count <= 0 || id.rfind("base:", 0) != 0)
                continue;
            int w = 0, h = 0;
            std::vector<uint8_t> px;
            sheet(ts.rgba, ts.tileW, ts.tileH, ts.count, false, w, h, px);
            const std::string rel = "tilesets/" + fileStem(id) + ".png";
            if (!writePng((outDir / rel).string(), w, h, px))
                return "cannot write " + rel;
            tilesets[id] = {{"png", rel}, {"tile", ts.tileW}, {"count", ts.count}};
        }
        out["tilesets"] = tilesets;

        json collisions = json::object();
        for (const auto &[id, cs] : db.allCollisions())
        {
            if (cs.count <= 0 || id.rfind("base:", 0) != 0)
                continue;
            int w = 0, h = 0;
            std::vector<uint8_t> px;
            sheet(cs.blocked, cs.tileW, cs.tileH, cs.count, true, w, h, px);
            const std::string rel = "collision/" + fileStem(id) + ".png";
            if (!writePng((outDir / rel).string(), w, h, px))
                return "cannot write " + rel;
            collisions[id] = {{"png", rel}, {"tile", cs.tileW}, {"count", cs.count}};
        }
        out["collisions"] = collisions;

        json types = json::object();
        for (const auto &[id, ot] : db.allObjectTypes())
        {
            if (id.rfind("base:", 0) != 0)
                continue;
            json t = {{"ground", !ot.hasMarker}};
            if (ot.hasArt && ot.art.w > 0 && ot.art.h > 0)
            {
                const std::string rel = "objtypes/" + fileStem(id) + ".png";
                if (!writePng((outDir / rel).string(), ot.art.w, ot.art.h, ot.art.rgba))
                    return "cannot write " + rel;
                t["png"] = rel;
                t["anchorX"] = ot.art.anchorX;
                t["anchorY"] = ot.art.anchorY;
                t["dx"] = ot.art.dx;
                t["dy"] = ot.art.dy;
            }
            if (!ot.mask.blocked.empty())
                t["mask"] = {{"w", ot.mask.w}, {"h", ot.mask.h}, {"ox", ot.mask.ox}, {"oy", ot.mask.oy}};
            types[id] = t;
        }
        out["objtypes"] = types;

        json areas = json::object();
        for (const auto &[id, a] : db.allAreas())
        {
            if (id.rfind("base:", 0) != 0)
                continue;
            json j = {{"diggable", a.diggable}, {"legacyId", a.legacyId}, {"name", a.name}, {"width", a.tilesW}, {"height", a.tilesH}, {"tileset", a.tileset},
                      {"collision", a.collision}, {"margins", {a.marginL, a.marginR, a.marginT, a.marginB}}, {"y_offset", a.collisionYOffset},
                      {"max_events", a.maxEvents}};
            json map = json::array();
            for (int r = 0; r < a.tilesH; ++r)
            {
                json row = json::array();
                for (int c = 0; c < a.tilesW; ++c)
                    row.push_back(a.tiles[size_t(r) * a.tilesW + c]);
                map.push_back(row);
            }
            j["map"] = map;
            json sp = json::array();
            for (const IPoint &p : a.spawnPoints)
                sp.push_back({p.x, p.y});
            j["spawn_points"] = sp;
            json objs = json::array();
            for (const ObjectDef &o : a.objects)
            {
                json oj = {{"id", o.id}, {"objtype", o.type}, {"x", o.x}, {"y", o.y}, {"slot", o.legacySlot},
                           {"box", {o.boxLeft, o.boxTop, o.boxRight, o.boxBottom}}};
                if (o.interactive() && o.reachX < 0xffff && o.reachY < 0xffff) // (0xffff = no reach in the ROM)
                    oj["reach"] = {o.reachX, o.reachY};
                if (!o.contextText.empty())
                    oj["sign"] = o.contextText;
                if (o.questMin != 0)
                    oj["quest_min"] = o.questMin;
                if (o.questMax != 0x7fffffff)
                    oj["quest_max"] = o.questMax;
                objs.push_back(oj);
            }
            j["objects"] = objs;
            areas[id] = j;
        }
        out["areas"] = areas;

        json npcs = json::array();
        for (const NpcDef *n : db.allNpcs())
            if (n->id.rfind("base:", 0) == 0)
            {
                json nj = {{"id", n->id}, {"name", n->nameText}, {"area", n->area}, {"x", n->x}, {"y", n->y},
                           {"stationary", n->stationary}, {"anim", n->anim}};
                if (n->questMin != 0)
                    nj["quest_min"] = n->questMin;
                if (n->questMax != 0x7fffffff)
                    nj["quest_max"] = n->questMax;
                npcs.push_back(std::move(nj));
            }
        out["npcs"] = npcs;

        json gates = json::array(); // stat / money guards' barriers (the `gate` record type)
        for (const auto &[gid, area] : db.allAreas())
            for (const GateDef *g : db.gatesIn(area.legacyId))
            {
                static const char *kinds[] = {"tricks", "stat", "money", "quests"};
                static const char *statNames[] = {"halos", "pitchforks", "stars", "lightbulbs"};
                json gj = {{"id", g->id}, {"barrier", g->barrier}, {"kind", kinds[int(g->kind)]}, {"need", g->need}};
                if (g->kind == GateKind::Personality)
                {
                    json st = json::array();
                    for (int v : g->stats)
                        st.push_back(statNames[v & 3]);
                    gj["stats"] = st;
                }
                gates.push_back(std::move(gj));
            }
        out["gates"] = gates;

        // the tables the editor lists: items (for the pickers), random finds, dig spots, quest rewards
        json items = json::array();
        for (const ItemDef &it : db.items())
            if (it.id.rfind("base:", 0) == 0)
                items.push_back({{"id", it.id}, {"name", it.name}, {"index", it.index}});
        out["items"] = items;
        json finds = json::array();
        for (const FindWeightDef *f : db.findWeights())
            if (f->id.rfind("base:", 0) == 0)
                finds.push_back({{"id", f->id}, {"item", f->item}, {"weight", f->weight}});
        out["finds"] = finds;
        json spots = json::array();
        for (const FixedSpotDef *s : db.allSpots())
            if (s->id.rfind("base:", 0) == 0)
            {
                json sj = {{"id", s->id}, {"area", s->area}, {"item", s->item}, {"x", s->x}, {"y", s->y}, {"state", s->state == 1 ? "buried" : "ground"}};
                if (!s->prereqItem.empty())
                    sj["requires_item"] = s->prereqItem;
                if (s->notWhileHeld)
                    sj["not_while_held"] = true;
                if (s->notOffered)
                    sj["not_offered"] = true;
                spots.push_back(std::move(sj));
            }
        out["spots"] = spots;
        json qsp = json::array();
        for (const QuestSpawnDef *q : db.allQuestSpawns())
            if (q->id.rfind("base:", 0) == 0)
            {
                json qj = {{"id", q->id}, {"quest", q->count}, {"area", q->area}, {"item", q->item}, {"state", q->state == 2 ? "ground" : "buried"}};
                if (q->random)
                {
                    qj["random"] = true;
                    qj["times"] = q->times;
                }
                else
                {
                    qj["x"] = q->x;
                    qj["y"] = q->y;
                }
                qsp.push_back(std::move(qj));
            }
        out["quest_spawns"] = qsp;

        // the shops (what each can stock) and the rules that put more into them
        json shops = json::array();
        for (const ShopDef &sd : db.shops())
            if (sd.id.rfind("base:", 0) == 0)
            {
                json its = json::array();
                for (const ShopItem &i : sd.items)
                    its.push_back({{"item", i.item}, {"weight", i.weight}, {"cap", i.cap}});
                shops.push_back({{"id", sd.id}, {"area", sd.area}, {"rolls", sd.rolls}, {"items", its}});
            }
        out["shops"] = shops;
        json rules = json::array();
        for (const StockRuleDef &r : db.stockRules())
            if (r.id.rfind("base:", 0) == 0)
            {
                json rj = {{"id", r.id}, {"shop", r.shop}, {"on", r.on}, {"from", r.from}};
                if (!r.area.empty())
                    rj["area"] = r.area;
                if (r.quest)
                    rj["quest"] = r.quest;
                if (!r.modes.empty())
                    rj["modes"] = r.modes;
                if (!r.item.empty())
                    rj["item"] = r.item;
                if (!r.onlyItem.empty())
                    rj["only_item"] = r.onlyItem;
                if (!r.exceptItem.empty())
                    rj["except_item"] = r.exceptItem;
                if (r.unlessHeld)
                    rj["unless_held"] = true;
                if (r.unlessOffered)
                    rj["unless_offered"] = true;
                rules.push_back(std::move(rj));
            }
        out["stock_rules"] = rules;

        // the ROM's sounds, for the audio panel: 96 sound effects (by number), the songs and where they play, and the instruments and drums songs are made of
        {
            json audio;
            audio["sounds"] = kRomSounds;
            json sn = json::object();
            {
                int c = 0;
                for (const AudioName *e = soundNames(c); c-- > 0; ++e)
                    sn[std::to_string(e->number)] = e->name;
            }
            audio["sound_names"] = sn; // number -> name, for the ones that have one
            json songs = json::array();
            for (int n = 0; n < 9; ++n)
            {
                json zones = json::array();
                for (int z = 0; z < 10; ++z)
                    if (rau::zoneSong(z) == n)
                        zones.push_back(z);
                songs.push_back({{"id", "base:music/" + std::to_string(n)}, {"name", songNameOf(n)}, {"zones", zones}, {"menu", n == rau::kMenuSong}});
            }
            audio["songs"] = songs;
            json ins = json::array();
            for (int n = 0; n < gm::kRomInstruments; ++n)
                ins.push_back({{"id", "base:instrument/" + std::to_string(n)}, {"name", gm::romInstrument(n).name}, {"gm", gm::romInstrument(n).program}});
            audio["instruments"] = ins;
            json drums = json::array();
            for (int n = 0; n < gm::kRomDrums; ++n)
                drums.push_back({{"id", "base:drum/" + std::to_string(n)}, {"name", gm::romDrum(n).name}, {"note", gm::romDrum(n).note}});
            audio["drums"] = drums;
            out["audio"] = audio;
        }

        json texts = json::object();
        for (const auto &[id, t] : db.allTexts())
            if (id.rfind("base:", 0) == 0)
                texts[id] = t.text;
        out["texts"] = texts;

        std::ofstream f(outDir / "base.json", std::ios::binary);
        if (!f)
            return "cannot write " + (outDir / "base.json").string();
        f << out.dump(1, ' ', false, json::error_handler_t::replace); // (some ROM text holds bytes that are not UTF-8)
        return {};
    }

    namespace exportimpl
    {
        inline std::string base64(const std::vector<uint8_t> &d)
        {
            static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve((d.size() + 2) / 3 * 4);
            for (size_t i = 0; i < d.size(); i += 3)
            {
                const uint32_t v = (uint32_t(d[i]) << 16) | (i + 1 < d.size() ? uint32_t(d[i + 1]) << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0);
                out += t[(v >> 18) & 63];
                out += t[(v >> 12) & 63];
                out += i + 1 < d.size() ? t[(v >> 6) & 63] : '=';
                out += i + 2 < d.size() ? t[v & 63] : '=';
            }
            return out;
        }
    } // namespace exportimpl

    // Writes <outDir>/pets.json: for each species (dog, monkey, panda) its colour machine palettes (as the ROM's 5-5-5 words, with the
    // species' default marked) and its main animations as palette INDICES (so a tool can colour them with any palette).
    //   { format, species: [ { name, defaultPalette, presets: [palette numbers in the machine's order],
    //       palettes: { "<number>": [words] }, anims: [ { name, id, frames: [ { w, h, ax, ay, dx, dy, idx: <base64 of w*h bytes> } ] } ] } ] }
    // A frame is drawn at (x - ax + dx, y - ay + dy), like the game does. Index 0 is see-through.
    inline std::string exportPets(const ContentDB &db, const Rom &rom, const fs::path &outDir)
    {
        using namespace exportimpl;
        struct Named
        {
            const char *name;
            int id;
        };
        json species = json::array();
        for (int sp = 0; sp < 3; ++sp)
        {
            const SpeciesDef *sd = db.speciesByLegacy(sp);
            json j;
            j["name"] = sp == 0 ? "dog" : sp == 1 ? "monkey" : "panda";
            j["defaultPalette"] = sd ? sd->defaultPalette : colorBase(sp) + 2;
            json presets = json::array(), palettes = json::object();
            auto addPalette = [&](int number)
            {
                Palette p;
                if (!readPalette(rom, number, p))
                    return;
                json w = json::array();
                for (uint16_t c : p.colors)
                    w.push_back(c);
                palettes[std::to_string(number)] = w;
            };
            for (int off : kColorPresetTable)
            {
                presets.push_back(colorBase(sp) + off);
                addPalette(colorBase(sp) + off);
            }
            if (!palettes.contains(std::to_string(int(j["defaultPalette"]))))
                addPalette(j["defaultPalette"]);
            j["presets"] = presets;
            j["palettes"] = palettes;

            std::vector<Named> list = {{"idle", gp::artOf(sp).idleBase}, {"idle sideways", gp::artOf(sp).idleSide}, {"walk down", gp::artOf(sp).walkDown}, {"walk up", gp::artOf(sp).walkUp},
                                       {"walk sideways", gp::artOf(sp).walkSide}, {"turn around", gp::artOf(sp).turn180}, {"turn sideways", gp::artOf(sp).turn90},
                                       {"settled", gp::artOf(sp).settled}, {"call (front)", gp::artOf(sp).callFront}, {"call (side)", gp::artOf(sp).callSide},
                                       {"idle (sick)", gp::artOf(sp).idleFlagB}, {"idle (sad)", gp::artOf(sp).idleFlagA}};
            static const char *kReactNames[] = {"reaction"};
            for (int rid : reactIdsForTier(sp))
                list.push_back({kReactNames[0], rid});
            json anims = json::array();
            int reactionNo = 0;
            for (const Named &n : list)
            {
                const uint32_t rec = kAnimTable + uint32_t(n.id) * 5;
                if (rec + 5 > rom.sizeWords())
                    continue;
                const int start = rom.u16(rec), end = rom.u16(rec + 1);
                if (end <= start || end - start > 256)
                    continue;
                const uint32_t tbl = rom.u16(rec + 4);
                json frames = json::array();
                for (int sid = start; sid < end; ++sid)
                {
                    DecodedSprite s;
                    if (!decodeSprite(rom, sid, s))
                        continue;
                    const int k = sid - start;
                    frames.push_back({{"w", s.w}, {"h", s.h}, {"ax", s.anchorX}, {"ay", s.anchorY}, {"dx", int(int16_t(rom.u16(tbl + k * 4)))},
                                      {"dy", int(int16_t(rom.u16(tbl + k * 4 + 1)))}, {"idx", base64(s.idx)}});
                }
                if (frames.empty())
                    continue;
                std::string name = n.name;
                if (name == "reaction")
                    name += " " + std::to_string(++reactionNo);
                anims.push_back({{"name", name}, {"id", n.id}, {"frames", frames}});
            }
            j["anims"] = anims;
            species.push_back(j);
        }
        json out = {{"format", 1}, {"species", species}};
        std::error_code ec;
        fs::create_directories(outDir, ec);
        std::ofstream f(outDir / "pets.json", std::ios::binary);
        if (!f)
            return "cannot write " + (outDir / "pets.json").string();
        f << out.dump(0);
        return {};
    }
} // namespace gp
