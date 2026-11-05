// mod_loader.hpp - importMods: scan mods/, sort, apply records on top of the db (after importRom).
// Phase 1: only the "text" record type exists. Every other type is a loud error, so typos can't pass silently.
#pragma once
#include "../engine/midi.hpp"           // a mod's MIDI music
#include "../engine/audio_clip.hpp"     // reading a mod's sound files
#include "../engine/audio_registry.hpp" // the sounds and songs of the mods
#include "../game/npc_anim_ids.hpp" // a species' art and the numbers of a mod's animations
#include "../content/db.hpp"
#include "../game/item_use.hpp"  // itemActions(): the registry an item's use.action is checked against
#include "../game/menu_draw.hpp" // kMenuPalette
#include "../loaders/font.hpp"   // registerUiArt
#include "manifest.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC // static functions, so including this from two .cpp files can't collide
#define STBI_ONLY_PNG
#include "stb/stb_image.h"

namespace gp
{
    struct ModReport
    {
        std::vector<std::string> errors, warnings;
        std::vector<std::string> loaded;               // mod ids in load order
        std::map<std::string, std::string> versions;   // mod id -> its manifest version
        std::map<std::string, std::string> lastWriter; // record id -> mod id (provenance, for later conflict logs)
        int nextAreaNumber = 1000;                     // the number the next mod area gets (AreaDef::legacyId); the base game uses 0..40
        bool ok() const { return errors.empty(); }
    };

    namespace modimpl
    {
        inline void err(ModReport &r, const fs::path &file, const std::string &mod, const std::string &msg)
        {
            r.errors.push_back("[" + mod + "] " + file.string() + ": " + msg);
        }

        inline void applyText(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "text"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                {
                    err(rep, file, m.id, "unknown field '" + it.key() + "' in text record");
                    return;
                }
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            if (!rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, "record '" + rec["id"].get<std::string>() + "' has no 'mode'");

            const std::string id = rec["id"].get<std::string>();
            const std::string mode = rec["mode"].get<std::string>();
            const bool exists = db.hasText(id);
            const bool hasBody = rec.contains("text") && rec["text"].is_string();

            if (mode == "add")
            {
                if (id.rfind(m.id + ":", 0) != 0)
                    return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                if (exists)
                    return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace or patch)");
                if (!hasBody)
                    return err(rep, file, m.id, "add '" + id + "': missing 'text'");
                db.put(TextDef{id, rec["text"].get<std::string>()});
            }
            else if (mode == "replace" || mode == "patch")
            {
                if (!exists)
                    return err(rep, file, m.id, mode + " '" + id + "': no such record");
                if (!hasBody)
                    return err(rep, file, m.id, mode + " '" + id + "': missing 'text'");
                db.put(TextDef{id, rec["text"].get<std::string>()}); // one-field record: patch == replace
            }
            else if (mode == "remove")
            {
                if (!exists)
                    return err(rep, file, m.id, "remove '" + id + "': no such record");
                db.eraseText(id);
                rep.warnings.push_back("[" + m.id + "] " + file.string() + ": removed '" + id +
                                       "'; references to it are not checked yet");
            }
            else
                return err(rep, file, m.id, "unknown mode '" + mode + "' on '" + id + "'");

            rep.lastWriter[id] = m.id;
        }

        inline bool validIdChars(const std::string &s)
        {
            if (s.empty())
                return false;
            for (unsigned char c : s)
                if (c <= 32 || c == 127 || c == ',' || c == '=' || c == '|')
                    return false;
            return true;
        }

        inline void applyNpc(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "name", "area", "x", "y",
                                                       "stationary", "anim", "lines", "palette", "based_on", "quest_min", "quest_max", "species", "quest_pool", "quest_tier"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in npc record");
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            if (!rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, "record '" + rec["id"].get<std::string>() + "' has no 'mode'");

            const std::string id = rec["id"].get<std::string>();
            const std::string mode = rec["mode"].get<std::string>();
            const bool exists = db.hasNpc(id);

            if (mode == "remove")
            {
                if (!exists)
                    return err(rep, file, m.id, "remove '" + id + "': no such npc");
                db.eraseNpc(id);
                rep.warnings.push_back("[" + m.id + "] " + file.string() + ": removed npc '" + id +
                                       "'; references to it are not checked yet");
                rep.lastWriter[id] = m.id;
                return;
            }
            if (mode == "patch")
                return err(rep, file, m.id, "patch on npc '" + id + "' is not supported yet (use replace)");
            if (mode != "add" && mode != "replace")
                return err(rep, file, m.id, "unknown mode '" + mode + "' on '" + id + "'");

            if (mode == "add")
            {
                if (id.rfind(m.id + ":", 0) != 0)
                    return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                if (!validIdChars(id))
                    return err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                if (exists)
                    return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace)");
            }
            else if (!exists)
                return err(rep, file, m.id, "replace '" + id + "': no such npc");

            // replace starts from the existing record, so unlisted fields keep their values
            NpcDef n;
            if (exists)
                n = *db.npc(id);
            else
                n.index = 1000 + db.npcCount();
            n.id = id;
            if (rec.contains("based_on")) // a new npc that starts as a copy of another (its path, stats, behaviour and art), e.g. a base pet
            {
                if (mode != "add" || !rec["based_on"].is_string())
                    return err(rep, file, m.id, "'" + id + "': 'based_on' is an npc id and only goes with mode add");
                const NpcDef *from = db.npc(rec["based_on"].get<std::string>());
                if (!from)
                    return err(rep, file, m.id, "'" + id + "': 'based_on' names an unknown npc '" + rec["based_on"].get<std::string>() + "'");
                n = *from;
                n.id = id;
                n.index = 1000 + db.npcCount();
            }
            if (rec.contains("species")) // a roaming pet drawn with a species' art: the record is found once every mod is loaded
            {
                if (!rec["species"].is_string())
                    return err(rep, file, m.id, "'" + id + "': 'species' must be the id of a species");
                n.speciesId = rec["species"].get<std::string>();
            }
            if (rec.contains("palette"))
            {
                if (!rec["palette"].is_string() || !db.palette(rec["palette"].get<std::string>()))
                    return err(rep, file, m.id, "'" + id + "': 'palette' must be the id of a palette record");
                n.palette = rec["palette"].get<std::string>();
            }

            auto intField = [&](const char *k, int &out) -> bool
            {
                if (!rec.contains(k))
                    return true;
                if (!rec[k].is_number_integer())
                {
                    err(rep, file, m.id, "'" + id + "': '" + std::string(k) + "' must be an integer");
                    return false;
                }
                out = rec[k].get<int>();
                return true;
            };
            if (rec.contains("area") && rec["area"].is_string()) // an area id: the base game's or a mod's own
            {
                const AreaDef *ad = db.area(rec["area"].get<std::string>());
                if (!ad)
                    return err(rep, file, m.id, "'" + id + "': unknown area '" + rec["area"].get<std::string>() + "'");
                n.area = ad->legacyId;
            }
            else if (!intField("area", n.area))
                return;
            if (!intField("x", n.x) || !intField("y", n.y) || !intField("quest_min", n.questMin) || !intField("quest_max", n.questMax))
                return;
            if (rec.contains("quest_pool"))
            {
                if (!rec["quest_pool"].is_boolean())
                    return err(rep, file, m.id, "'" + id + "': 'quest_pool' must be true or false");
                n.questPool = rec["quest_pool"].get<bool>();
            }
            if (rec.contains("quest_tier"))
            {
                static const std::set<int> tiers{0, 1, 2, 3, 5, 6, 7, 9};
                if (!rec["quest_tier"].is_number_integer() || !tiers.count(rec["quest_tier"].get<int>()))
                    return err(rep, file, m.id, "'" + id + "': 'quest_tier' must be one of 0, 1, 2, 3, 5, 6, 7, 9 (when quests may use this npc: 0 = always, 1 = after 3 quests, 2 / 3 = after 16, 9 = after 15, 6 = after 41, 7 = after 60, 5 = after 89)");
                n.questTier = rec["quest_tier"].get<int>();
            }
            if (n.questMin < 0 || n.questMax < 0)
                return err(rep, file, m.id, "'" + id + "': 'quest_min' / 'quest_max' cannot be negative");
            if (n.questMax <= n.questMin)
                return err(rep, file, m.id, "'" + id + "': 'quest_max' must be greater than 'quest_min' (it is the first quest count it is gone)");
            if (rec.contains("stationary"))
            {
                if (!rec["stationary"].is_boolean())
                    return err(rep, file, m.id, "'" + id + "': 'stationary' must be true or false");
                n.stationary = rec["stationary"].get<bool>();
            }
            if (rec.contains("name"))
            {
                if (!rec["name"].is_string())
                    return err(rep, file, m.id, "'" + id + "': 'name' must be a text id");
                n.nameText = rec["name"].get<std::string>();
            }
            if (rec.contains("anim"))
            {
                if (!rec["anim"].is_string())
                    return err(rep, file, m.id, "'" + id + "': 'anim' must be an anim id");
                n.anim = rec["anim"].get<std::string>();
                if (!db.anim(n.anim))
                    return err(rep, file, m.id, "'" + id + "': unknown anim '" + n.anim + "'");
            }
            if (rec.contains("lines"))
            {
                if (!rec["lines"].is_array())
                    return err(rep, file, m.id, "'" + id + "': 'lines' must be an array of text ids");
                n.lines.clear();
                for (const json &l : rec["lines"])
                {
                    if (!l.is_string())
                        return err(rep, file, m.id, "'" + id + "': every entry of 'lines' must be a text id");
                    n.lines.push_back(l.get<std::string>());
                }
            }
            if (!n.speciesId.empty() && n.stationary)
                return err(rep, file, m.id, "'" + id + "': 'species' is for roaming pets, not a stationary npc");
            if (mode == "add" && n.anim.empty() && n.speciesId.empty())
                return err(rep, file, m.id, "add '" + id + "': missing 'anim'");
            if (n.stationary && n.lines.size() < 3)
                return err(rep, file, m.id, "'" + id + "': a stationary npc needs 3 lines");

            db.put(std::move(n));
            rep.lastWriter[id] = m.id;
        }

        inline void applyAnim(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "frames", "palette"};
            static const std::set<std::string> frameAllowed{"png", "anchorX", "anchorY", "dx", "dy"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in anim record");
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            if (!rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, "record '" + rec["id"].get<std::string>() + "' has no 'mode'");

            const std::string id = rec["id"].get<std::string>();
            const std::string mode = rec["mode"].get<std::string>();
            const bool exists = db.hasAnim(id);

            if (mode == "remove")
            {
                if (!exists)
                    return err(rep, file, m.id, "remove '" + id + "': no such anim");
                db.eraseAnim(id);
                rep.warnings.push_back("[" + m.id + "] " + file.string() + ": removed anim '" + id +
                                       "'; references to it are not checked yet");
                rep.lastWriter[id] = m.id;
                return;
            }
            if (mode == "patch")
                return err(rep, file, m.id, "patch on anim '" + id + "' is not supported yet (use replace)");
            if (mode != "add" && mode != "replace")
                return err(rep, file, m.id, "unknown mode '" + mode + "' on '" + id + "'");
            if (mode == "add")
            {
                if (id.rfind(m.id + ":", 0) != 0)
                    return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                if (!validIdChars(id))
                    return err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                if (exists)
                    return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace)");
            }
            else if (!exists)
                return err(rep, file, m.id, "replace '" + id + "': no such anim");

            if (!rec.contains("frames") || !rec["frames"].is_array() || rec["frames"].empty())
                return err(rep, file, m.id, "'" + id + "': 'frames' must be a non-empty array");

            Anim a;
            a.id = id;
            if (rec.contains("palette")) // the game palette the pictures are drawn with: they take the pet's colours instead of staying as drawn
            {
                if (!rec["palette"].is_number_integer() || rec["palette"].get<int>() < 0x101 || rec["palette"].get<int>() > 0xfff)
                    return err(rep, file, m.id, "'" + id + "': 'palette' must be a game palette number, like 479 (0x1df, the dog's colours)");
                a.refPalette = rec["palette"].get<int>();
            }
            for (const json &f : rec["frames"])
            {
                if (!f.is_object())
                    return err(rep, file, m.id, "'" + id + "': every frame must be an object");
                for (auto it = f.begin(); it != f.end(); ++it)
                    if (!frameAllowed.count(it.key()))
                        return err(rep, file, m.id, "'" + id + "': unknown frame field '" + it.key() + "'");
                if (!f.contains("png") || !f["png"].is_string())
                    return err(rep, file, m.id, "'" + id + "': frame has no string 'png'");

                const fs::path rel = f["png"].get<std::string>();
                if (rel.is_absolute())
                    return err(rep, file, m.id, "'" + id + "': png path must be relative to the mod folder");
                for (const auto &part : rel)
                    if (part == "..")
                        return err(rep, file, m.id, "'" + id + "': png path may not contain '..'");
                const fs::path png = m.dir / rel;
                if (!fs::exists(png))
                    return err(rep, file, m.id, "'" + id + "': missing file " + png.string());

                int w = 0, h = 0, comp = 0;
                unsigned char *px = stbi_load(png.string().c_str(), &w, &h, &comp, 4);
                if (!px)
                    return err(rep, file, m.id, "'" + id + "': cannot decode " + png.string() + " (" + stbi_failure_reason() + ")");
                if (w > 1024 || h > 1024)
                {
                    stbi_image_free(px);
                    return err(rep, file, m.id, "'" + id + "': " + png.string() + " is larger than 1024x1024");
                }

                SpriteFrame sf;
                sf.w = w;
                sf.h = h;
                sf.rgba.assign(px, px + size_t(w) * h * 4);
                stbi_image_free(px);

                auto num = [&](const char *k, int def)
                { return f.contains(k) && f[k].is_number_integer() ? f[k].get<int>() : def; };
                sf.anchorX = num("anchorX", w / 2); // default: bottom centre
                sf.anchorY = num("anchorY", h - 1);
                sf.dx = num("dx", 0);
                sf.dy = num("dy", 0);
                a.frames.push_back(std::move(sf));
            }
            db.put(std::move(a));
            rep.lastWriter[id] = m.id;
        }

        // What an item's use.action takes. Actions not listed here (a native handler a mod build registered) are not
        // checked. Unknown keys are errors so a typo like "hungr" can't pass silently.
        struct UseParams
        {
            std::set<std::string> num, str;
        };
        inline const std::map<std::string, UseParams> &useParams()
        {
            static const std::map<std::string, UseParams> t{
                {"food", {{"hunger", "happiness", "health", "raise", "lower"}, {"stat"}}},
                {"medicine", {{"illness"}, {}}},
                {"potion", {}},
                {"dialogue", {}},
                {"none", {}}};
            return t;
        }

        // The category tab an item shows under. Values are ItemType numbers; the strings are what a mod writes.
        inline bool itemCategory(const std::string &s, int &out)
        {
            static const std::map<std::string, ItemType> t{{"food", ItemType::Food},
                                                           {"dish", ItemType::FoodDish},
                                                           {"medicine", ItemType::Medicine},
                                                           {"quest", ItemType::GenericQuestItem}};
            auto i = t.find(s);
            if (i == t.end())
                return false;
            out = int(i->second);
            return true;
        }

        // Fields: category ("food" | "dish" | "medicine" | "quest"), name / plural (text ids), maxQty (1..99),
        // price (0..9999), icon / world (anim ids), use {action, num{}, str{}}.
        // icon is drawn at (x - anchorX + dx, y - anchorY + dy) from the inventory cell's corner, so give it anchor
        // 0,0. world is drawn from the item's spot on the ground; the default (bottom centre) usually fits.
        // On add, world defaults to icon. On replace only the fields given change.
        inline void applyItem(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "name", "plural", "category",
                                                       "maxQty", "price", "icon", "world", "use"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in item record");
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            if (!rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, "record '" + rec["id"].get<std::string>() + "' has no 'mode'");

            const std::string id = rec["id"].get<std::string>();
            const std::string mode = rec["mode"].get<std::string>();
            const bool exists = db.item(id) != nullptr;

            if (mode == "remove")
                return err(rep, file, m.id, "remove on item '" + id + "' is not supported yet (items are numbered by position)");
            if (mode == "patch")
                return err(rep, file, m.id, "patch on item '" + id + "' is not supported yet (use replace)");
            if (mode != "add" && mode != "replace")
                return err(rep, file, m.id, "unknown mode '" + mode + "' on '" + id + "'");

            if (mode == "add")
            {
                if (id.rfind(m.id + ":", 0) != 0)
                    return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                if (!validIdChars(id))
                    return err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                if (exists)
                    return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace)");
            }
            else if (!exists)
                return err(rep, file, m.id, "replace '" + id + "': no such item");

            ItemDef d;
            if (exists)
                d = *db.item(id);
            else
            {
                d.id = id;
                d.maxQty = 9;
                d.decorSprite = 0xffff; // not a decor item
                d.worldSprite = 0xffff;
            }

            auto textField = [&](const char *k, Id &out) -> bool
            {
                if (!rec.contains(k))
                    return true;
                if (!rec[k].is_string())
                {
                    err(rep, file, m.id, "'" + id + "': '" + std::string(k) + "' must be a text id");
                    return false;
                }
                out = rec[k].get<std::string>();
                if (!db.hasText(out))
                {
                    err(rep, file, m.id, "'" + id + "': unknown text '" + out + "' in '" + std::string(k) + "'");
                    return false;
                }
                return true;
            };
            auto intField = [&](const char *k, int lo, int hi, int &out) -> bool
            {
                if (!rec.contains(k))
                    return true;
                if (!rec[k].is_number_integer() || rec[k].get<int>() < lo || rec[k].get<int>() > hi)
                {
                    err(rep, file, m.id, "'" + id + "': '" + std::string(k) + "' must be an integer from " +
                                             std::to_string(lo) + " to " + std::to_string(hi));
                    return false;
                }
                out = rec[k].get<int>();
                return true;
            };
            if (!textField("name", d.name) || !textField("plural", d.plural) ||
                !intField("maxQty", 1, 99, d.maxQty) || !intField("price", 0, 9999, d.price))
                return;
            if (mode == "add" && d.name.empty())
                return err(rep, file, m.id, "add '" + id + "': missing 'name'");

            if (rec.contains("category"))
            {
                if (!rec["category"].is_string() || !itemCategory(rec["category"].get<std::string>(), d.type))
                    return err(rep, file, m.id, "'" + id + "': 'category' must be food, dish, medicine or quest");
            }
            else if (mode == "add")
                return err(rep, file, m.id, "add '" + id + "': missing 'category'");

            if (rec.contains("use"))
            {
                const json &u = rec["use"];
                if (!u.is_object() || !u.contains("action") || !u["action"].is_string())
                    return err(rep, file, m.id, "'" + id + "': 'use' must be an object with a string 'action'");
                ItemUseDef use;
                use.action = u["action"].get<std::string>();
                if (!itemActions().find(use.action))
                    return err(rep, file, m.id, "'" + id + "': unknown use action '" + use.action + "'");
                const auto pi = useParams().find(use.action);
                for (auto it = u.begin(); it != u.end(); ++it)
                    if (it.key() != "action" && it.key() != "num" && it.key() != "str")
                        return err(rep, file, m.id, "'" + id + "': unknown field '" + it.key() + "' in use");
                if (u.contains("num"))
                {
                    if (!u["num"].is_object())
                        return err(rep, file, m.id, "'" + id + "': use.num must be an object of integers");
                    for (auto it = u["num"].begin(); it != u["num"].end(); ++it)
                    {
                        if (!it.value().is_number_integer())
                            return err(rep, file, m.id, "'" + id + "': use.num '" + it.key() + "' must be an integer");
                        if (pi != useParams().end() && !pi->second.num.count(it.key()))
                            return err(rep, file, m.id, "'" + id + "': action '" + use.action + "' has no number '" + it.key() + "'");
                        use.num[it.key()] = it.value().get<int>();
                    }
                }
                if (u.contains("str"))
                {
                    if (!u["str"].is_object())
                        return err(rep, file, m.id, "'" + id + "': use.str must be an object of strings");
                    for (auto it = u["str"].begin(); it != u["str"].end(); ++it)
                    {
                        if (!it.value().is_string())
                            return err(rep, file, m.id, "'" + id + "': use.str '" + it.key() + "' must be a string");
                        if (pi != useParams().end() && !pi->second.str.count(it.key()))
                            return err(rep, file, m.id, "'" + id + "': action '" + use.action + "' has no string '" + it.key() + "'");
                        use.str[it.key()] = it.value().get<std::string>();
                    }
                }
                if (use.action == "food")
                {
                    PersonalityStat ignored;
                    auto s = use.str.find("stat");
                    if (s != use.str.end() && !detail::statByName(s->second, ignored))
                        return err(rep, file, m.id, "'" + id + "': use.str.stat must be halos, pitchforks, stars or lightbulbs");
                    if (use.num.count("raise") && use.num.count("lower"))
                        return err(rep, file, m.id, "'" + id + "': use.num may have 'raise' or 'lower', not both");
                }
                d.use = std::move(use);
            }
            else if (mode == "add")
            {
                if (d.type != int(ItemType::GenericQuestItem))
                    return err(rep, file, m.id, "add '" + id + "': missing 'use' (only quest items default to doing nothing)");
                d.use.action = "none";
            }

            // Art. A mod item's sprite numbers start at kModSpriteBase; the same anim can be registered twice.
            auto artField = [&](const char *k, int &sprite, bool &given) -> bool
            {
                given = false;
                if (!rec.contains(k))
                    return true;
                if (!rec[k].is_string())
                {
                    err(rep, file, m.id, "'" + id + "': '" + std::string(k) + "' must be an anim id");
                    return false;
                }
                const Anim *a = db.anim(rec[k].get<std::string>());
                if (!a || a->frames.empty())
                {
                    err(rep, file, m.id, "'" + id + "': unknown anim '" + rec[k].get<std::string>() + "' in '" + std::string(k) + "'");
                    return false;
                }
                sprite = registerUiArt(db, Anim(*a), kMenuPalette);
                given = true;
                return true;
            };
            bool hasIcon = false, hasWorld = false;
            if (!artField("icon", d.iconSprite, hasIcon) || !artField("world", d.worldSprite, hasWorld))
                return;
            if (mode == "add" && !hasIcon)
                return err(rep, file, m.id, "add '" + id + "': missing 'icon'");
            if (mode == "add" && !hasWorld)
                d.worldSprite = d.iconSprite;

            db.put(std::move(d));
            rep.lastWriter[id] = m.id;
        }

        // ---- world records: tileset, collision, objtype, area, object, exit ----
        // All of these are add-only for now ("replace" / "remove" / "patch" are errors that say so). They let a mod build a
        // playable area from tiles: a tile sheet, a collision sheet, a map of tile numbers, placed objects, and exits.

        struct PngData
        {
            int w = 0, h = 0;
            std::vector<uint8_t> rgba;
        };

        // A path inside the mod folder: relative, no "..", must exist.
        inline bool modFile(const ModManifest &m, const std::string &rel, fs::path &out, std::string &why)
        {
            const fs::path r = rel;
            if (r.empty() || r.is_absolute())
            {
                why = "path must be relative to the mod folder";
                return false;
            }
            for (const auto &part : r)
                if (part == "..")
                {
                    why = "path may not contain '..'";
                    return false;
                }
            out = m.dir / r;
            if (!fs::exists(out))
            {
                why = "missing file " + out.string();
                return false;
            }
            return true;
        }
        inline bool readModPng(const ModManifest &m, const std::string &rel, int maxDim, PngData &png, std::string &why)
        {
            fs::path p;
            if (!modFile(m, rel, p, why))
                return false;
            int w = 0, h = 0, comp = 0;
            unsigned char *px = stbi_load(p.string().c_str(), &w, &h, &comp, 4);
            if (!px)
            {
                why = "cannot decode " + p.string() + " (" + stbi_failure_reason() + ")";
                return false;
            }
            if (w > maxDim || h > maxDim)
            {
                stbi_image_free(px);
                why = p.string() + " is larger than " + std::to_string(maxDim) + "x" + std::to_string(maxDim);
                return false;
            }
            png.w = w;
            png.h = h;
            png.rgba.assign(px, px + size_t(w) * h * 4);
            stbi_image_free(px);
            return true;
        }

        // The start every add-only record shares: known fields only, a string id and mode, mode "add", the mod's own
        // prefix, valid characters, no duplicate. False = an error has been reported.
        inline bool beginAdd(const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep, const char *type,
                             const std::set<std::string> &allowed, const std::function<bool(const std::string &)> &exists, std::string &id)
        {
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                {
                    err(rep, file, m.id, "unknown field '" + it.key() + "' in " + type + " record");
                    return false;
                }
            if (!rec.contains("id") || !rec["id"].is_string())
            {
                err(rep, file, m.id, "record has no string 'id'");
                return false;
            }
            id = rec["id"].get<std::string>();
            if (!rec.contains("mode") || !rec["mode"].is_string())
            {
                err(rep, file, m.id, "record '" + id + "' has no 'mode'");
                return false;
            }
            if (rec["mode"].get<std::string>() != "add")
            {
                err(rep, file, m.id, "'" + id + "': " + type + " records only support mode 'add' so far");
                return false;
            }
            if (id.rfind(m.id + ":", 0) != 0)
            {
                err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                return false;
            }
            if (!validIdChars(id))
            {
                err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                return false;
            }
            if (exists(id))
            {
                err(rep, file, m.id, "add '" + id + "': duplicate id");
                return false;
            }
            return true;
        }

        // Optional integer field in lo..hi. False with `why` set if it is there and wrong.
        inline bool optInt(const json &rec, const char *k, int &out, int lo, int hi, std::string &why)
        {
            if (!rec.contains(k))
                return true;
            if (!rec[k].is_number_integer())
            {
                why = std::string("'") + k + "' must be an integer";
                return false;
            }
            const long long v = rec[k].get<long long>();
            if (v < lo || v > hi)
            {
                why = std::string("'") + k + "' must be " + std::to_string(lo) + " to " + std::to_string(hi);
                return false;
            }
            out = int(v);
            return true;
        }
        // Optional true / false field.
        inline bool optBool(const json &rec, const char *k, bool &out, std::string &why)
        {
            if (!rec.contains(k))
                return true;
            if (!rec[k].is_boolean())
            {
                why = std::string("'") + k + "' must be true or false";
                return false;
            }
            out = rec[k].get<bool>();
            return true;
        }
        // Optional [a, b] pair of integers.
        inline bool optPair(const json &rec, const char *k, int &a, int &b, int lo, int hi, std::string &why)
        {
            if (!rec.contains(k))
                return true;
            if (!rec[k].is_array() || rec[k].size() != 2 || !rec[k][0].is_number_integer() || !rec[k][1].is_number_integer())
            {
                why = std::string("'") + k + "' must be a pair of integers like [10, 20]";
                return false;
            }
            const long long x = rec[k][0].get<long long>(), y = rec[k][1].get<long long>();
            if (x < lo || x > hi || y < lo || y > hi)
            {
                why = std::string("'") + k + "' values must be " + std::to_string(lo) + " to " + std::to_string(hi);
                return false;
            }
            a = int(x);
            b = int(y);
            return true;
        }
        // Optional "music": the ROM's song number 0..8, or the id of a mod's music record (made in pass 0, so it is known by the time areas load).
        inline bool optMusic(const json &rec, int &out, std::string &why)
        {
            if (rec.contains("music") && rec["music"].is_string())
            {
                const int n = songNumberOf(rec["music"].get<std::string>()); // "base:music/ufo", a number's id, or a mod's music record
                if (n < 0 || (n < kModMusicBase && n > 8))
                    return why = "'music' names no song or music record: '" + rec["music"].get<std::string>() + "'", false;
                out = n;
                return true;
            }
            return optInt(rec, "music", out, 0, 8, why);
        }
        inline bool needStr(const json &rec, const char *k, std::string &out, std::string &why)
        {
            if (!rec.contains(k) || !rec[k].is_string())
            {
                why = std::string("'") + k + "' is required (a string)";
                return false;
            }
            out = rec[k].get<std::string>();
            return true;
        }
        // Optional string field (empty when absent).
        inline bool optStr(const json &rec, const char *k, std::string &out, std::string &why)
        {
            if (!rec.contains(k))
                return true;
            if (!rec[k].is_string())
            {
                why = std::string("'") + k + "' must be a string";
                return false;
            }
            out = rec[k].get<std::string>();
            return true;
        }

        // palette: a set of colours for sprites that come from the ROM (the roaming pets). Either { colors = [[r, g, b] | null, ...] } (a
        // full list, at most 256; null = see-through) or { based_on = a ROM palette number, set = { "<index>": [r, g, b] | null } } to
        // change single entries of an existing one (the pets' colours are 0x1e0 and up: see the base export). Entry 0 is always
        // see-through in a sprite. An npc record uses it with `palette`.
        inline bool rgbWord(const json &c, uint16_t &out, std::string &why)
        {
            if (c.is_null())
            {
                out = 0x8000;
                return true;
            }
            if (!c.is_array() || c.size() != 3)
            {
                why = "a colour is [r, g, b] (0 to 255 each) or null";
                return false;
            }
            int v[3];
            for (int i = 0; i < 3; ++i)
            {
                if (!c[size_t(i)].is_number_integer() || c[size_t(i)].get<int>() < 0 || c[size_t(i)].get<int>() > 255)
                {
                    why = "a colour is [r, g, b] (0 to 255 each) or null";
                    return false;
                }
                v[i] = (c[size_t(i)].get<int>() * 31 + 127) / 255;
            }
            out = uint16_t((v[0] << 10) | (v[1] << 5) | v[2]);
            return true;
        }
        inline void applyPalette(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "based_on", "set", "colors", "species"};
            std::string id, why;
            if (!beginAdd(m, file, rec, rep, "palette", allowed, [&](const std::string &i)
                          { return db.palette(i) != nullptr; }, id))
                return;
            PaletteDef p;
            p.id = id;
            if (rec.contains("species"))
            {
                const std::string sp = rec["species"].is_string() ? rec["species"].get<std::string>() : "";
                p.species = sp == "dog" ? 0 : sp == "monkey" ? 1 : sp == "panda" ? 2 : -1;
                if (p.species < 0)
                    return err(rep, file, m.id, "'" + id + "': 'species' must be \"dog\", \"monkey\" or \"panda\"");
            }
            if (rec.contains("based_on") == rec.contains("colors"))
                return err(rep, file, m.id, "'" + id + "': give exactly one of 'based_on' (+ 'set') or 'colors'");
            if (rec.contains("colors"))
            {
                if (rec.contains("set"))
                    return err(rep, file, m.id, "'" + id + "': 'set' goes with 'based_on'");
                if (!rec["colors"].is_array() || rec["colors"].empty() || rec["colors"].size() > 256)
                    return err(rep, file, m.id, "'" + id + "': 'colors' must be 1 to 256 colours");
                for (const json &c : rec["colors"])
                {
                    uint16_t w = 0;
                    if (!rgbWord(c, w, why))
                        return err(rep, file, m.id, "'" + id + "': " + why);
                    p.colors.push_back(w);
                }
            }
            else
            {
                if (!rec["based_on"].is_number_integer() || rec["based_on"].get<int>() <= 0x100 || rec["based_on"].get<int>() >= kModPaletteBase)
                    return err(rep, file, m.id, "'" + id + "': 'based_on' must be a ROM palette number (257 and up)");
                p.basedOn = rec["based_on"].get<int>();
                if (rec.contains("set"))
                {
                    if (!rec["set"].is_object() || rec["set"].size() > 256)
                        return err(rep, file, m.id, "'" + id + "': 'set' must be an object like {\"3\": [255, 80, 20]}");
                    for (auto it = rec["set"].begin(); it != rec["set"].end(); ++it)
                    {
                        char *end = nullptr;
                        const long idx = std::strtol(it.key().c_str(), &end, 10);
                        if (it.key().empty() || *end || idx < 0 || idx > 255)
                            return err(rep, file, m.id, "'" + id + "': set index '" + it.key() + "' must be a number from 0 to 255");
                        uint16_t w = 0;
                        if (!rgbWord(it.value(), w, why))
                            return err(rep, file, m.id, "'" + id + "': " + why);
                        p.set.push_back({int(idx), w});
                    }
                }
            }
            db.put(std::move(p));
            rep.lastWriter[id] = m.id;
        }

        // tileset: a PNG sheet cut into square tiles, numbered row by row from 0. { png, tile = 32 }
        inline void applyTileset(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "png", "tile"};
            std::string id, why;
            if (!beginAdd(m, file, rec, rep, "tileset", allowed, [&](const std::string &i)
                          { return db.tileset(i) != nullptr; }, id))
                return;
            std::string rel;
            int tile = 32;
            PngData png;
            if (!needStr(rec, "png", rel, why) || !optInt(rec, "tile", tile, 8, 128, why) || !readModPng(m, rel, 4096, png, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (png.w % tile || png.h % tile)
                return err(rep, file, m.id, "'" + id + "': the picture is " + std::to_string(png.w) + "x" + std::to_string(png.h) + ", not a multiple of the tile size " + std::to_string(tile));
            const int cols = png.w / tile, rows = png.h / tile;
            if (cols * rows > 4096)
                return err(rep, file, m.id, "'" + id + "': more than 4096 tiles");
            Tileset ts;
            ts.id = id;
            ts.tileW = ts.tileH = tile;
            ts.count = cols * rows;
            ts.rgba.resize(size_t(ts.count) * tile * tile * 4);
            for (int t = 0; t < ts.count; ++t)
                for (int y = 0; y < tile; ++y)
                {
                    const uint8_t *src = &png.rgba[(size_t((t / cols) * tile + y) * png.w + size_t(t % cols) * tile) * 4];
                    std::copy(src, src + size_t(tile) * 4, &ts.rgba[(size_t(t) * tile * tile + size_t(y) * tile) * 4]);
                }
            db.put(std::move(ts));
            rep.lastWriter[id] = m.id;
        }

        // collision: what blocks walking, per tile. { png, tile = 32 } = a sheet cut into square cells like the tileset (use the
        // tileset's own `tile` size and the same layout, and cell n is tile n); a pixel is blocked where it is at least half opaque.
        // The game checks a 32 x 32 grid per tile, so another cell size is scaled to it: a bigger cell blocks a grid pixel if any of
        // the pixels it covers is blocked. Or { count } with { blocked_tiles: [3, 4] } for whole blocked tiles. Both can be used together.
        inline void applyCollision(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "png", "tile", "count", "blocked_tiles"};
            std::string id, why;
            if (!beginAdd(m, file, rec, rep, "collision", allowed, [&](const std::string &i)
                          { return db.collision(i) != nullptr; }, id))
                return;
            CollisionSet cs;
            cs.id = id;
            cs.tileW = cs.tileH = 32;
            if (rec.contains("png"))
            {
                if (rec.contains("count"))
                    return err(rep, file, m.id, "'" + id + "': give 'png' or 'count', not both");
                std::string rel;
                PngData png;
                if (!needStr(rec, "png", rel, why) || !readModPng(m, rel, 4096, png, why))
                    return err(rep, file, m.id, "'" + id + "': " + why);
                int tile = 32;
                if (!optInt(rec, "tile", tile, 8, 128, why))
                    return err(rep, file, m.id, "'" + id + "': " + why);
                if (png.w % tile || png.h % tile)
                    return err(rep, file, m.id, "'" + id + "': the picture is " + std::to_string(png.w) + "x" + std::to_string(png.h) + ", not a multiple of the tile size " + std::to_string(tile) + " (set 'tile' to the cell size)");
                const int cols = png.w / tile, rows = png.h / tile;
                if (cols * rows > 4096)
                    return err(rep, file, m.id, "'" + id + "': more than 4096 tiles");
                cs.count = cols * rows;
                cs.blocked.assign(size_t(cs.count) * 1024, 0);
                for (int t = 0; t < cs.count; ++t)
                    for (int y = 0; y < 32; ++y)
                        for (int x = 0; x < 32; ++x)
                        {
                            // the source pixels this grid pixel covers (at least one)
                            const int x0 = x * tile / 32, x1 = std::max(x0 + 1, (x + 1) * tile / 32);
                            const int y0 = y * tile / 32, y1 = std::max(y0 + 1, (y + 1) * tile / 32);
                            bool blocked = false;
                            for (int sy = y0; sy < y1 && !blocked; ++sy)
                                for (int sx = x0; sx < x1 && !blocked; ++sx)
                                    blocked = png.rgba[(size_t((t / cols) * tile + sy) * png.w + size_t(t % cols) * tile + sx) * 4 + 3] >= 128;
                            cs.blocked[size_t(t) * 1024 + size_t(y) * 32 + x] = blocked ? 1 : 0;
                        }
            }
            else
            {
                int count = 0;
                if (!rec.contains("count") || !optInt(rec, "count", count, 1, 4096, why))
                    return err(rep, file, m.id, "'" + id + "': " + (why.empty() ? "give 'png' or 'count'" : why));
                cs.count = count;
                cs.blocked.assign(size_t(count) * 1024, 0);
            }
            if (rec.contains("blocked_tiles"))
            {
                if (!rec["blocked_tiles"].is_array())
                    return err(rep, file, m.id, "'" + id + "': 'blocked_tiles' must be an array of tile numbers");
                for (const json &t : rec["blocked_tiles"])
                {
                    if (!t.is_number_integer() || t.get<int>() < 0 || t.get<int>() >= cs.count)
                        return err(rep, file, m.id, "'" + id + "': 'blocked_tiles' has a tile number outside 0 to " + std::to_string(cs.count - 1));
                    std::fill_n(cs.blocked.begin() + std::ptrdiff_t(size_t(t.get<int>()) * 1024), 1024, uint8_t(1));
                }
            }
            db.put(std::move(cs));
            rep.lastWriter[id] = m.id;
        }

        // objtype: the art and footprint of a placed object. { png, anchorX, anchorY, dx, dy, box = [left, top, right, bottom],
        // collision_png, ground }. The anchor is the object's feet (default: bottom centre of the picture). `box` is the part
        // of the ground it blocks, in pixels from the feet; `collision_png` is a mask the size of the art (opaque = blocked).
        // `ground: true` draws it flat under everything (a bridge, a floor decal) instead of sorting it with the pet.
        // The shadow fields of an objtype record (add or patch): shadow = "default" | "none" | "auto", or shadow_png (+ shadow_anchorX,
        // shadow_anchorY, default the middle of the picture) for a picture. Sets `ot.shadow` and registers the picture as an anim.
        // False = an error has been reported.
        inline bool readShadow(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep, const std::string &id, ObjectType &ot)
        {
            std::string mode, rel, why;
            if (!optStr(rec, "shadow", mode, why) || !optStr(rec, "shadow_png", rel, why))
                return err(rep, file, m.id, "'" + id + "': " + why), false;
            if (!mode.empty() && mode != "default" && mode != "none" && mode != "auto" && mode != "picture")
                return err(rep, file, m.id, "'" + id + "': 'shadow' must be \"default\", \"none\", \"auto\" or \"picture\" (or give 'shadow_png')"), false;
            if (!rel.empty() && !mode.empty() && mode != "picture")
                return err(rep, file, m.id, "'" + id + "': 'shadow_png' goes with shadow \"picture\", not \"" + mode + "\""), false;
            if (mode == "picture" && rel.empty())
                return err(rep, file, m.id, "'" + id + "': shadow \"picture\" needs 'shadow_png'"), false;
            if (rel.empty())
            {
                if (rec.contains("shadow_anchorX") || rec.contains("shadow_anchorY"))
                    return err(rep, file, m.id, "'" + id + "': 'shadow_anchorX' / 'shadow_anchorY' need 'shadow_png'"), false;
                if (mode == "none")
                    ot.shadow = ObjectType::ShadowNone;
                else if (mode == "auto")
                    ot.shadow = ObjectType::ShadowAuto;
                else if (mode == "default")
                    ot.shadow = ObjectType::ShadowDefault;
                return true;
            }
            PngData png;
            if (!readModPng(m, rel, 256, png, why))
                return err(rep, file, m.id, "'" + id + "': " + why), false;
            Anim a;
            a.id = ObjectType::shadowAnim(ot.id);
            SpriteFrame f;
            f.w = png.w;
            f.h = png.h;
            f.rgba = png.rgba;
            f.anchorX = png.w / 2; // the middle of the picture sits on the object's feet
            f.anchorY = png.h / 2;
            if (!optInt(rec, "shadow_anchorX", f.anchorX, -256, 512, why) || !optInt(rec, "shadow_anchorY", f.anchorY, -256, 512, why))
                return err(rep, file, m.id, "'" + id + "': " + why), false;
            a.frames.push_back(std::move(f));
            db.put(std::move(a));
            ot.shadow = ObjectType::ShadowPicture;
            return true;
        }

        // objtype, mode "patch": change only the shadow of a type that exists (the base game's or a mod's). The art and footprint are not
        // patchable (make a new type for those).
        inline void applyObjTypeShadowPatch(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "shadow", "shadow_png", "shadow_anchorX", "shadow_anchorY"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in an objtype patch (a patch can only change the shadow)");
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            const std::string id = rec["id"].get<std::string>();
            const ObjectType *cur = db.objectType(id);
            if (!cur)
                return err(rep, file, m.id, "patch '" + id + "': no such objtype");
            if (!rec.contains("shadow") && !rec.contains("shadow_png"))
                return err(rep, file, m.id, "patch '" + id + "': nothing to change (give 'shadow' or 'shadow_png')");
            ObjectType ot = *cur;
            if (!readShadow(db, m, file, rec, rep, id, ot))
                return;
            db.put(std::move(ot));
            rep.lastWriter[id] = m.id;
        }

        inline void applyObjType(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "png", "anchorX", "anchorY", "dx", "dy", "box", "collision_png", "ground",
                                                       "shadow", "shadow_png", "shadow_anchorX", "shadow_anchorY"};
            if (rec.contains("mode") && rec["mode"].is_string() && rec["mode"].get<std::string>() == "patch")
                return applyObjTypeShadowPatch(db, m, file, rec, rep);
            std::string id, why;
            if (!beginAdd(m, file, rec, rep, "objtype", allowed, [&](const std::string &i)
                          { return db.objectType(i) != nullptr; }, id))
                return;
            std::string rel;
            PngData png;
            if (!needStr(rec, "png", rel, why) || !readModPng(m, rel, 1024, png, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            ObjectType ot;
            ot.id = id;
            ot.hasArt = true;
            ot.art.w = png.w;
            ot.art.h = png.h;
            ot.art.rgba = png.rgba;
            ot.art.anchorX = png.w / 2;
            ot.art.anchorY = png.h - 1;
            if (!optInt(rec, "anchorX", ot.art.anchorX, -1024, 2048, why) || !optInt(rec, "anchorY", ot.art.anchorY, -1024, 2048, why) ||
                !optInt(rec, "dx", ot.art.dx, -1024, 1024, why) || !optInt(rec, "dy", ot.art.dy, -1024, 1024, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            bool ground = false;
            if (rec.contains("ground"))
            {
                if (!rec["ground"].is_boolean())
                    return err(rep, file, m.id, "'" + id + "': 'ground' must be true or false");
                ground = rec["ground"].get<bool>();
            }
            ot.hasMarker = !ground; // (a type with no marker flag is drawn under everything: RenderStoryObjectsInYRange)
            if (rec.contains("box") && rec.contains("collision_png"))
                return err(rep, file, m.id, "'" + id + "': give 'box' or 'collision_png', not both");
            if (rec.contains("box"))
            {
                const json &b = rec["box"];
                if (!b.is_array() || b.size() != 4)
                    return err(rep, file, m.id, "'" + id + "': 'box' must be [left, top, right, bottom]");
                int v[4];
                for (int i = 0; i < 4; ++i)
                {
                    if (!b[size_t(i)].is_number_integer() || b[size_t(i)].get<int>() < -1024 || b[size_t(i)].get<int>() > 1024)
                        return err(rep, file, m.id, "'" + id + "': 'box' values must be integers from -1024 to 1024");
                    v[i] = b[size_t(i)].get<int>();
                }
                if (v[2] <= v[0] || v[3] <= v[1])
                    return err(rep, file, m.id, "'" + id + "': 'box' must have right > left and bottom > top");
                ot.mask.w = v[2] - v[0];
                ot.mask.h = v[3] - v[1];
                ot.mask.ox = v[0];
                ot.mask.oy = v[1];
                ot.mask.blocked.assign(size_t(ot.mask.w) * ot.mask.h, 1);
            }
            else if (rec.contains("collision_png"))
            {
                std::string crel;
                PngData mask;
                if (!needStr(rec, "collision_png", crel, why) || !readModPng(m, crel, 1024, mask, why))
                    return err(rep, file, m.id, "'" + id + "': " + why);
                ot.mask.w = mask.w;
                ot.mask.h = mask.h;
                ot.mask.ox = -ot.art.anchorX; // the mask shares the art's anchor
                ot.mask.oy = -ot.art.anchorY;
                ot.mask.blocked.resize(size_t(mask.w) * mask.h);
                for (size_t i = 0; i < ot.mask.blocked.size(); ++i)
                    ot.mask.blocked[i] = mask.rgba[i * 4 + 3] >= 128 ? 1 : 0;
            }
            if (!readShadow(db, m, file, rec, rep, id, ot))
                return;
            db.put(std::move(ot));
            rep.lastWriter[id] = m.id;
        }

        // area: a map of 32 x 32 tiles. { name = text id, tileset, collision, map = [[row of tile numbers], ...], margins = [left, right,
        // top, bottom] (default the base game's 42, 42, 62, 20), y_offset, music = a song 0..8 (default: keep the current one),
        // spawn_points = [[x, y], ...] (where random finds may appear), max_events }. Width and height follow the map (1 to 255 tiles).
        // The area gets a number of its own from 1000 up, which the parts of the game that still count areas by number use.
        // area, mode "patch": change an area that exists (the base game's or a mod's). Any of name, tileset, collision, margins,
        // y_offset, music, spawn_points, max_events as in add, plus `set_tiles` = [[column, row, tile], ...] to change single
        // tiles, or a whole new `map`. Tile numbers must exist in the (new) tileset and collision. Fields left out stay as they were.
        inline void applyAreaPatch(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "name", "tileset", "collision", "map", "set_tiles", "margins",
                                                       "y_offset", "music", "spawn_points", "max_events", "resume", "diggable"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in area record");
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            const std::string id = rec["id"].get<std::string>();
            AreaDef *base = db.areaMutable(id);
            if (!base)
                return err(rep, file, m.id, "patch '" + id + "': no such area");
            AreaDef a = *base;
            std::string why, tsId = a.tileset, csId = a.collision;
            if (!optStr(rec, "tileset", tsId, why) || !optStr(rec, "collision", csId, why) || !optStr(rec, "name", a.name, why))
                return err(rep, file, m.id, "patch '" + id + "': " + why);
            const Tileset *ts = db.tileset(tsId);
            const CollisionSet *cs = db.collision(csId);
            // a few base areas have no tile data at all; their other fields (music, resume, ...) can still be patched
            const bool noLook = tsId.empty() && csId.empty() && !rec.contains("map") && !rec.contains("set_tiles");
            if (!noLook && !ts)
                return err(rep, file, m.id, "patch '" + id + "': unknown tileset '" + tsId + "'");
            if (!noLook && !cs)
                return err(rep, file, m.id, "patch '" + id + "': unknown collision '" + csId + "'");
            if (!a.name.empty() && !db.hasText(a.name))
                return err(rep, file, m.id, "patch '" + id + "': unknown name text '" + a.name + "'");
            a.tileset = tsId;
            a.collision = csId;
            auto tileOk = [&](const json &t)
            { return t.is_number_integer() && t.get<int>() >= 0 && t.get<int>() < ts->count && t.get<int>() < cs->count; };
            if (rec.contains("map"))
            {
                if (!rec["map"].is_array() || rec["map"].empty() || rec["map"].size() > 255)
                    return err(rep, file, m.id, "patch '" + id + "': 'map' must be 1 to 255 rows");
                a.tilesH = int(rec["map"].size());
                a.tilesW = rec["map"][0].is_array() ? int(rec["map"][0].size()) : 0;
                if (a.tilesW < 1 || a.tilesW > 255)
                    return err(rep, file, m.id, "patch '" + id + "': every map row must be an array of 1 to 255 tile numbers");
                a.tiles.clear();
                for (int r = 0; r < a.tilesH; ++r)
                {
                    const json &row = rec["map"][size_t(r)];
                    if (!row.is_array() || int(row.size()) != a.tilesW)
                        return err(rep, file, m.id, "patch '" + id + "': map row " + std::to_string(r) + " has " + std::to_string(row.is_array() ? row.size() : 0) + " tiles, row 0 has " + std::to_string(a.tilesW));
                    for (int c = 0; c < a.tilesW; ++c)
                    {
                        if (!tileOk(row[size_t(c)]))
                            return err(rep, file, m.id, "patch '" + id + "': map row " + std::to_string(r) + ", column " + std::to_string(c) + " is not a tile number the tileset (" + std::to_string(ts->count) + " tiles) and collision (" + std::to_string(cs->count) + ") both have");
                        a.tiles.push_back(uint16_t(row[size_t(c)].get<int>()));
                    }
                }
            }
            else if (!noLook)
            {
                // without a new map every tile already there must still exist in the (possibly new) tileset and collision
                for (uint16_t t : a.tiles)
                    if (t >= ts->count || t >= cs->count)
                        return err(rep, file, m.id, "patch '" + id + "': the map uses tile " + std::to_string(t) + ", which the new tileset (" + std::to_string(ts->count) + " tiles) or collision (" + std::to_string(cs->count) + ") does not have");
            }
            if (rec.contains("set_tiles"))
            {
                if (!rec["set_tiles"].is_array() || rec["set_tiles"].size() > 65025)
                    return err(rep, file, m.id, "patch '" + id + "': 'set_tiles' must be an array of [column, row, tile]");
                for (const json &s : rec["set_tiles"])
                {
                    if (!s.is_array() || s.size() != 3 || !s[0].is_number_integer() || !s[1].is_number_integer() || !tileOk(s[2]))
                        return err(rep, file, m.id, "patch '" + id + "': every 'set_tiles' entry must be [column, row, tile] with a tile the tileset and collision have");
                    const int c = s[0].get<int>(), r = s[1].get<int>();
                    if (c < 0 || c >= a.tilesW || r < 0 || r >= a.tilesH)
                        return err(rep, file, m.id, "patch '" + id + "': set_tiles column " + std::to_string(c) + ", row " + std::to_string(r) + " is outside the " + std::to_string(a.tilesW) + "x" + std::to_string(a.tilesH) + " map");
                    a.tiles[size_t(r) * a.tilesW + c] = uint16_t(s[2].get<int>());
                }
            }
            if (rec.contains("margins"))
            {
                const json &mg = rec["margins"];
                if (!mg.is_array() || mg.size() != 4)
                    return err(rep, file, m.id, "patch '" + id + "': 'margins' must be [left, right, top, bottom]");
                int *dst[4] = {&a.marginL, &a.marginR, &a.marginT, &a.marginB};
                for (size_t i = 0; i < 4; ++i)
                {
                    if (!mg[i].is_number_integer() || mg[i].get<int>() < 0 || mg[i].get<int>() > 1000)
                        return err(rep, file, m.id, "patch '" + id + "': 'margins' values must be integers from 0 to 1000");
                    *dst[i] = mg[i].get<int>();
                }
            }
            if (!optInt(rec, "y_offset", a.collisionYOffset, -64, 64, why) || !optMusic(rec, a.music, why) ||
                !optInt(rec, "max_events", a.maxEvents, 0, 200, why) || !optBool(rec, "resume", a.resume, why) ||
                !optBool(rec, "diggable", a.diggable, why))
                return err(rep, file, m.id, "patch '" + id + "': " + why);
            if (rec.contains("spawn_points"))
            {
                if (!rec["spawn_points"].is_array() || rec["spawn_points"].size() > 500)
                    return err(rep, file, m.id, "patch '" + id + "': 'spawn_points' must be an array of at most 500 [x, y] pairs");
                a.spawnPoints.clear();
                for (const json &p : rec["spawn_points"])
                {
                    if (!p.is_array() || p.size() != 2 || !p[0].is_number_integer() || !p[1].is_number_integer())
                        return err(rep, file, m.id, "patch '" + id + "': every spawn point must be a pair like [100, 200]");
                    a.spawnPoints.push_back({p[0].get<int>(), p[1].get<int>()});
                }
            }
            *base = std::move(a);
            rep.lastWriter[id] = m.id;
        }

        inline void applyArea(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "name", "tileset", "collision", "map", "margins", "y_offset",
                                                       "music", "spawn_points", "max_events", "resume", "diggable"};
            if (rec.contains("mode") && rec["mode"].is_string() && rec["mode"].get<std::string>() == "patch")
                return applyAreaPatch(db, m, file, rec, rep);
            std::string id, why;
            if (!beginAdd(m, file, rec, rep, "area", allowed, [&](const std::string &i)
                          { return db.area(i) != nullptr; }, id))
                return;
            AreaDef a;
            a.id = id;
            a.legacyId = rep.nextAreaNumber++;
            std::string tsId, csId;
            if (!needStr(rec, "tileset", tsId, why) || !needStr(rec, "collision", csId, why) || !optStr(rec, "name", a.name, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            const Tileset *ts = db.tileset(tsId);
            const CollisionSet *cs = db.collision(csId);
            if (!ts)
                return err(rep, file, m.id, "'" + id + "': unknown tileset '" + tsId + "'");
            if (!cs)
                return err(rep, file, m.id, "'" + id + "': unknown collision '" + csId + "'");
            if (!a.name.empty() && !db.hasText(a.name))
                return err(rep, file, m.id, "'" + id + "': unknown name text '" + a.name + "'");
            a.tileset = tsId;
            a.collision = csId;
            if (!rec.contains("map") || !rec["map"].is_array() || rec["map"].empty() || rec["map"].size() > 255)
                return err(rep, file, m.id, "'" + id + "': 'map' must be 1 to 255 rows");
            a.tilesH = int(rec["map"].size());
            a.tilesW = rec["map"][0].is_array() ? int(rec["map"][0].size()) : 0;
            if (a.tilesW < 1 || a.tilesW > 255)
                return err(rep, file, m.id, "'" + id + "': every map row must be an array of 1 to 255 tile numbers");
            a.tiles.reserve(size_t(a.tilesW) * a.tilesH);
            for (int r = 0; r < a.tilesH; ++r)
            {
                const json &row = rec["map"][size_t(r)];
                if (!row.is_array() || int(row.size()) != a.tilesW)
                    return err(rep, file, m.id, "'" + id + "': map row " + std::to_string(r) + " has " + std::to_string(row.is_array() ? row.size() : 0) + " tiles, row 0 has " + std::to_string(a.tilesW));
                for (int c = 0; c < a.tilesW; ++c)
                {
                    const json &t = row[size_t(c)];
                    if (!t.is_number_integer() || t.get<int>() < 0 || t.get<int>() >= ts->count || t.get<int>() >= cs->count)
                        return err(rep, file, m.id, "'" + id + "': map row " + std::to_string(r) + ", column " + std::to_string(c) + " is not a tile number the tileset (" + std::to_string(ts->count) + " tiles) and collision (" + std::to_string(cs->count) + ") both have");
                    a.tiles.push_back(uint16_t(t.get<int>()));
                }
            }
            a.marginL = 42; // the base game's margins
            a.marginR = 42;
            a.marginT = 62;
            a.marginB = 20;
            if (rec.contains("margins"))
            {
                const json &mg = rec["margins"];
                if (!mg.is_array() || mg.size() != 4)
                    return err(rep, file, m.id, "'" + id + "': 'margins' must be [left, right, top, bottom]");
                int* dst[4] = {&a.marginL, &a.marginR, &a.marginT, &a.marginB};
                for (size_t i = 0; i < 4; ++i)
                {
                    if (!mg[i].is_number_integer() || mg[i].get<int>() < 0 || mg[i].get<int>() > 1000)
                        return err(rep, file, m.id, "'" + id + "': 'margins' values must be integers from 0 to 1000");
                    *dst[i] = mg[i].get<int>();
                }
            }
            if (!optInt(rec, "y_offset", a.collisionYOffset, -64, 64, why) || !optMusic(rec, a.music, why) ||
                !optInt(rec, "max_events", a.maxEvents, 0, 200, why) || !optBool(rec, "resume", a.resume, why) ||
                !optBool(rec, "diggable", a.diggable, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (rec.contains("spawn_points"))
            {
                if (!rec["spawn_points"].is_array() || rec["spawn_points"].size() > 500)
                    return err(rep, file, m.id, "'" + id + "': 'spawn_points' must be an array of at most 500 [x, y] pairs");
                for (const json &p : rec["spawn_points"])
                {
                    if (!p.is_array() || p.size() != 2 || !p[0].is_number_integer() || !p[1].is_number_integer())
                        return err(rep, file, m.id, "'" + id + "': every spawn point must be a pair like [100, 200]");
                    a.spawnPoints.push_back({p[0].get<int>(), p[1].get<int>()});
                }
            }
            db.put(std::move(a));
            rep.lastWriter[id] = m.id;
        }

        // object: a placed object in an area of this mod or of the base game. { area, type = an objtype id, x, y (the feet), reach =
        // [rx, ry] (makes it something the pet can use: stand within the reach box and press Z), sign = text id (Z shows it on the
        // sign picture), message = text id (Z shows it in a message box), scene = a scene id (Z opens that scene: Scene.hooks(id) in a script), quest_min, quest_max (present only while the completed-quests
        // counter is in that range) }. An exit record can use it as a door.
        // object, mode "patch" or "remove": change or take away an object that is already placed (the base game's or another
        // mod's). { area = the area it is in, id = the object's id } plus, for patch, any of x, y, reach, objtype, sign, message,
        // quest_min, quest_max; fields left out stay as they were. A moved object keeps its slot, so the parts of the game keyed to
        // it (quest gates, progress bits) still find it. A removed object is gone for good; exits and interactions that named it stay
        // but can no longer be triggered.
        inline void applyObjectEdit(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep, bool remove)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "area", "objtype", "x", "y", "reach", "sign", "message", "scene", "quest_min", "quest_max"};
            const std::string id = rec.contains("id") && rec["id"].is_string() ? rec["id"].get<std::string>() : std::string();
            const std::string verb = remove ? "remove" : "patch";
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in object record");
            if (id.empty())
                return err(rep, file, m.id, "record has no string 'id'");
            std::string areaId, typeId, sign, message, scene, why;
            if (!needStr(rec, "area", areaId, why) || !optStr(rec, "objtype", typeId, why) || !optStr(rec, "sign", sign, why) ||
                !optStr(rec, "message", message, why) || !optStr(rec, "scene", scene, why))
                return err(rep, file, m.id, verb + " '" + id + "': " + why);
            AreaDef *area = db.areaMutable(areaId);
            if (!area)
                return err(rep, file, m.id, verb + " '" + id + "': unknown area '" + areaId + "'");
            auto at = std::find_if(area->objects.begin(), area->objects.end(), [&](const ObjectDef &o)
                                   { return o.id == id; });
            if (at == area->objects.end())
                return err(rep, file, m.id, verb + " '" + id + "': area '" + areaId + "' has no such object");
            if (remove)
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (it.key() != "type" && it.key() != "mode" && it.key() != "id" && it.key() != "area")
                        return err(rep, file, m.id, "remove '" + id + "': a remove record takes only 'area' and 'id'");
                area->objects.erase(at);
                rep.lastWriter[id] = m.id;
                return;
            }

            ObjectDef o = *at;
            int rx = o.reachX, ry = o.reachY, x = o.x, y = o.y;
            if (!optInt(rec, "x", x, -100000, 100000, why) || !optInt(rec, "y", y, -100000, 100000, why) ||
                !optPair(rec, "reach", rx, ry, 1, 1000, why) || !optInt(rec, "quest_min", o.questMin, 0, 100000, why) ||
                !optInt(rec, "quest_max", o.questMax, 0, 0x7fffffff, why))
                return err(rep, file, m.id, "patch '" + id + "': " + why);
            if (int(!sign.empty()) + int(!message.empty()) + int(!scene.empty()) > 1)
                return err(rep, file, m.id, "patch '" + id + "': give one of 'sign', 'message' or 'scene'");
            if (!scene.empty() && !validIdChars(scene))
                return err(rep, file, m.id, "patch '" + id + "': 'scene' is a scene id (no space, ',', '=' or '|')");
            if ((!sign.empty() && !db.hasText(sign)) || (!message.empty() && !db.hasText(message)))
                return err(rep, file, m.id, "patch '" + id + "': unknown text '" + (!sign.empty() && !db.hasText(sign) ? sign : message) + "'");
            const ObjectType *ot = nullptr;
            if (!typeId.empty())
            {
                ot = db.objectType(typeId);
                if (!ot)
                    return err(rep, file, m.id, "patch '" + id + "': unknown objtype '" + typeId + "'");
                o.type = typeId;
            }
            // the box the art covers: a new type is measured from its art, a plain move shifts the old box along
            if (ot && ot->hasArt)
            {
                o.boxLeft = x - ot->art.anchorX + ot->art.dx;
                o.boxTop = y - ot->art.anchorY + ot->art.dy;
                o.boxRight = o.boxLeft + ot->art.w;
                o.boxBottom = o.boxTop + ot->art.h;
            }
            else
            {
                o.boxLeft += x - o.x;
                o.boxRight += x - o.x;
                o.boxTop += y - o.y;
                o.boxBottom += y - o.y;
            }
            o.x = x;
            o.y = y;
            if (rec.contains("reach") || !sign.empty() || !message.empty() || !scene.empty())
            {
                if (!rec.contains("reach") && !o.interactive())
                    rx = ry = 28;
                o.reachX = rx;
                o.reachY = ry;
                if (!o.interactive())
                    o.interactId = 0;
            }
            if (!sign.empty())
                o.contextText = sign;
            if (!message.empty())
            {
                InteractionDef d;
                d.id = id + "/interaction";
                d.area = areaId;
                d.object = id;
                d.action = "message";
                d.str["text"] = message;
                db.put(std::move(d));
            }
            if (!scene.empty())
            {
                InteractionDef d; // Z on the object opens a scene of a script (Scene.hooks(id)); checked once the scripts are loaded
                d.id = id + "/interaction";
                d.area = areaId;
                d.object = id;
                d.action = "scene";
                d.str["scene"] = scene;
                db.put(std::move(d));
            }
            *at = std::move(o);
            rep.lastWriter[id] = m.id;
        }

        inline void applyObject(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "area", "objtype", "x", "y", "reach", "sign", "message", "scene", "quest_min", "quest_max"};
            if (rec.contains("mode") && rec["mode"].is_string())
            {
                const std::string mode = rec["mode"].get<std::string>();
                if (mode == "patch" || mode == "remove")
                    return applyObjectEdit(db, m, file, rec, rep, mode == "remove");
                if (mode == "replace")
                    return err(rep, file, m.id, "object records support 'add', 'patch' and 'remove' (replace an object with patch)");
            }
            std::string id, why;
            std::string areaId, typeId, sign, message, scene;
            // ("type" is the record type, so the object's own type is called "objtype" here)
            if (!beginAdd(m, file, rec, rep, "object", allowed, [&](const std::string &)
                          { return false; }, id))
                return;
            if (!needStr(rec, "area", areaId, why) || !needStr(rec, "objtype", typeId, why) || !optStr(rec, "sign", sign, why) || !optStr(rec, "message", message, why) || !optStr(rec, "scene", scene, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            AreaDef *area = db.areaMutable(areaId);
            if (!area)
                return err(rep, file, m.id, "'" + id + "': unknown area '" + areaId + "'");
            for (const ObjectDef &o : area->objects)
                if (o.id == id)
                    return err(rep, file, m.id, "add '" + id + "': duplicate id");
            const ObjectType *ot = db.objectType(typeId);
            if (!ot)
                return err(rep, file, m.id, "'" + id + "': unknown objtype '" + typeId + "'");
            if ((!sign.empty() && !db.hasText(sign)) || (!message.empty() && !db.hasText(message)))
                return err(rep, file, m.id, "'" + id + "': unknown text '" + (!sign.empty() && !db.hasText(sign) ? sign : message) + "'");
            if (int(!sign.empty()) + int(!message.empty()) + int(!scene.empty()) > 1)
                return err(rep, file, m.id, "'" + id + "': give one of 'sign', 'message' or 'scene'");
            if (!scene.empty() && !validIdChars(scene))
                return err(rep, file, m.id, "'" + id + "': 'scene' is a scene id (no space, ',', '=' or '|')");
            ObjectDef o;
            o.id = id;
            o.type = typeId;
            int rx = 0, ry = 0;
            if (!rec.contains("x") || !rec.contains("y"))
                return err(rep, file, m.id, "'" + id + "': 'x' and 'y' are required");
            if (!optInt(rec, "x", o.x, -100000, 100000, why) || !optInt(rec, "y", o.y, -100000, 100000, why) ||
                !optPair(rec, "reach", rx, ry, 1, 1000, why) || !optInt(rec, "quest_min", o.questMin, 0, 100000, why) ||
                !optInt(rec, "quest_max", o.questMax, 0, 0x7fffffff, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            const bool usable = rec.contains("reach") || !sign.empty() || !message.empty() || !scene.empty();
            if (usable)
            {
                if (!rec.contains("reach"))
                    rx = ry = 28; // about the size of the reach of the base game's signs
                o.reachX = rx;
                o.reachY = ry;
                o.interactId = 0;
            }
            // The box the art covers, for the cull; a type without art uses the reach box.
            if (ot->hasArt)
            {
                o.boxLeft = o.x - ot->art.anchorX + ot->art.dx;
                o.boxTop = o.y - ot->art.anchorY + ot->art.dy;
                o.boxRight = o.boxLeft + ot->art.w;
                o.boxBottom = o.boxTop + ot->art.h;
            }
            else
            {
                o.boxLeft = o.x - std::max(rx, 16);
                o.boxRight = o.x + std::max(rx, 16);
                o.boxTop = o.y - std::max(ry, 16);
                o.boxBottom = o.y + std::max(ry, 16);
            }
            o.contextText = sign;
            if (!message.empty())
            {
                InteractionDef d;
                d.id = id + "/interaction";
                d.area = areaId;
                d.object = id;
                d.action = "message";
                d.str["text"] = message;
                db.put(std::move(d));
            }
            if (!scene.empty())
            {
                InteractionDef d; // Z on the object opens a scene of a script (Scene.hooks(id)); checked once the scripts are loaded
                d.id = id + "/interaction";
                d.area = areaId;
                d.object = id;
                d.action = "scene";
                d.str["scene"] = scene;
                db.put(std::move(d));
            }
            area->objects.push_back(std::move(o));
            rep.lastWriter[id] = m.id;
        }

        // exit: a door between areas. { from = area, object = an object id placed in that area (it needs a reach, see object),
        // to = area, spawn = [x, y] where the pet appears in the destination, on_touch (default true: walking into the reach box is
        // enough; false: the pet has to press Z) }. A way back is a second exit record.
        inline void applyExit(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "from", "object", "to", "spawn", "on_touch"};
            std::string id, why, from, objectId, to;
            if (!beginAdd(m, file, rec, rep, "exit", allowed, [&](const std::string &)
                          { return false; }, id))
                return;
            if (!needStr(rec, "from", from, why) || !needStr(rec, "object", objectId, why) || !needStr(rec, "to", to, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            const AreaDef *fa = db.area(from);
            if (!fa)
                return err(rep, file, m.id, "'" + id + "': unknown area '" + from + "'");
            if (!db.area(to))
                return err(rep, file, m.id, "'" + id + "': unknown area '" + to + "'");
            const ObjectDef *obj = nullptr;
            for (const ObjectDef &o : fa->objects)
                if (o.id == objectId)
                    obj = &o;
            if (!obj)
                return err(rep, file, m.id, "'" + id + "': area '" + from + "' has no object '" + objectId + "'");
            if (!obj->interactive())
                return err(rep, file, m.id, "'" + id + "': object '" + objectId + "' has no reach, so nothing can trigger it");
            int sx = 0, sy = 0;
            if (!rec.contains("spawn") || !optPair(rec, "spawn", sx, sy, -100000, 100000, why))
                return err(rep, file, m.id, "'" + id + "': " + (why.empty() ? "'spawn' is required: [x, y]" : why));
            bool onTouch = true;
            if (rec.contains("on_touch"))
            {
                if (!rec["on_touch"].is_boolean())
                    return err(rep, file, m.id, "'" + id + "': 'on_touch' must be true or false");
                onTouch = rec["on_touch"].get<bool>();
            }
            if (db.findExit(from, objectId, onTouch))
                return err(rep, file, m.id, "'" + id + "': that object already has an exit of this kind");
            ExitDef e;
            e.id = id;
            e.fromArea = from;
            e.object = objectId;
            e.toArea = to;
            e.onTouch = onTouch;
            e.hasSpawnOverride = true;
            e.spawnOverride = {sx, sy};
            db.put(std::move(e));
            TransitionDef t; // the game looks a transition up for every move between areas; the exit's spawn wins over its row
            t.from = from;
            t.to = to;
            t.spawn = {sx, sy};
            db.put(std::move(t));
            rep.lastWriter[id] = m.id;
        }

        // spot: a find placed on every entry to an area (the base game's hidden quest items and lost-and-found things are the same
        // records: "base:spot/<item number>"). { area, item, x, y, state = "buried" (default; dig it up) or "ground" (lying there),
        // requires_item (only while the pet holds it), not_while_held (not once the pet holds the item itself, which makes a find
        // "once"), not_offered (not once the item was returned to the King) }.
        // mode "remove" takes a spot away (the base game's too); fields are then just { id }.
        inline void applySpot(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "area", "item", "x", "y", "state", "requires_item",
                                                       "not_while_held", "not_offered"};
            if (rec.contains("mode") && rec["mode"].is_string() && rec["mode"].get<std::string>() == "remove")
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (it.key() != "type" && it.key() != "mode" && it.key() != "id")
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a spot remove record");
                if (!rec.contains("id") || !rec["id"].is_string())
                    return err(rep, file, m.id, "record has no string 'id'");
                const std::string id = rec["id"].get<std::string>();
                if (!db.spot(id))
                    return err(rep, file, m.id, "remove '" + id + "': no such spot");
                db.eraseSpot(id);
                rep.lastWriter[id] = m.id;
                return;
            }
            std::string id, why, areaId, itemId, state = "buried";
            if (!beginAdd(m, file, rec, rep, "spot", allowed, [&](const std::string &i)
                          { return db.spot(i) != nullptr; }, id))
                return;
            FixedSpotDef s;
            s.id = id;
            if (!needStr(rec, "area", areaId, why) || !needStr(rec, "item", itemId, why) || !optStr(rec, "state", state, why) ||
                !optStr(rec, "requires_item", s.prereqItem, why) || !optInt(rec, "x", s.x, -100000, 100000, why) ||
                !optInt(rec, "y", s.y, -100000, 100000, why) || !optBool(rec, "not_while_held", s.notWhileHeld, why) ||
                !optBool(rec, "not_offered", s.notOffered, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (!rec.contains("x") || !rec.contains("y"))
                return err(rep, file, m.id, "'" + id + "': 'x' and 'y' are required");
            const AreaDef *ad = db.area(areaId);
            if (!ad)
                return err(rep, file, m.id, "'" + id + "': unknown area '" + areaId + "'");
            if (!db.item(itemId))
                return err(rep, file, m.id, "'" + id + "': unknown item '" + itemId + "'");
            if (!s.prereqItem.empty() && !db.item(s.prereqItem))
                return err(rep, file, m.id, "'" + id + "': unknown item '" + s.prereqItem + "' in requires_item");
            if (state != "buried" && state != "ground")
                return err(rep, file, m.id, "'" + id + "': 'state' must be \"buried\" or \"ground\"");
            s.area = ad->legacyId;
            s.item = itemId;
            s.state = state == "buried" ? 1 : 2;
            db.put(std::move(s));
            rep.lastWriter[id] = m.id;
        }

        // find: one row of the table a random dig find is drawn from. { item, weight } - the item turns up with chance
        // weight / (all weights) of the finds that are not duds (tuning.findDudPercent decides how many are). The base game's rows are
        // "base:find/<item number>" (e.g. base:find/265 has weight 100). mode "patch": change weight and/or item of a row that exists;
        // mode "remove": take a row away. Only affects random finds in areas that have spawn_points, not the fixed spots.
        inline void applyFind(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "item", "weight"};
            const std::string mode = rec.contains("mode") && rec["mode"].is_string() ? rec["mode"].get<std::string>() : "";
            std::string id, why;
            if (mode == "patch" || mode == "remove")
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (!allowed.count(it.key()) || (mode == "remove" && it.key() != "type" && it.key() != "mode" && it.key() != "id"))
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a find " + mode + " record");
                if (!needStr(rec, "id", id, why))
                    return err(rep, file, m.id, "find record: " + why);
                FindWeightDef *f = db.findWeightMutable(id);
                if (!f)
                    return err(rep, file, m.id, mode + " '" + id + "': no such find row");
                if (mode == "remove")
                    db.eraseFindWeight(id);
                else
                {
                    FindWeightDef n = *f;
                    if (!rec.contains("item") && !rec.contains("weight"))
                        return err(rep, file, m.id, "patch '" + id + "': nothing to change (item, weight)");
                    if (!optStr(rec, "item", n.item, why) || !optInt(rec, "weight", n.weight, 1, 100000, why))
                        return err(rep, file, m.id, "patch '" + id + "': " + why);
                    if (!db.item(n.item))
                        return err(rep, file, m.id, "patch '" + id + "': unknown item '" + n.item + "'");
                    *f = std::move(n);
                }
                rep.lastWriter[id] = m.id;
                return;
            }
            if (!beginAdd(m, file, rec, rep, "find", allowed, [&](const std::string &i)
                          { return db.findWeight(i) != nullptr; }, id))
                return;
            FindWeightDef f;
            f.id = id;
            if (!needStr(rec, "item", f.item, why) || !optInt(rec, "weight", f.weight, 1, 100000, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (!rec.contains("weight"))
                return err(rep, file, m.id, "'" + id + "': 'weight' is required (1 to 100000)");
            if (!db.item(f.item))
                return err(rep, file, m.id, "'" + id + "': unknown item '" + f.item + "'");
            db.put(std::move(f));
            rep.lastWriter[id] = m.id;
        }

        // side_quest: a quest beside the story chain. Only mode "add".
        //   { type: "side_quest", mode: "add", id: "mymod:sidequest/pies", giver: "mymod:npc/baker", receiver: "mymod:npc/mayor" (default: the giver),
        //     objectives: [ { item: "mymod:item/pie", count: 3 } ] (0 to 4; none = just go and talk to the receiver),
        //     offer_text, complete_text: text ids (required), accept_text, remind_text: text ids (optional),
        //     reward_money: 0..99999, reward_items: [ { item, count } ] (up to 4), quest_min / quest_max: offered while quest_min <= quests done < quest_max,
        //     requires: a player flag that must be set first, repeatable: true = offered again after it is handed in }
        // The giver offers it when the pet talks to them (a yes / no question); the objectives show on the HUD; the receiver takes the items, says complete_text and pays.
        inline void applySideQuest(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "giver", "receiver", "objectives", "offer_text", "accept_text", "remind_text", "complete_text",
                                                       "reward_money", "reward_items", "quest_min", "quest_max", "requires", "repeatable"};
            std::string id, why;
            if (!beginAdd(m, file, rec, rep, "side_quest", allowed, [&](const std::string &i)
                          { return db.sideQuest(i) != nullptr; }, id))
                return;
            SideQuestDef q;
            q.id = id;
            auto npcField = [&](const char *key, Id &out, bool required) -> bool
            {
                if (!rec.contains(key))
                {
                    if (required)
                        return err(rep, file, m.id, "'" + id + "': '" + key + "' is required (an npc id)"), false;
                    return true;
                }
                if (!rec[key].is_string() || !db.hasNpc(rec[key].get<std::string>()))
                    return err(rep, file, m.id, "'" + id + "': '" + key + "' must be the id of an npc that exists"), false;
                out = rec[key].get<std::string>();
                return true;
            };
            auto textField = [&](const char *key, Id &out, bool required) -> bool
            {
                if (!rec.contains(key))
                {
                    if (required)
                        return err(rep, file, m.id, "'" + id + "': '" + key + "' is required (a text id)"), false;
                    return true;
                }
                if (!rec[key].is_string() || !db.hasText(rec[key].get<std::string>()))
                    return err(rep, file, m.id, "'" + id + "': '" + key + "' must be the id of a text that exists"), false;
                out = rec[key].get<std::string>();
                return true;
            };
            auto entries = [&](const char *key, size_t most, std::vector<QuestEntry> &out) -> bool
            {
                if (!rec.contains(key))
                    return true;
                if (!rec[key].is_array() || rec[key].size() > most)
                    return err(rep, file, m.id, "'" + id + "': '" + key + "' must be a list of up to " + std::to_string(most) + " like { \"item\": \"mymod:item/pie\", \"count\": 3 }"), false;
                for (const json &e : rec[key])
                {
                    if (!e.is_object() || !e.contains("item") || !e["item"].is_string() || !db.item(e["item"].get<std::string>()))
                        return err(rep, file, m.id, "'" + id + "': every entry in '" + key + "' needs an 'item' that exists"), false;
                    for (auto it = e.begin(); it != e.end(); ++it)
                        if (it.key() != "item" && it.key() != "count")
                            return err(rep, file, m.id, "'" + id + "': unknown field '" + it.key() + "' in '" + key + "'"), false;
                    QuestEntry q2;
                    q2.item = e["item"].get<std::string>();
                    q2.count = 1;
                    if (!optInt(e, "count", q2.count, 1, 99, why))
                        return err(rep, file, m.id, "'" + id + "': " + why), false;
                    out.push_back(std::move(q2));
                }
                return true;
            };
            if (!npcField("giver", q.giver, true) || !npcField("receiver", q.receiver, false) || !textField("offer_text", q.offerText, true) ||
                !textField("complete_text", q.completeText, true) || !textField("accept_text", q.acceptText, false) || !textField("remind_text", q.remindText, false) ||
                !entries("objectives", 4, q.objectives) || !entries("reward_items", 4, q.rewardItems))
                return;
            if (q.receiver.empty())
                q.receiver = q.giver;
            if (!optInt(rec, "reward_money", q.rewardMoney, 0, 99999, why) || !optInt(rec, "quest_min", q.questMin, 0, 100000, why) ||
                !optInt(rec, "quest_max", q.questMax, 0, 0x7fffffff, why) || !optBool(rec, "repeatable", q.repeatable, why) || !optStr(rec, "requires", q.requires_, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (q.questMax <= q.questMin)
                return err(rep, file, m.id, "'" + id + "': 'quest_max' must be greater than 'quest_min'");
            for (const char c : q.requires_)
                if (std::isspace(static_cast<unsigned char>(c)))
                    return err(rep, file, m.id, "'" + id + "': 'requires' is a flag name and cannot contain spaces");
            std::set<Id> seen;
            for (const QuestEntry &o : q.objectives)
                if (!seen.insert(o.item).second)
                    return err(rep, file, m.id, "'" + id + "': item '" + o.item + "' is listed twice in 'objectives'");
            db.put(std::move(q));
            rep.lastWriter[id] = m.id;
        }

        // shop: what a shop can stock, and where. The stock is rolled from this table (see ShopDef): `rolls` weighted picks when the game starts, then one more
        // every restock. { type: "shop", mode: "add", id: "mymod:shop/store", area: "mymod:area/store", rolls: 10,
        //   items: [ { item: "mymod:item/pie", weight: 20, cap: 5 } ] }   // weight: 0..10000 (default 10), cap: the most offered at once, 1..99 (default 5)
        // mode "patch" (or "replace": the same) changes a shop that is there, a base one too ("base:shop/10" is the area number): `rolls`, `items` (the whole
        // list), `add_items` (added, or changed if the item is already listed), `remove_items` (item ids). mode "remove": the shop sells nothing. The area is where the
        // stock stands (the area's spawn points) and cannot be changed by a patch.
        inline void applyShop(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "area", "rolls", "items", "add_items", "remove_items"};
            const std::string mode = rec.contains("mode") && rec["mode"].is_string() ? rec["mode"].get<std::string>() : "";
            std::string id, why;
            auto readItems = [&](const json &arr, const char *key, std::vector<ShopItem> &out) -> bool {
                if (!arr.is_array() || arr.size() > 200)
                    return why = std::string("'") + key + "' must be a list of up to 200 items like { \"item\": \"mymod:item/pie\", \"weight\": 20, \"cap\": 5 }", false;
                for (const json &e : arr)
                {
                    if (!e.is_object())
                        return why = std::string("each entry of '") + key + "' must be an object { item, weight, cap }", false;
                    for (auto it = e.begin(); it != e.end(); ++it)
                        if (it.key() != "item" && it.key() != "weight" && it.key() != "cap")
                            return why = "unknown field '" + it.key() + "' in a shop item (item, weight, cap)", false;
                    ShopItem si;
                    si.weight = 10;
                    if (!needStr(e, "item", si.item, why) || !optInt(e, "weight", si.weight, 0, 10000, why) || !optInt(e, "cap", si.cap, 1, 99, why))
                        return false;
                    if (!db.item(si.item))
                        return why = "unknown item '" + si.item + "'", false;
                    for (const ShopItem &o : out)
                        if (o.item == si.item)
                            return why = "item '" + si.item + "' is listed twice", false;
                    out.push_back(std::move(si));
                }
                return true;
            };
            if (mode == "patch" || mode == "replace" || mode == "remove")
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (!allowed.count(it.key()) || (mode == "remove" && it.key() != "type" && it.key() != "mode" && it.key() != "id"))
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a shop " + mode + " record");
                if (!needStr(rec, "id", id, why))
                    return err(rep, file, m.id, "shop record: " + why);
                ShopDef *s = db.shopMutable(id);
                if (!s)
                    return err(rep, file, m.id, mode + " '" + id + "': no such shop");
                if (mode == "remove")
                {
                    db.eraseShop(id);
                    rep.lastWriter[id] = m.id;
                    return;
                }
                if (rec.contains("area"))
                    return err(rep, file, m.id, mode + " '" + id + "': the shop's area cannot be changed");
                ShopDef n = *s;
                if (!optInt(rec, "rolls", n.rolls, 0, 100, why))
                    return err(rep, file, m.id, mode + " '" + id + "': " + why);
                if (rec.contains("items"))
                {
                    n.items.clear();
                    if (!readItems(rec["items"], "items", n.items))
                        return err(rep, file, m.id, mode + " '" + id + "': " + why);
                }
                if (rec.contains("remove_items"))
                {
                    if (!rec["remove_items"].is_array())
                        return err(rep, file, m.id, mode + " '" + id + "': 'remove_items' must be a list of item ids");
                    for (const json &r : rec["remove_items"])
                    {
                        if (!r.is_string())
                            return err(rep, file, m.id, mode + " '" + id + "': 'remove_items' must be a list of item ids");
                        bool found = false;
                        for (size_t i = 0; i < n.items.size(); ++i)
                            if (n.items[i].item == r.get<std::string>())
                            {
                                n.items.erase(n.items.begin() + std::ptrdiff_t(i));
                                found = true;
                                break;
                            }
                        if (!found)
                            return err(rep, file, m.id, mode + " '" + id + "': '" + r.get<std::string>() + "' is not in the shop to remove");
                    }
                }
                if (rec.contains("add_items"))
                {
                    std::vector<ShopItem> add;
                    if (!readItems(rec["add_items"], "add_items", add))
                        return err(rep, file, m.id, mode + " '" + id + "': " + why);
                    for (ShopItem &a : add)
                    {
                        bool done = false;
                        for (ShopItem &o : n.items)
                            if (o.item == a.item)
                            {
                                o = a; // already listed: its weight and cap change
                                done = true;
                            }
                        if (!done)
                            n.items.push_back(std::move(a));
                    }
                }
                if (!rec.contains("rolls") && !rec.contains("items") && !rec.contains("add_items") && !rec.contains("remove_items"))
                    return err(rep, file, m.id, mode + " '" + id + "': nothing to change (rolls, items, add_items, remove_items)");
                *s = std::move(n);
                rep.lastWriter[id] = m.id;
                return;
            }
            if (!beginAdd(m, file, rec, rep, "shop", allowed, [&](const std::string &i) { return db.shop(i) != nullptr; }, id))
                return;
            if (rec.contains("add_items") || rec.contains("remove_items"))
                return err(rep, file, m.id, "'" + id + "': add_items / remove_items only go with a patch (an add lists its 'items')");
            ShopDef s;
            s.id = id;
            if (!needStr(rec, "area", s.area, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            const AreaDef *ad = db.area(s.area);
            if (!ad)
                return err(rep, file, m.id, "'" + id + "': unknown area '" + s.area + "'");
            if (!optInt(rec, "rolls", s.rolls, 0, 100, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (!rec.contains("items"))
                return err(rep, file, m.id, "'" + id + "': 'items' is required");
            if (!readItems(rec["items"], "items", s.items))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (db.shopForArea(s.area))
                return err(rep, file, m.id, "'" + id + "': area '" + s.area + "' already has a shop (" + db.shopForArea(s.area)->id + "); patch that one instead");
            if (ad->spawnPoints.empty())
                rep.warnings.push_back("[" + m.id + "] " + id + ": area '" + s.area + "' has no spawn_points, so the stock has nowhere to stand");
            db.put(std::move(s));
            rep.lastWriter[id] = m.id;
        }

        // shop_stock: something that puts an item into a shop's stock when something happens (see StockRuleDef; the base game's own are "base:stock/...").
        // { type: "shop_stock", mode: "add", id: "mymod:stock/x", shop: "base:shop/12", on: "area", area: "mymod:area/cave", item: "mymod:item/gem", unless_held: true }
        // on: "area" (the pet walks into `area`), "quest" (quest number `quest` is handed in; `modes` default [5]), "type" (a quest of one of `modes`), "illness".
        // from: "item" (the fixed `item`, the default), "first_objective", "each_objective" (only_item / except_item narrow it), "first_spawn", "cure" (on "illness").
        // mode "patch" changes any of these fields, mode "remove" takes the rule away.
        inline void applyShopStock(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "shop", "on", "area", "quest", "modes", "from", "item", "only_item", "except_item", "unless_held", "unless_offered"};
            static const std::set<std::string> ons{"area", "quest", "type", "illness"};
            static const std::set<std::string> froms{"item", "first_objective", "each_objective", "first_spawn", "cure"};
            const std::string mode = rec.contains("mode") && rec["mode"].is_string() ? rec["mode"].get<std::string>() : "";
            std::string id, why;
            StockRuleDef r;
            bool patch = false;
            if (mode == "patch" || mode == "remove")
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (!allowed.count(it.key()) || (mode == "remove" && it.key() != "type" && it.key() != "mode" && it.key() != "id"))
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a shop_stock " + mode + " record");
                if (!needStr(rec, "id", id, why))
                    return err(rep, file, m.id, "shop_stock record: " + why);
                StockRuleDef *old = db.stockRuleMutable(id);
                if (!old)
                    return err(rep, file, m.id, mode + " '" + id + "': no such shop_stock rule");
                if (mode == "remove")
                {
                    db.eraseStockRule(id);
                    rep.lastWriter[id] = m.id;
                    return;
                }
                r = *old;
                patch = true;
            }
            else if (!beginAdd(m, file, rec, rep, "shop_stock", allowed, [&](const std::string &i)
                               { return db.stockRuleMutable(i) != nullptr; }, id))
                return;
            else
                r.id = id;
            if (rec.contains("shop") && !optStr(rec, "shop", r.shop, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (rec.contains("on") && !optStr(rec, "on", r.on, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (rec.contains("from") && !optStr(rec, "from", r.from, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (!patch && (!rec.contains("shop") || !rec.contains("on")))
                return err(rep, file, m.id, "'" + id + "': 'shop' and 'on' are required");
            if (!db.shop(r.shop))
                return err(rep, file, m.id, "'" + id + "': unknown shop '" + r.shop + "'");
            if (!ons.count(r.on))
                return err(rep, file, m.id, "'" + id + "': 'on' must be \"area\", \"quest\", \"type\" or \"illness\"");
            if (!froms.count(r.from))
                return err(rep, file, m.id, "'" + id + "': 'from' must be \"item\", \"first_objective\", \"each_objective\", \"first_spawn\" or \"cure\"");
            for (const char *k : {"area", "item", "only_item", "except_item"})
                if (rec.contains(k))
                {
                    Id &dst = std::string(k) == "area" ? r.area : std::string(k) == "item" ? r.item : std::string(k) == "only_item" ? r.onlyItem : r.exceptItem;
                    if (!optStr(rec, k, dst, why))
                        return err(rep, file, m.id, "'" + id + "': " + why);
                }
            if (rec.contains("quest") && !optInt(rec, "quest", r.quest, 1, 1000, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            for (const char *k : {"unless_held", "unless_offered"})
                if (rec.contains(k))
                {
                    if (!rec[k].is_boolean())
                        return err(rep, file, m.id, "'" + id + "': '" + k + "' must be true or false");
                    (std::string(k) == "unless_held" ? r.unlessHeld : r.unlessOffered) = rec[k].get<bool>();
                }
            if (rec.contains("modes"))
            {
                if (!rec["modes"].is_array() || rec["modes"].size() > 16)
                    return err(rep, file, m.id, "'" + id + "': 'modes' must be a list of quest type modes like [4, 6]");
                r.modes.clear();
                for (const json &e : rec["modes"])
                {
                    if (!e.is_number_integer() || e.get<int>() < 0 || e.get<int>() > 100)
                        return err(rep, file, m.id, "'" + id + "': 'modes' must be a list of quest type modes like [4, 6]");
                    r.modes.push_back(e.get<int>());
                }
            }
            if (r.on == "area" && !db.area(r.area))
                return err(rep, file, m.id, "'" + id + "': an 'area' rule needs an existing 'area' (it says '" + r.area + "')");
            if (r.on == "quest" && r.quest < 1)
                return err(rep, file, m.id, "'" + id + "': a 'quest' rule needs 'quest' (the number handed in)");
            if (r.on == "type" && r.modes.empty())
                return err(rep, file, m.id, "'" + id + "': a 'type' rule needs 'modes'");
            if (r.on == "illness" && r.from != "cure" && r.from != "item")
                return err(rep, file, m.id, "'" + id + "': an 'illness' rule puts in the 'cure' or a fixed 'item'");
            if (r.on == "area" && r.from != "item")
                return err(rep, file, m.id, "'" + id + "': an 'area' rule puts in a fixed 'item'");
            if (r.from == "item" && !db.item(r.item))
                return err(rep, file, m.id, "'" + id + "': unknown item '" + r.item + "'");
            if (r.from == "cure" && r.on != "illness")
                return err(rep, file, m.id, "'" + id + "': 'cure' only goes with 'on': \"illness\"");
            for (const Id *i : {&r.onlyItem, &r.exceptItem})
                if (!i->empty() && !db.item(*i))
                    return err(rep, file, m.id, "'" + id + "': unknown item '" + *i + "'");
            if ((!r.onlyItem.empty() || !r.exceptItem.empty()) && r.from != "each_objective")
                return err(rep, file, m.id, "'" + id + "': only_item / except_item go with 'from': \"each_objective\"");
            if ((r.unlessHeld || r.unlessOffered) && r.on != "area")
                return err(rep, file, m.id, "'" + id + "': unless_held / unless_offered go with 'on': \"area\"");
            if (patch)
                db.stockRuleMutable(id) ? (void)(*db.stockRuleMutable(id) = std::move(r)) : (void)0;
            else
                db.put(std::move(r));
            rep.lastWriter[id] = m.id;
        }

        // gate: a barrier object that goes for good once the pet meets a condition (the base game's five guards in Magma Mountain are
        // "base:gate/5/25", "/15", "/7", "/1" and "/22"). { barrier = an object id, kind = "tricks" (tricks known >= need) | "stat"
        // (any of stats - "halos", "pitchforks", "stars", "lightbulbs" - >= need) | "money" (>= need) | "quests" (quests done >= need,
        // the game counts from 1), need }. Checked every step while the pet is in the barrier's area; once met it stays met (a saved
        // flag named like the gate), so a mod's gate is "kind" + "need" on an object it also placed. The guard's words are an ordinary
        // object `message`. mode "patch": change kind / stats / need; mode "remove": the barrier stays for good (fields are just { id }).
        inline bool findObjectById(ContentDB &db, const std::string &objId, AreaDef *&area, ObjectDef *&obj)
        {
            for (const auto &[aid, ad] : db.allAreas())
                for (const ObjectDef &o : ad.objects)
                    if (o.id == objId)
                    {
                        area = db.areaMutable(aid);
                        for (ObjectDef &m : area->objects)
                            if (m.id == objId)
                                obj = &m;
                        return obj != nullptr;
                    }
            return false;
        }
        inline void applyGate(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "barrier", "kind", "stats", "need"};
            static const std::map<std::string, int> statNames{{"halos", 0}, {"pitchforks", 1}, {"stars", 2}, {"lightbulbs", 3}};
            const std::string mode = rec.contains("mode") && rec["mode"].is_string() ? rec["mode"].get<std::string>() : "";
            std::string id, why, kindName, barrierId;
            // Reads kind / stats / need over `g`; used by add and patch.
            auto fill = [&](GateDef &g, bool adding) -> bool
            {
                std::string kind = g.kind == GateKind::Tricks ? "tricks" : g.kind == GateKind::Money ? "money" : g.kind == GateKind::Quests ? "quests" : "stat";
                if (adding && (!rec.contains("kind") || !rec.contains("need")))
                    return why = "'kind' and 'need' are required", false;
                if (!optStr(rec, "kind", kind, why) || !optInt(rec, "need", g.need, 0, 1000000, why))
                    return false;
                if (kind != "tricks" && kind != "stat" && kind != "money" && kind != "quests")
                    return why = "'kind' must be \"tricks\", \"stat\", \"money\" or \"quests\"", false;
                g.kind = kind == "tricks" ? GateKind::Tricks : kind == "money" ? GateKind::Money : kind == "quests" ? GateKind::Quests : GateKind::Personality;
                if (rec.contains("stats"))
                {
                    if (g.kind != GateKind::Personality)
                        return why = "'stats' only goes with kind \"stat\"", false;
                    if (!rec["stats"].is_array() || rec["stats"].empty() || rec["stats"].size() > 4)
                        return why = "'stats' must be a list of 1 to 4 of halos, pitchforks, stars, lightbulbs", false;
                    g.stats.clear();
                    for (const json &n : rec["stats"])
                    {
                        const auto it = n.is_string() ? statNames.find(n.get<std::string>()) : statNames.end();
                        if (it == statNames.end())
                            return why = "'stats' entries must be halos, pitchforks, stars or lightbulbs", false;
                        if (std::find(g.stats.begin(), g.stats.end(), it->second) == g.stats.end())
                            g.stats.push_back(it->second);
                    }
                }
                if (g.kind == GateKind::Personality && g.stats.empty())
                    return why = "kind \"stat\" needs 'stats'", false;
                if (g.kind != GateKind::Personality)
                    g.stats.clear();
                return true;
            };
            if (mode == "patch" || mode == "remove")
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (!allowed.count(it.key()) || it.key() == "barrier" || (mode == "remove" && it.key() != "type" && it.key() != "mode" && it.key() != "id"))
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a gate " + mode + " record");
                if (!needStr(rec, "id", id, why))
                    return err(rep, file, m.id, "gate record: " + why);
                GateDef *g = db.gateMutable(id);
                if (!g)
                    return err(rep, file, m.id, mode + " '" + id + "': no such gate");
                if (mode == "remove")
                {
                    AreaDef *ad = nullptr;
                    ObjectDef *ob = nullptr;
                    if (findObjectById(db, g->barrier, ad, ob))
                        ob->goneOnFlag.clear();
                    db.eraseGate(id);
                }
                else
                {
                    if (rec.size() <= 3)
                        return err(rep, file, m.id, "patch '" + id + "': nothing to change (kind, stats, need)");
                    GateDef n = *g;
                    if (!fill(n, false))
                        return err(rep, file, m.id, "patch '" + id + "': " + why);
                    *g = std::move(n);
                }
                rep.lastWriter[id] = m.id;
                return;
            }
            if (!beginAdd(m, file, rec, rep, "gate", allowed, [&](const std::string &i)
                          { return db.gate(i) != nullptr; }, id))
                return;
            GateDef g;
            g.id = id;
            if (!needStr(rec, "barrier", barrierId, why) || !fill(g, true))
                return err(rep, file, m.id, "'" + id + "': " + why);
            AreaDef *ad = nullptr;
            ObjectDef *ob = nullptr;
            if (!findObjectById(db, barrierId, ad, ob))
                return err(rep, file, m.id, "'" + id + "': unknown object '" + barrierId + "' in 'barrier'");
            if (!ob->goneOnFlag.empty())
                return err(rep, file, m.id, "'" + id + "': object '" + barrierId + "' is already the barrier of another gate");
            ob->goneOnFlag = id;
            g.barrier = barrierId;
            g.area = ad->legacyId;
            db.put(std::move(g));
            rep.lastWriter[id] = m.id;
        }

        // quest_spawn: something placed when a quest is turned in. { quest = quest number (15 = the 15th story quest), area, item, x, y,
        // state = "buried" (default; dig it up) or "ground" }. Instead of x / y, "random": true (and "times": n, default 1) puts n copies
        // on free ones of the area's random dig spots (spawn_points; an area without any gets none). It stays in the world until picked up, wherever the pet goes. The base
        // game's four are "base:quest_spawn/15", "/32", "/90", "/100". mode "patch": change any of the fields of a row that exists;
        // mode "remove": take a row away (fields are then just { id }).
        inline void applyQuestSpawn(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "quest", "area", "item", "x", "y", "state", "random", "times"};
            const std::string mode = rec.contains("mode") && rec["mode"].is_string() ? rec["mode"].get<std::string>() : "";
            std::string id, why, areaId, state;
            // Reads the fields present in `rec` over `n`; the area and item are checked against the db.
            auto fill = [&](QuestSpawnDef &n, bool adding) -> bool
            {
                std::string itemId = n.item;
                if (!optBool(rec, "random", n.random, why) || !optInt(rec, "times", n.times, 1, 50, why))
                    return false;
                if (adding && (!rec.contains("quest") || !rec.contains("area") || !rec.contains("item")))
                    return why = "'quest', 'area' and 'item' are required", false;
                if (adding && !n.random && (!rec.contains("x") || !rec.contains("y")))
                    return why = "'x' and 'y' are required (or \"random\": true)", false;
                if (n.random && (rec.contains("x") || rec.contains("y")))
                    return why = "a random spawn has no 'x' / 'y'", false;
                if (rec.contains("times") && !n.random)
                    return why = "'times' only applies with \"random\": true", false;
                if (!optInt(rec, "quest", n.count, 1, 100000, why) || !optStr(rec, "area", areaId, why) || !optStr(rec, "item", itemId, why) ||
                    !optInt(rec, "x", n.x, -100000, 100000, why) || !optInt(rec, "y", n.y, -100000, 100000, why) ||
                    !optStr(rec, "state", state, why))
                    return false;
                if (!areaId.empty())
                {
                    const AreaDef *ad = db.area(areaId);
                    if (!ad)
                        return why = "unknown area '" + areaId + "'", false;
                    n.area = ad->legacyId;
                }
                if (!db.item(itemId))
                    return why = "unknown item '" + itemId + "'", false;
                n.item = itemId;
                if (!state.empty())
                {
                    if (state != "buried" && state != "ground")
                        return why = "'state' must be \"buried\" or \"ground\"", false;
                    n.state = state == "buried" ? 1 : 2;
                }
                // "buried" is the invisible quest-item kind (1) at a fixed place, but the dig marker (0) on a random spot, as the ROM's
                // random rewards use it
                if (n.random && n.state == 1)
                    n.state = 0;
                else if (!n.random && n.state == 0)
                    n.state = 1;
                return true;
            };
            if (mode == "patch" || mode == "remove")
            {
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (!allowed.count(it.key()) || (mode == "remove" && it.key() != "type" && it.key() != "mode" && it.key() != "id"))
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a quest_spawn " + mode + " record");
                if (!needStr(rec, "id", id, why))
                    return err(rep, file, m.id, "quest_spawn record: " + why);
                QuestSpawnDef *q = db.questSpawnMutable(id);
                if (!q)
                    return err(rep, file, m.id, mode + " '" + id + "': no such quest_spawn");
                if (mode == "remove")
                    db.eraseQuestSpawn(id);
                else
                {
                    if (rec.size() <= 3) // type, mode, id
                        return err(rep, file, m.id, "patch '" + id + "': nothing to change");
                    QuestSpawnDef n = *q;
                    if (!fill(n, false))
                        return err(rep, file, m.id, "patch '" + id + "': " + why);
                    *q = std::move(n);
                }
                rep.lastWriter[id] = m.id;
                return;
            }
            if (!beginAdd(m, file, rec, rep, "quest_spawn", allowed, [&](const std::string &i)
                          { return db.questSpawn(i) != nullptr; }, id))
                return;
            QuestSpawnDef n;
            n.id = id;
            if (!fill(n, true))
                return err(rep, file, m.id, "'" + id + "': " + why);
            db.put(std::move(n));
            rep.lastWriter[id] = m.id;
        }

        // minigame, mode "patch": change a minigame's numbers and lists. { id = "base:minigame/prize_pools", num = { key: number },
        // lists = { key: [..] } (replaces the list), list_add = { key: [..] } (appends), list_remove = { key: [..] } (removes every
        // copy) }. A list entry is a number or an item id ("base:item/114" becomes its item number). A repeat in a pool is a heavier
        // weight. The keys are the ones the record already has (a typo is an error, and the message lists them): see baseMinigames()
        // in game/minigame_defs.hpp. The prize pools are "base:minigame/prize_pools": first, general (Memory Match, Swift Pick's random
        // game, the wells), prizeA..prizeD and special (the 100 Bucks well); "base:minigame/friend_pools": gifts (item numbers), advice and
        // defaultLines (text ids, strings only); "base:minigame/colour_machine": presets, stepTicks. A list cannot be left empty.
        inline bool readNumberList(const ContentDB &db, const json &arr, const std::string &where, std::vector<int> &out, std::string &why)
        {
            if (!arr.is_array() || arr.size() > 500)
            {
                why = "'" + where + "' must be an array of at most 500 numbers or item ids";
                return false;
            }
            for (const json &e : arr)
            {
                if (e.is_number_integer() && e.get<long long>() >= 0 && e.get<long long>() <= 100000)
                    out.push_back(e.get<int>());
                else if (e.is_string())
                {
                    const ItemDef *d = db.item(e.get<std::string>());
                    if (!d)
                    {
                        why = "'" + where + "': unknown item '" + e.get<std::string>() + "'";
                        return false;
                    }
                    out.push_back(d->index);
                }
                else
                {
                    why = "'" + where + "': every entry must be a whole number (0 or more) or an item id";
                    return false;
                }
            }
            return true;
        }
        // The entries of a list of colours: a number (an offset from the species' base colour, as the ROM's own presets) or the id of a
        // palette record.
        inline bool readPaletteList(const ContentDB &db, const json &arr, const std::string &where, std::vector<int> &out, std::string &why)
        {
            if (!arr.is_array() || arr.size() > 500)
            {
                why = "'" + where + "' must be an array of at most 500 colours";
                return false;
            }
            for (const json &e : arr)
            {
                if (e.is_number_integer() && e.get<long long>() >= 0 && e.get<long long>() <= 1023)
                    out.push_back(e.get<int>());
                else if (e.is_string())
                {
                    const PaletteDef *p = db.palette(e.get<std::string>());
                    if (!p)
                    {
                        why = "'" + where + "': unknown palette '" + e.get<std::string>() + "'";
                        return false;
                    }
                    out.push_back(p->number);
                }
                else
                {
                    why = "'" + where + "': every entry must be a palette id or an offset (0 to 1023)";
                    return false;
                }
            }
            return true;
        }
        inline void applyMinigame(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "num", "lists", "list_add", "list_remove"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in minigame record");
            if (!rec.contains("id") || !rec["id"].is_string())
                return err(rep, file, m.id, "record has no string 'id'");
            const std::string id = rec["id"].get<std::string>();
            if (!rec.contains("mode") || !rec["mode"].is_string() || rec["mode"].get<std::string>() != "patch")
                return err(rep, file, m.id, "'" + id + "': minigame records only support mode 'patch'");
            MinigameDef *base = db.minigameMutable(id);
            if (!base)
                return err(rep, file, m.id, "patch '" + id + "': no such minigame");
            if (!rec.contains("num") && !rec.contains("lists") && !rec.contains("list_add") && !rec.contains("list_remove"))
                return err(rep, file, m.id, "patch '" + id + "': nothing to change (num, lists, list_add, list_remove)");
            MinigameDef d = *base;
            auto keysOf = [](const auto &map, const auto &more)
            {
                std::string s;
                for (const auto &kv : map)
                    s += (s.empty() ? "" : ", ") + kv.first;
                for (const auto &kv : more)
                    s += (s.empty() ? "" : ", ") + kv.first;
                return s;
            };
            if (rec.contains("num"))
            {
                if (!rec["num"].is_object())
                    return err(rep, file, m.id, "patch '" + id + "': 'num' must be an object like {\"cost\": 30}");
                for (auto it = rec["num"].begin(); it != rec["num"].end(); ++it)
                {
                    if (!d.num.count(it.key()))
                        return err(rep, file, m.id, "patch '" + id + "': no number '" + it.key() + "' (it has: " + keysOf(d.num, std::map<int, int>()) + ")");
                    if (!it.value().is_number_integer() || it.value().get<long long>() < -1000000 || it.value().get<long long>() > 1000000)
                        return err(rep, file, m.id, "patch '" + id + "': '" + it.key() + "' must be a whole number from -1000000 to 1000000");
                    d.num[it.key()] = it.value().get<int>();
                }
            }
            for (const char *field : {"lists", "list_add", "list_remove"})
            {
                if (!rec.contains(field))
                    continue;
                if (!rec[field].is_object())
                    return err(rep, file, m.id, std::string("patch '") + id + "': '" + field + "' must be an object like {\"first\": [1, 2]}");
                for (auto it = rec[field].begin(); it != rec[field].end(); ++it)
                {
                    auto cur = d.lists.find(it.key());
                    auto curIds = d.idLists.find(it.key());
                    if (cur == d.lists.end() && curIds == d.idLists.end())
                        return err(rep, file, m.id, "patch '" + id + "': no list '" + it.key() + "' (it has: " + keysOf(d.lists, d.idLists) + ")");
                    std::string why;
                    if (curIds != d.idLists.end()) // a list of text ids
                    {
                        std::vector<std::string> ids;
                        if (!it.value().is_array() || it.value().size() > 500)
                            return err(rep, file, m.id, "patch '" + id + "': '" + std::string(field) + "." + it.key() + "' must be an array of at most 500 text ids");
                        for (const json &e : it.value())
                        {
                            if (!e.is_string() || !db.hasText(e.get<std::string>()))
                                return err(rep, file, m.id, "patch '" + id + "': '" + std::string(field) + "." + it.key() + "' has " + (e.is_string() ? "an unknown text id '" + e.get<std::string>() + "'" : std::string("an entry that is not a text id")));
                            ids.push_back(e.get<std::string>());
                        }
                        if (std::string(field) == "lists")
                            curIds->second = std::move(ids);
                        else if (std::string(field) == "list_add")
                            curIds->second.insert(curIds->second.end(), ids.begin(), ids.end());
                        else
                            for (const std::string &v : ids)
                                curIds->second.erase(std::remove(curIds->second.begin(), curIds->second.end(), v), curIds->second.end());
                        continue;
                    }
                    std::vector<int> vals;
                    const bool read = d.paletteLists.count(it.key()) ? readPaletteList(db, it.value(), std::string(field) + "." + it.key(), vals, why)
                                                                     : readNumberList(db, it.value(), std::string(field) + "." + it.key(), vals, why);
                    if (!read)
                        return err(rep, file, m.id, "patch '" + id + "': " + why);
                    if (d.itemLists.count(it.key())) // item numbers: the game looks each one up
                        for (int v : vals)
                            if (std::string(field) != "list_remove" && v >= int(db.items().size()))
                                return err(rep, file, m.id, "patch '" + id + "': list '" + it.key() + "' has " + std::to_string(v) + ", which is not an item number");
                    if (std::string(field) == "lists")
                        cur->second = std::move(vals);
                    else if (std::string(field) == "list_add")
                        cur->second.insert(cur->second.end(), vals.begin(), vals.end());
                    else
                        for (int v : vals)
                            cur->second.erase(std::remove(cur->second.begin(), cur->second.end(), v), cur->second.end());
                }
            }
            for (const auto &kv : d.lists)
            {
                const bool wasFilled = !base->list(kv.first).empty();
                if (wasFilled && kv.second.empty())
                    return err(rep, file, m.id, "patch '" + id + "': list '" + kv.first + "' cannot be left empty");
            }
            for (const auto &kv : d.idLists)
                if (!base->idList(kv.first).empty() && kv.second.empty())
                    return err(rep, file, m.id, "patch '" + id + "': list '" + kv.first + "' cannot be left empty");
            *base = std::move(d);
            rep.lastWriter[id] = m.id;
        }

        // ---- sound and music records ----
        //   { type: "sound", mode: "add", id: "mymod:sound/meow", file: "audio/meow.ogg", volume: 100, pitch: 0 }
        //   { type: "music", mode: "add", id: "mymod:music/theme", file: "audio/theme.ogg", loop_start: 4.0, loop_end: 60.5 }
        //   { type: "sound", mode: "replace", id: "base:sound/84", file: ... }   // plays the file where the game asks for the ROM's sound 84
        // file: .wav or .ogg inside the mod's folder (music: or a .mid / .midi file, played with the ROM's instruments, with `programs` {midiProgram: romInstrument 0..13}
        // and `drums` {midiDrumNote: romDrum 0..11} (programs and drum notes are General MIDI unless `numbers` is "rom") to say which of its instruments and drums are which; `channels` {"1": 3} gives a channel (1 to 16) its instrument
        // (or name the editor's channel "Lead [3]"; `--export-instruments` writes what each number sounds like); `slide_channels` [3] lists the channels
        // (1 to 16) where two notes starting and ending together are an FL Studio slide, see engine/midi.hpp). volume: percent of the file's own level (0..400). pitch: semitones, + higher / faster.
        // loop_start / loop_end (music only): seconds of the file; the song plays up to loop_end, then repeats from loop_start (0 = the whole song).
        inline bool modFile(const ModManifest &m, const std::string &rel, std::string &out, std::string &why, bool midiOk = false)
        {
            const fs::path r = rel;
            if (r.is_absolute())
                return why = "the file must be a path relative to the mod folder", false;
            for (const auto &part : r)
                if (part == "..")
                    return why = "the file may not contain '..'", false;
            const fs::path p = m.dir / r;
            if (!fs::exists(p))
                return why = "missing file " + p.string(), false;
            if (!midiOk && !checkSoundFile(p.string(), why))
                return why = p.string() + ": " + why, false;
            out = p.generic_string();
            return true;
        }
        // ---- instrument and drum records (the sounds a MIDI song is played with, see engine/midi.hpp) ----
        //   { type: "instrument", mode: "add", id: "mymod:instrument/harp", file: "audio/harp.wav", root: 60, loop_start: 0.4, loop_end: 1.1,
        //     attack: 0.005, decay: 0.2, sustain: 0.6, release: 0.1, volume: 100 }
        //   { type: "instrument", mode: "replace", id: "base:instrument/3", zones: [ {file: "low.wav", root: 48, top: 59, loop_start: 0.3}, {file: "high.wav", root: 72} ] }
        //   { type: "drum", mode: "add", id: "mymod:drum/clap", file: "audio/clap.wav", volume: 100, pitch: 0 }
        //   { type: "drum", mode: "replace", id: "base:drum/5", file: ... }
        // An instrument is one sample (file, root, ...) or `zones`: several, each for the notes up to its `top` (highest note, default 127). root: the MIDI note the
        // sample sounds at (default 60 = middle C); the note played speeds it up or slows it down from there. With loop_start / loop_end (seconds of the file; end
        // defaults to the file's end) a held note repeats that stretch until it ends, with none the sample plays once. attack, decay and release are seconds,
        // sustain a level from 0 to 1. A drum is played from its start whatever the note (pitch only shifts it); it has no loop. The ROM's instruments are 0 to 13
        // and its drums 0 to 11 (replace them with "base:instrument/3" / "base:drum/5"); a mod's own come after, in the order they load: 14, 15... and 12, 13...
        // Use these numbers in a music record's programs / drums / channels, or "Lead [i14]" in the editor's channel names.
        inline void applyInstrument(const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> fields{"type", "mode", "id", "file", "volume", "root", "top", "loop_start", "loop_end", "zones", "attack", "decay", "sustain", "release"};
            static const std::set<std::string> zoneFields{"file", "volume", "root", "top", "loop_start", "loop_end"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!fields.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in instrument record");
            if (!rec.contains("id") || !rec["id"].is_string() || !rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, "instrument record needs a string 'id' and 'mode'");
            const std::string id = rec["id"].get<std::string>(), mode = rec["mode"].get<std::string>();
            if (mode != "add" && mode != "replace")
                return err(rep, file, m.id, "'" + id + "': instrument records support mode 'add' and 'replace'");
            AudioRegistry &reg = audioRegistry();
            int program = -1;
            size_t existing = reg.instruments.size();
            const std::string prefix = "base:instrument/";
            if (id.rfind(prefix, 0) == 0)
            {
                char *end = nullptr;
                const long n = std::strtol(id.c_str() + prefix.size(), &end, 10);
                if (mode != "replace" || id.size() == prefix.size() || *end || n < 0 || n >= kRomInstrumentCount)
                    return err(rep, file, m.id, "'" + id + "': the ROM's instruments are replaced with mode 'replace' and an id like 'base:instrument/3' (0 to 13)");
                program = int(n);
            }
            if (program >= 0 || mode == "replace" || mode == "add")
                for (size_t i = 0; i < reg.instruments.size(); ++i)
                    if (reg.instruments[i].id == id)
                        existing = i;
            if (program < 0)
            {
                if (mode == "add")
                {
                    if (id.rfind(m.id + ":", 0) != 0)
                        return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                    if (!validIdChars(id))
                        return err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                    if (existing < reg.instruments.size())
                        return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace)");
                }
                else if (existing >= reg.instruments.size())
                    return err(rep, file, m.id, "replace '" + id + "': no such instrument");
            }
            ModInstrument ins;
            if (existing < reg.instruments.size())
                ins = reg.instruments[existing]; // another mod's replacement of it: unlisted fields stay
            ins.id = id;
            if (program >= 0)
                ins.program = program;
            else if (existing >= reg.instruments.size())
            {
                int own = 0;
                for (const ModInstrument &o : reg.instruments)
                    own += o.program >= kRomInstrumentCount ? 1 : 0;
                ins.program = kRomInstrumentCount + own;
            }
            std::string why;
            // one zone from a record's fields (the instrument's own, or one of `zones`)
            auto readZone = [&](const json &z, ModZone &out, bool needFile) -> bool {
                if (z.contains("file"))
                {
                    if (!z["file"].is_string())
                        return why = "'file' must be a path", false;
                    if (!modFile(m, z["file"].get<std::string>(), out.path, why))
                        return false;
                }
                else if (needFile && out.path.empty())
                    return why = "'file' is required", false;
                int percent = int(out.gain * 100 + 0.5f);
                if (!optInt(z, "volume", percent, 0, 400, why) || !optInt(z, "root", out.root, 0, 127, why) || !optInt(z, "top", out.top, 0, 127, why))
                    return false;
                out.gain = float(percent) / 100.f;
                for (const char *k : {"loop_start", "loop_end"})
                    if (z.contains(k))
                    {
                        if (!z[k].is_number() || z[k].get<double>() < 0 || z[k].get<double>() > 3600)
                            return why = std::string("'") + k + "' must be seconds of the file", false;
                        out.loop = true;
                        (std::string(k) == "loop_start" ? out.loopStart : out.loopEnd) = z[k].get<double>();
                    }
                if (out.loopEnd > 0 && out.loopStart >= out.loopEnd)
                    return why = "'loop_start' must come before 'loop_end'", false;
                return true;
            };
            if (rec.contains("zones"))
            {
                if (rec.contains("file") || rec.contains("root") || rec.contains("top") || rec.contains("loop_start") || rec.contains("loop_end") || rec.contains("volume"))
                    return err(rep, file, m.id, "'" + id + "': give either the sample's fields or 'zones', not both");
                if (!rec["zones"].is_array() || rec["zones"].empty() || rec["zones"].size() > 32)
                    return err(rep, file, m.id, "'" + id + "': 'zones' must be a list of one to 32 samples");
                ins.zones.clear();
                for (const json &z : rec["zones"])
                {
                    if (!z.is_object())
                        return err(rep, file, m.id, "'" + id + "': each zone must be an object like {\"file\": \"low.wav\", \"root\": 48, \"top\": 59}");
                    for (auto it = z.begin(); it != z.end(); ++it)
                        if (!zoneFields.count(it.key()))
                            return err(rep, file, m.id, "'" + id + "': unknown field '" + it.key() + "' in a zone");
                    ModZone mz;
                    if (!readZone(z, mz, true))
                        return err(rep, file, m.id, "'" + id + "': " + why);
                    ins.zones.push_back(mz);
                }
            }
            else if (rec.contains("file") || rec.contains("root") || rec.contains("top") || rec.contains("loop_start") || rec.contains("loop_end") || rec.contains("volume") || ins.zones.empty())
            {
                ModZone mz = ins.zones.size() == 1 ? ins.zones[0] : ModZone{};
                if (!readZone(rec, mz, true))
                    return err(rep, file, m.id, "'" + id + "': " + why);
                ins.zones.assign(1, mz);
            }
            for (const char *k : {"attack", "decay", "release"})
                if (rec.contains(k))
                {
                    if (!rec[k].is_number() || rec[k].get<double>() < 0 || rec[k].get<double>() > 30)
                        return err(rep, file, m.id, "'" + id + "': '" + k + "' must be seconds, 0 to 30");
                    (std::string(k) == "attack" ? ins.attack : std::string(k) == "decay" ? ins.decay : ins.release) = rec[k].get<double>();
                }
            if (rec.contains("sustain"))
            {
                if (!rec["sustain"].is_number() || rec["sustain"].get<double>() < 0 || rec["sustain"].get<double>() > 1)
                    return err(rep, file, m.id, "'" + id + "': 'sustain' must be a level from 0 to 1");
                ins.sustain = rec["sustain"].get<double>();
            }
            if (existing < reg.instruments.size())
                reg.instruments[existing] = ins;
            else
                reg.instruments.push_back(ins);
            ++reg.generation;
            rep.lastWriter[id] = m.id;
            if (ins.program >= kRomInstrumentCount)
                rep.warnings.push_back("[" + m.id + "] " + id + " is instrument " + std::to_string(ins.program));
        }

        inline void applyDrum(const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> fields{"type", "mode", "id", "file", "volume", "pitch"};
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!fields.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in drum record");
            if (!rec.contains("id") || !rec["id"].is_string() || !rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, "drum record needs a string 'id' and 'mode'");
            const std::string id = rec["id"].get<std::string>(), mode = rec["mode"].get<std::string>();
            if (mode != "add" && mode != "replace")
                return err(rep, file, m.id, "'" + id + "': drum records support mode 'add' and 'replace'");
            AudioRegistry &reg = audioRegistry();
            int index = -1;
            size_t existing = reg.drums.size();
            const std::string prefix = "base:drum/";
            if (id.rfind(prefix, 0) == 0)
            {
                char *end = nullptr;
                const long n = std::strtol(id.c_str() + prefix.size(), &end, 10);
                if (mode != "replace" || id.size() == prefix.size() || *end || n < 0 || n >= kRomDrumCount)
                    return err(rep, file, m.id, "'" + id + "': the ROM's drums are replaced with mode 'replace' and an id like 'base:drum/5' (0 to 11)");
                index = int(n);
            }
            for (size_t i = 0; i < reg.drums.size(); ++i)
                if (reg.drums[i].id == id)
                    existing = i;
            if (index < 0)
            {
                if (mode == "add")
                {
                    if (id.rfind(m.id + ":", 0) != 0)
                        return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                    if (!validIdChars(id))
                        return err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                    if (existing < reg.drums.size())
                        return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace)");
                }
                else if (existing >= reg.drums.size())
                    return err(rep, file, m.id, "replace '" + id + "': no such drum");
            }
            ModDrum d;
            if (existing < reg.drums.size())
                d = reg.drums[existing];
            d.id = id;
            if (index >= 0)
                d.index = index;
            else if (existing >= reg.drums.size())
            {
                int own = 0;
                for (const ModDrum &o : reg.drums)
                    own += o.index >= kRomDrumCount ? 1 : 0;
                d.index = kRomDrumCount + own;
            }
            std::string why;
            if (rec.contains("file"))
            {
                if (!rec["file"].is_string() || !modFile(m, rec["file"].get<std::string>(), d.path, why))
                    return err(rep, file, m.id, "'" + id + "': " + (rec["file"].is_string() ? why : std::string("'file' must be a path")));
            }
            else if (d.path.empty())
                return err(rep, file, m.id, "'" + id + "': 'file' is required");
            int percent = int(d.gain * 100 + 0.5f);
            if (!optInt(rec, "volume", percent, 0, 400, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            d.gain = float(percent) / 100.f;
            if (rec.contains("pitch"))
            {
                if (!rec["pitch"].is_number() || rec["pitch"].get<double>() < -48 || rec["pitch"].get<double>() > 48)
                    return err(rep, file, m.id, "'" + id + "': 'pitch' must be semitones, -48 to 48");
                d.semitones = rec["pitch"].get<double>();
            }
            if (existing < reg.drums.size())
                reg.drums[existing] = d;
            else
                reg.drums.push_back(d);
            ++reg.generation;
            rep.lastWriter[id] = m.id;
            if (d.index >= kRomDrumCount)
                rep.warnings.push_back("[" + m.id + "] " + id + " is drum " + std::to_string(d.index));
        }

        inline void applyAudio(ContentDB &, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep, bool music)
        {
            const char *kind = music ? "music" : "sound";
            static const std::set<std::string> soundFields{"type", "mode", "id", "file", "volume", "pitch"};
            static const std::set<std::string> musicFields{"type", "mode", "id", "file", "volume", "pitch", "loop_start", "loop_end", "programs", "drums", "channels", "slide_channels", "numbers"};
            const auto &allowed = music ? musicFields : soundFields;
            for (auto it = rec.begin(); it != rec.end(); ++it)
                if (!allowed.count(it.key()))
                    return err(rep, file, m.id, "unknown field '" + it.key() + "' in " + kind + " record");
            if (!rec.contains("id") || !rec["id"].is_string() || !rec.contains("mode") || !rec["mode"].is_string())
                return err(rep, file, m.id, std::string(kind) + " record needs a string 'id' and 'mode'");
            const std::string id = rec["id"].get<std::string>(), mode = rec["mode"].get<std::string>();
            if (mode != "add" && mode != "replace")
                return err(rep, file, m.id, "'" + id + "': " + kind + " records support mode 'add' and 'replace'");
            AudioRegistry &reg = audioRegistry();
            auto &mine = music ? reg.songs : reg.sounds;
            const std::string prefix = std::string("base:") + kind + "/";
            int romNumber = -1;
            size_t existing = mine.size();
            if (id.rfind(prefix, 0) == 0)
            {
                const long n = music ? romSongNumber(id) : romSoundNumber(id); // a number or a name: "base:sound/84", "base:sound/kaboing"
                if (mode != "replace" || n < 0)
                    return err(rep, file, m.id, "'" + id + "': the ROM's " + kind + "s are replaced with mode 'replace' and an id like '" + prefix + (music ? "ufo" : "kaboing") + "' or '" + prefix + "84' (0 to " +
                                                    std::to_string((music ? kRomSongs : kRomSounds) - 1) + ")");
                romNumber = int(n);
            }
            else
            {
                for (size_t i = 0; i < mine.size(); ++i)
                    if (mine[i].id == id)
                        existing = i;
                if (mode == "add")
                {
                    if (id.rfind(m.id + ":", 0) != 0)
                        return err(rep, file, m.id, "add '" + id + "': new ids must use the mod's own prefix '" + m.id + ":'");
                    if (!validIdChars(id))
                        return err(rep, file, m.id, "add '" + id + "': id contains a space, ',', '=' or '|'");
                    if (existing < mine.size())
                        return err(rep, file, m.id, "add '" + id + "': duplicate id (use mode replace)");
                }
                else if (existing >= mine.size())
                    return err(rep, file, m.id, "replace '" + id + "': no such " + kind);
            }
            // A ROM song with no file but programs / drums / channels: its notes stay, its instruments change.
            const bool patch = music && romNumber >= 0 && !rec.contains("file") && (rec.contains("programs") || rec.contains("drums") || rec.contains("channels"));
            if (patch)
                for (const char *k : {"volume", "pitch", "loop_start", "loop_end", "slide_channels", "numbers"})
                    if (rec.contains(k))
                        return err(rep, file, m.id, "'" + id + "': '" + k + "' needs a 'file' (without one the record only changes the ROM song's instruments: programs, drums, channels)");
            ModSound s;
            if (romNumber < 0 && mode == "replace")
                s = mine[existing]; // a mod's own sound that another mod replaces: unlisted fields stay
            else if (patch && reg.songPatches.count(romNumber))
                s = reg.songPatches[romNumber]; // another mod's changes to the same song: unlisted ones stay
            s.id = id;
            std::string rel, why;
            if (rec.contains("file"))
            {
                if (!rec["file"].is_string())
                    return err(rep, file, m.id, "'" + id + "': 'file' must be a path");
                const std::string ext = lowerExt(rec["file"].get<std::string>());
                s.midi = music && (ext == ".mid" || ext == ".midi");
                if (!modFile(m, rec["file"].get<std::string>(), s.path, why, s.midi))
                    return err(rep, file, m.id, "'" + id + "': " + why);
                if (!s.midi)
                {
                    s.programs.clear();
                    s.drums.clear();
                    s.programIds.clear();
                    s.drumIds.clear();
                    s.channelIds.clear();
                    s.slideChannels.clear();
                    s.channels.clear();
                    s.gmNumbers = true;
                }
            }
            else if (patch)
                s.midi = true; // (read like a MIDI record's maps, but the numbers are the ROM's own)
            else if (s.path.empty())
                return err(rep, file, m.id, "'" + id + "': 'file' is required");
            for (const char *k : {"programs", "drums"})
                if (rec.contains(k))
                {
                    if (!s.midi)
                        return err(rep, file, m.id, "'" + id + "': '" + k + "' only goes with a MIDI file");
                    if (!rec[k].is_object())
                        return err(rep, file, m.id, "'" + id + "': '" + k + "' must be an object like {\"40\": 9}");
                    const bool prog = std::string(k) == "programs";
                    auto &dst = prog ? s.programs : s.drums;
                    auto &dstIds = prog ? s.programIds : s.drumIds;
                    dst.clear();
                    dstIds.clear();
                    for (auto it = rec[k].begin(); it != rec[k].end(); ++it)
                    {
                        char *end = nullptr;
                        const long from = std::strtol(it.key().c_str(), &end, 10);
                        if (it.key().empty() || *end || from < 0 || from > 127 || !(it.value().is_number_integer() || it.value().is_string()))
                            return err(rep, file, m.id, "'" + id + "': '" + k + "' maps a MIDI number (0 to 127) to a number or an id: " + it.key());
                        if (it.value().is_string())
                        {
                            dstIds[int(from)] = it.value().get<std::string>();
                            continue;
                        }
                        const int to = it.value().get<int>(), top = 63; // (the ROM's are 0..13 and 0..11, a mod's instruments and drums come after)
                        if (to < 0 || to > top)
                            return err(rep, file, m.id, "'" + id + "': '" + k + "' " + it.key() + " must go to 0.." + std::to_string(top) + " (the ROM's " + (prog ? "instruments" : "drums") + ")");
                        dst[int(from)] = to;
                    }
                }
            if (rec.contains("numbers"))
            {
                if (!s.midi)
                    return err(rep, file, m.id, "'" + id + "': 'numbers' only goes with a MIDI file");
                const std::string kind = rec["numbers"].is_string() ? rec["numbers"].get<std::string>() : "";
                if (kind != "gm" && kind != "rom")
                    return err(rep, file, m.id, "'" + id + "': 'numbers' must be \"gm\" (programs and drum notes as General MIDI, the default) or \"rom\" (the ROM's own)");
                s.gmNumbers = kind == "gm";
            }
            if (rec.contains("channels"))
            {
                if (!s.midi)
                    return err(rep, file, m.id, "'" + id + "': 'channels' only goes with a MIDI file");
                if (!rec["channels"].is_object())
                    return err(rep, file, m.id, "'" + id + "': 'channels' must be an object like {\"1\": 3} (the channel as your editor shows it, 1 to 16, and the instrument: a number, 0 to 13 are the ROM's, or an id like \"mymod:instrument/harp\")");
                s.channels.clear();
                s.channelIds.clear();
                for (auto it = rec["channels"].begin(); it != rec["channels"].end(); ++it)
                {
                    char *end = nullptr;
                    const long c = std::strtol(it.key().c_str(), &end, 10);
                    if (it.key().empty() || *end || c < 1 || c > 16 || !(it.value().is_string() || (it.value().is_number_integer() && it.value().get<int>() >= 0 && it.value().get<int>() <= 63)))
                        return err(rep, file, m.id, "'" + id + "': 'channels' must be an object like {\"1\": 3} (the channel as your editor shows it, 1 to 16, and the instrument: a number, 0 to 13 are the ROM's, or an id like \"mymod:instrument/harp\"): " + it.key());
                    if (it.value().is_string())
                        s.channelIds[int(c) - 1] = it.value().get<std::string>();
                    else
                        s.channels[int(c) - 1] = it.value().get<int>();
                }
            }
            if (rec.contains("slide_channels"))
            {
                if (!s.midi)
                    return err(rep, file, m.id, "'" + id + "': 'slide_channels' only goes with a MIDI file");
                if (!rec["slide_channels"].is_array() || rec["slide_channels"].size() > 16)
                    return err(rep, file, m.id, "'" + id + "': 'slide_channels' must be a list of channel numbers like [3] (1 to 16, as your editor shows them)");
                s.slideChannels.clear();
                for (const json &c : rec["slide_channels"])
                {
                    if (!c.is_number_integer() || c.get<int>() < 1 || c.get<int>() > 16)
                        return err(rep, file, m.id, "'" + id + "': 'slide_channels' must be a list of channel numbers like [3] (1 to 16, as your editor shows them)");
                    s.slideChannels.push_back(c.get<int>() - 1);
                }
            }
            if (s.midi && !patch) // read it now, so a bad file or a note it cannot play is reported when the mod loads
            {
                midi::Options mo;
                mo.programs = s.programs;
                mo.drums = s.drums;
                mo.slideChannels = s.slideChannels;
                mo.channels = s.channels;
                mo.gmPrograms = mo.gmDrums = s.gmNumbers;
                mo.programCount = mo.drumCount = 64; // (a mod's instruments and drums load in their own order: not checked here)
                std::vector<uint8_t> bytes;
                midi::Result mr;
                if (!readFileBytes(s.path, bytes) || !midi::toSong(bytes, mo, mr, why))
                    return err(rep, file, m.id, "'" + id + "': " + s.path + ": " + why);
                for (const std::string &w : mr.warnings)
                    rep.warnings.push_back("[" + m.id + "] " + s.path + ": " + w);
            }
            int percent = int(s.gain * 100 + 0.5f);
            if (!optInt(rec, "volume", percent, 0, 400, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            s.gain = float(percent) / 100.f;
            if (rec.contains("pitch"))
            {
                if (!rec["pitch"].is_number() || rec["pitch"].get<double>() < -48 || rec["pitch"].get<double>() > 48)
                    return err(rep, file, m.id, "'" + id + "': 'pitch' must be semitones, -48 to 48");
                s.semitones = rec["pitch"].get<double>();
            }
            for (const char *k : {"loop_start", "loop_end"})
                if (rec.contains(k))
                {
                    if (!rec[k].is_number() || rec[k].get<double>() < 0 || rec[k].get<double>() > 36000)
                        return err(rep, file, m.id, "'" + id + "': '" + k + "' must be seconds of the file");
                    (std::string(k) == "loop_start" ? s.loopStart : s.loopEnd) = rec[k].get<double>();
                }
            if (s.loopEnd > 0 && s.loopStart >= s.loopEnd)
                return err(rep, file, m.id, "'" + id + "': 'loop_start' must come before 'loop_end'");
            if (patch)
            {
                s.midi = false;
                s.gmNumbers = false;
                reg.songPatches[romNumber] = s;
                ++reg.generation;
            }
            else if (romNumber >= 0)
                (music ? reg.songOver : reg.soundOver)[romNumber] = s;
            else if (existing < mine.size())
                mine[existing] = s;
            else
                mine.push_back(s);
            rep.lastWriter[id] = m.id;
        }

        // ---- species: a pet species of the mod's own (pass 3: it names anims, texts and items) ----
        //   { type: "species", mode: "add", id: "mymod:species/fox",
        //     based_on: "base:species/0",          // optional: start as a copy of another species (art and start values)
        //     default_palette, own_illness, start: { health, hunger, happiness, cleanliness, tiredness }, money: [lo, hi],
        //     starter_items: [ { item, count } ],
        //     art: { walkUp: "mymod:anim/fox_up" | 123, ... } }   // every field of SpeciesArt by its name, see npc_anim_ids.hpp
        // An animation field takes a ROM animation number or the id of a mod's anim record. Without based_on the art starts empty:
        // walkUp, walkDown, walkSide and idleBase are required, the rest are optional (a role with no animation is skipped).
        struct ArtField
        {
            const char *name;
            int anim;    // 0 a plain number, 1 may be given as a mod anim id, 2 as a mod sound id
            int n;       // 1 = a number, more = an array of exactly n
            std::function<int *(SpeciesArt &, int)> at;
        };
        inline const std::vector<ArtField> &artFields()
        {
            static std::vector<ArtField> v;
            if (!v.empty())
                return v;
#define GP_ROLE(f) v.push_back({#f, true, 1, [](SpeciesArt &a, int) { return &a.f; }})
#define GP_STYLE(f) v.push_back({#f, false, 1, [](SpeciesArt &a, int) { return &a.f; }})
#define GP_ARR(f, anim, N) v.push_back({#f, anim, N, [](SpeciesArt &a, int i) { return &a.f[i]; }})
            GP_ROLE(walkUp); GP_ROLE(walkDown); GP_ROLE(walkSide); GP_ROLE(sitDown); GP_ROLE(sitAlt); GP_ROLE(eat); GP_ROLE(bowlBite);
            GP_ROLE(bedGetIn); GP_ROLE(sleepCycle); GP_ROLE(bedGetOut); GP_ROLE(idleBase); GP_ROLE(idleSide); GP_ROLE(turn180); GP_ROLE(turn90);
            GP_ROLE(settled); GP_ROLE(idleFlagA); GP_ROLE(idleFlagB); GP_ROLE(callFront); GP_ROLE(callSide);
            GP_ROLE(posSide); GP_ROLE(posDown); GP_ROLE(emoSide); GP_ROLE(emoDown); GP_ROLE(shkSide); GP_ROLE(shkDown); GP_ROLE(neg1); GP_ROLE(neg2);
            GP_ROLE(negIO); GP_ROLE(sad); GP_ROLE(bored); GP_ROLE(reactIdleSide); GP_ROLE(reactIdleDown); GP_ROLE(introSide); GP_ROLE(introDown);
            GP_ROLE(loadPicture); GP_ROLE(trickTurnAround); GP_ROLE(trickDirectional); GP_ROLE(trickActorReaction); GP_ROLE(dig); GP_ROLE(hopIntro);
            GP_STYLE(walkSideStyle); GP_STYLE(eatStyle); GP_STYLE(actionStyle); GP_STYLE(boredStyle); GP_STYLE(positiveStyle); GP_STYLE(idleStyle);
            GP_STYLE(turn90Style); GP_STYLE(turn180Style); GP_STYLE(settledStyle); GP_STYLE(idleFlagAStyle); GP_STYLE(idleFlagBStyle); GP_STYLE(callStyle);
            GP_STYLE(bowlX); GP_STYLE(bowlY); GP_STYLE(bowlSlope); GP_STYLE(digDirt); GP_STYLE(idleFlagBRate); GP_STYLE(settledRate); GP_STYLE(sleepFps);
            GP_STYLE(tiredDecay); GP_STYLE(hungerDecay); GP_STYLE(thoughtDY); GP_STYLE(overheadDY); GP_STYLE(bubbleLower); GP_STYLE(bubbleRaise);
            GP_STYLE(colorBase); GP_STYLE(menuPortrait); GP_STYLE(npcPortraitTile); GP_STYLE(loadTile); GP_STYLE(loadSpot); GP_STYLE(loadX); GP_STYLE(loadY);
            GP_ARR(trickMain, true, 3); GP_ARR(trickIntro, true, 3); GP_ARR(trickClosing, true, 3);
            GP_ARR(voice, 2, kPetVoiceCategories); GP_ARR(thoughtDX, false, 3); GP_ARR(trickRepeatLo, false, 3); GP_ARR(trickRepeatHi, false, 3);
            GP_ARR(trickIcons, false, 3); GP_ARR(potions, false, 5); GP_ARR(namePortrait, false, 3); GP_ARR(npcPortraitPal, false, 3);
#undef GP_ROLE
#undef GP_STYLE
#undef GP_ARR
            return v;
        }

        // One number of a species' art: an integer, or (where `anim`) the id of a mod's anim record.
        inline bool artNumber(const ContentDB &db, const json &v, int kind, int &out, std::string &why)
        {
            if (v.is_number_integer())
            {
                const long long x = v.get<long long>();
                if (x < -1000000 || x > 10000000)
                    return why = "a number out of range", false;
                out = int(x);
                return true;
            }
            if (kind == 1 && v.is_string())
            {
                const std::string s = v.get<std::string>();
                if (!db.hasAnim(s))
                    return why = "no anim '" + s + "'", false;
                out = modAnimNumber(s);
                return true;
            }
            if (kind == 2 && v.is_string())
            {
                const std::string s = v.get<std::string>();
                const int n = soundNumberOf(s); // "base:sound/kaboing" or a mod's sound
                if (n < 0)
                    return why = "no sound '" + s + "'", false;
                out = n;
                return true;
            }
            why = kind == 1 ? "must be a number or the id of an anim" : kind == 2 ? "must be a sound number or the id of a sound" : "must be a number";
            return false;
        }
        inline bool artIntList(const ContentDB &db, const json &v, int kind, std::vector<int> &out, std::string &why)
        {
            if (!v.is_array() || v.size() > 64)
                return why = "must be a list of numbers", false;
            std::vector<int> r;
            for (const json &e : v)
            {
                int x = 0;
                if (!artNumber(db, e, kind, x, why))
                    return false;
                r.push_back(x);
            }
            out = r;
            return true;
        }
        inline bool readTrickRules(const ContentDB &db, const json &o, TrickRules &t, std::string &why)
        {
            if (!o.is_object())
                return why = "each of 'trickRules' must be an object", false;
            struct F { const char *k; int *p; int anim; };
            const F ints[] = {{"teacherRepeats", &t.teacherRepeats, false}, {"bulbScale", &t.bulbScale, false}, {"startSound", &t.startSound, 2},
                              {"startSoundVolume", &t.startSoundVolume, false}, {"soundFrame", &t.soundFrame, false}, {"frameSound", &t.frameSound, 2},
                              {"frameSoundVolume", &t.frameSoundVolume, false}, {"loopSoundAnim", &t.loopSoundAnim, true}, {"loopSound", &t.loopSound, 2},
                              {"loopSoundVolume", &t.loopSoundVolume, false}, {"chainAnim", &t.chainAnim, true}, {"chainWaitAnim", &t.chainWaitAnim, true},
                              {"chainFinalAnim", &t.chainFinalAnim, true}, {"chainFinalSound", &t.chainFinalSound, 2}, {"chainFinalVolume", &t.chainFinalVolume, false}};
            const struct { const char *k; bool *p; } bools[] = {{"cameraPan", &t.cameraPan}, {"closingReversed", &t.closingReversed},
                                                                {"turnAroundAfter", &t.turnAroundAfter}, {"actorReactionExit", &t.actorReactionExit}};
            for (auto it = o.begin(); it != o.end(); ++it)
            {
                const std::string k = it.key();
                bool done = false;
                for (const F &f : ints)
                    if (k == f.k)
                    {
                        if (!artNumber(db, it.value(), f.anim, *f.p, why))
                            return why = "'" + k + "' " + why, false;
                        done = true;
                    }
                for (const auto &b : bools)
                    if (k == b.k)
                    {
                        if (!it.value().is_boolean())
                            return why = "'" + k + "' must be true or false", false;
                        *b.p = it.value().get<bool>();
                        done = true;
                    }
                if (done)
                    continue;
                if (k == "startSoundDelay")
                {
                    if (!it.value().is_number() || it.value().get<double>() < 0 || it.value().get<double>() > 60)
                        return why = "'startSoundDelay' must be seconds, 0 to 60", false;
                    t.startSoundDelay = it.value().get<double>();
                }
                else if (k == "stopOnClose" || k == "chainSounds")
                {
                    if (!artIntList(db, it.value(), 2, k == "stopOnClose" ? t.stopOnClose : t.chainSounds, why))
                        return why = "'" + k + "' " + why, false;
                }
                else if (k == "chainReactions")
                {
                    if (!it.value().is_array() || it.value().size() > 8)
                        return why = "'chainReactions' must be a list of { roll, anim, sound, volume }", false;
                    std::vector<TrickReaction> rs;
                    for (const json &e : it.value())
                    {
                        if (!e.is_object())
                            return why = "'chainReactions' must be a list of objects", false;
                        TrickReaction r;
                        for (auto q = e.begin(); q != e.end(); ++q)
                        {
                            int *dst = q.key() == "roll" ? &r.roll : q.key() == "anim" ? &r.anim : q.key() == "sound" ? &r.sound : q.key() == "volume" ? &r.volume : nullptr;
                            if (!dst)
                                return why = "unknown field '" + q.key() + "' in a chain reaction", false;
                            if (!artNumber(db, q.value(), q.key() == "anim" ? 1 : q.key() == "sound" ? 2 : 0, *dst, why))
                                return why = "'" + q.key() + "' " + why, false;
                        }
                        rs.push_back(r);
                    }
                    t.chainReactions = rs;
                }
                else
                    return why = "unknown field '" + k + "' in trickRules", false;
            }
            return true;
        }
        inline bool readSpeciesArt(const ContentDB &db, const json &o, SpeciesArt &a, std::string &why)
        {
            if (!o.is_object())
                return why = "'art' must be an object", false;
            for (auto it = o.begin(); it != o.end(); ++it)
            {
                const std::string k = it.key();
                const json &v = it.value();
                bool found = false;
                for (const ArtField &f : artFields())
                {
                    if (k != f.name)
                        continue;
                    found = true;
                    if (f.n == 1)
                    {
                        int x = 0;
                        if (!artNumber(db, v, f.anim, x, why))
                            return why = "'" + k + "' " + why, false;
                        *f.at(a, 0) = x;
                    }
                    else
                    {
                        if (!v.is_array() || int(v.size()) != f.n)
                            return why = "'" + k + "' must be a list of exactly " + std::to_string(f.n), false;
                        for (int i = 0; i < f.n; ++i)
                        {
                            int x = 0;
                            if (!artNumber(db, v[size_t(i)], f.anim, x, why))
                                return why = "'" + k + "' " + why, false;
                            *f.at(a, i) = x;
                        }
                    }
                    break;
                }
                if (found)
                    continue;
                if (k == "facesRight" || k == "bowlStartsWithAction" || k == "wideShadow")
                {
                    if (!v.is_boolean())
                        return why = "'" + k + "' must be true or false", false;
                    (k == "facesRight" ? a.facesRight : k == "wideShadow" ? a.wideShadow : a.bowlStartsWithAction) = v.get<bool>();
                }
                else if (k == "raisedAnims")
                {
                    if (!artIntList(db, v, true, a.raisedAnims, why))
                        return why = "'raisedAnims' " + why, false;
                }
                else if (k == "trickNames")
                {
                    if (!v.is_array() || v.size() != 3)
                        return why = "'trickNames' must be three text ids", false;
                    for (int i = 0; i < 3; ++i)
                    {
                        if (!v[size_t(i)].is_string() || !db.hasText(v[size_t(i)].get<std::string>()))
                            return why = "'trickNames' entry " + std::to_string(i + 1) + " is not the id of a text", false;
                        a.trickNames[i] = v[size_t(i)].get<std::string>();
                    }
                }
                else if (k == "trickRules")
                {
                    if (!v.is_array() || v.size() > 3)
                        return why = "'trickRules' must be a list of up to three objects (levels 1 to 3)", false;
                    for (size_t i = 0; i < v.size(); ++i)
                        if (!readTrickRules(db, v[i], a.trickRules[i], why))
                            return why = "trickRules " + std::to_string(i + 1) + ": " + why, false;
                }
                else
                    return why = "unknown art field '" + k + "'", false;
            }
            return true;
        }

        // A palette for a species' colours: a number (a ROM palette) or the id of a mod's palette.
        inline bool colourList(const ContentDB &db, const json &v, std::vector<int> &out, std::string &why)
        {
            if (!v.is_array() || v.size() > 64)
                return why = "must be a list of palette numbers or palette ids", false;
            std::vector<int> r;
            for (const json &e : v)
            {
                if (e.is_number_integer() && e.get<int>() >= 0 && e.get<int>() < kModPaletteBase)
                    r.push_back(e.get<int>());
                else if (e.is_string() && db.palette(e.get<std::string>()))
                    r.push_back(db.palette(e.get<std::string>())->number);
                else
                    return why = "must be a list of ROM palette numbers or ids of palettes", false;
            }
            out = r;
            return true;
        }

        // The fields of a species record that both `add` and `patch` take, applied to def and art.
        inline bool fillSpecies(const ContentDB &db, const json &rec, SpeciesDef &def, SpeciesArt &art, std::string &why)
        {
            if (!optInt(rec, "default_palette", def.defaultPalette, 0, 0x7ffff, why) || !optInt(rec, "own_illness", def.ownIllness, 0, 9, why) ||
                !optInt(rec, "starter_kit", def.kitSpecies, 0, 2, why))
                return false;
            if (rec.contains("start"))
            {
                const json &s = rec["start"];
                if (!s.is_object())
                    return why = "'start' must be an object", false;
                for (auto it = s.begin(); it != s.end(); ++it)
                {
                    int *dst = it.key() == "health" ? &def.startHealth : it.key() == "hunger" ? &def.startHunger : it.key() == "happiness" ? &def.startHappiness
                               : it.key() == "cleanliness" ? &def.startCleanliness : it.key() == "tiredness" ? &def.startTiredness : nullptr;
                    if (!dst)
                        return why = "unknown field '" + it.key() + "' in 'start'", false;
                    if (!it.value().is_number_integer() || it.value().get<int>() < 0 || it.value().get<int>() > 255)
                        return why = "start " + it.key() + " must be 0 to 255", false;
                    *dst = it.value().get<int>();
                }
            }
            int lo = def.startMoneyMin, hi = def.startMoneyMax;
            if (!optPair(rec, "money", lo, hi, 0, 9999, why))
                return false;
            if (hi < lo)
                return why = "'money' must be [low, high]", false;
            def.startMoneyMin = lo;
            def.startMoneyMax = hi;
            if (rec.contains("starter_items"))
            {
                if (!rec["starter_items"].is_array() || rec["starter_items"].size() > 64)
                    return why = "'starter_items' must be a list of { item, count }", false;
                def.starterItems.clear();
                for (const json &e : rec["starter_items"])
                {
                    if (!e.is_object() || !e.contains("item") || !e["item"].is_string() || !db.item(e["item"].get<std::string>()))
                        return why = "a starter item needs the id of an item", false;
                    StarterItem si;
                    si.item = e["item"].get<std::string>();
                    if (!optInt(e, "count", si.count, 1, 99, why))
                        return false;
                    def.starterItems.push_back(si);
                }
            }
            if (rec.contains("starter"))
            {
                const json &st = rec["starter"];
                if (!st.is_object())
                    return why = "'starter' must be an object { room, colors, rare_colors, requires }", false;
                for (auto it = st.begin(); it != st.end(); ++it)
                {
                    const std::string &k = it.key();
                    if (k == "room")
                    {
                        if (!it.value().is_boolean())
                            return why = "starter 'room' must be true or false", false;
                        def.starterRoom = it.value().get<bool>();
                    }
                    else if (k == "colors" || k == "rare_colors")
                    {
                        if (!colourList(db, it.value(), k == "colors" ? def.starterColors : def.starterRareColors, why))
                            return why = "starter '" + k + "' " + why, false;
                    }
                    else if (k == "requires")
                    {
                        if (!it.value().is_array() || it.value().size() > 16)
                            return why = "starter 'requires' must be a list of profile keys", false;
                        def.starterRequires.clear();
                        for (const json &e : it.value())
                        {
                            if (!e.is_string() || e.get<std::string>().empty())
                                return why = "starter 'requires' must be a list of profile keys", false;
                            def.starterRequires.push_back(e.get<std::string>());
                        }
                    }
                    else
                        return why = "unknown field '" + k + "' in 'starter'", false;
                }
            }
            if (rec.contains("art") && !readSpeciesArt(db, rec["art"], art, why))
                return false;
            return true;
        }

        inline void applySpecies(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "based_on", "default_palette", "own_illness", "start", "money", "starter_items", "art", "starter", "starter_kit"};
            std::string id, why;
            if (rec.contains("mode") && rec["mode"].is_string() && rec["mode"].get<std::string>() == "patch")
            { // changes a species that is there (a base one too): the same fields as add, without based_on
                for (auto it = rec.begin(); it != rec.end(); ++it)
                    if (!allowed.count(it.key()) || it.key() == "based_on")
                        return err(rep, file, m.id, "unknown field '" + it.key() + "' in a species patch");
                if (!rec.contains("id") || !rec["id"].is_string())
                    return err(rep, file, m.id, "record has no string 'id'");
                id = rec["id"].get<std::string>();
                SpeciesDef *def = db.speciesMutable(id);
                if (!def || !knownSpecies(def->legacyIndex))
                    return err(rep, file, m.id, "patch '" + id + "': no such species");
                SpeciesDef d2 = *def;
                SpeciesArt a2 = artOf(def->legacyIndex);
                if (!fillSpecies(db, rec, d2, a2, why))
                    return err(rep, file, m.id, "'" + id + "': " + why);
                *def = d2;
                a2.kit = d2.kitSpecies;
                speciesArts()[size_t(def->legacyIndex)] = a2;
                rep.lastWriter[id] = m.id;
                return;
            }
            if (!beginAdd(m, file, rec, rep, "species", allowed, [&](const std::string &i)
                          { return db.species(i) != nullptr; }, id))
                return;
            SpeciesArt art;
            SpeciesDef def;
            const bool clone = rec.contains("based_on");
            if (clone)
            {
                const SpeciesDef *b = rec["based_on"].is_string() ? db.species(rec["based_on"].get<std::string>()) : nullptr;
                if (!b || b->legacyIndex < 0 || !knownSpecies(b->legacyIndex))
                    return err(rep, file, m.id, "'" + id + "': 'based_on' must be the id of a species (like \"base:species/0\")");
                art = artOf(b->legacyIndex);
                def = *b;
                def.starterRoom = false; // a copy is not offered until its record says so
                def.starterColors.clear();
                def.starterRareColors.clear();
                def.starterRequires.clear();
            }
            else
            {
                def.startHealth = 0x41; def.startHunger = 0x34; def.startHappiness = 0x50; def.startCleanliness = 0x4b; def.startTiredness = 0x4b;
                def.startMoneyMin = 0x28; def.startMoneyMax = 0x3d;
                def.defaultPalette = 0x1df;
            }
            def.id = id;
            if (!fillSpecies(db, rec, def, art, why))
                return err(rep, file, m.id, "'" + id + "': " + why);
            if (!clone)
                for (int role : {art.walkUp, art.walkDown, art.walkSide, art.idleBase})
                    if (role < 0)
                        return err(rep, file, m.id, "'" + id + "': a species with no 'based_on' needs art.walkUp, walkDown, walkSide and idleBase");
            def.legacyIndex = int(speciesArts().size()); // its runtime number (not saved: a save keeps the id)
            art.id = id;
            art.kit = def.kitSpecies;
            speciesArts().push_back(art);
            db.put(std::move(def));
            rep.lastWriter[id] = m.id;
        }

        // Records that others refer to are applied first, so item.json may load before text.json or anim.json
        // and still find its references (file names sort alphabetically, which used to decide this by luck).
        //   pass 0: text, anim, tileset, collision, objtype (and every file-level error)
        //   pass 1: area (needs the tileset and collision)
        //   pass 2: everything else: npc, item, object, exit (they refer to areas, texts, anims and object types)
        inline int passOf(const std::string &t)
        {
            if (t == "text" || t == "anim" || t == "tileset" || t == "collision" || t == "objtype" || t == "palette" || t == "sound" || t == "music" || t == "instrument" || t == "drum")
                return 0;
            if (t == "area")
                return 1;
            if (t == "shop_stock")
                return 4; // names shops, which are made in pass 3
            return t == "spot" || t == "find" || t == "quest_spawn" || t == "gate" || t == "minigame" || t == "species" || t == "shop" || t == "side_quest" ? 3 : 2; // they name areas and items, so they come last
        }
        inline constexpr int kPasses = 5;

        inline void applyFile(ContentDB &db, const ModManifest &m, const fs::path &file, ModReport &rep, int pass)
        {
            json j;
            std::string e;
            if (!readJsonFile(file, j, e))
            {
                if (pass == 0)
                    rep.errors.push_back("[" + m.id + "] " + e);
                return;
            }
            std::vector<json> recs;
            if (j.is_array())
                recs.assign(j.begin(), j.end());
            else if (j.is_object())
                recs.push_back(j);
            else
            {
                if (pass == 0)
                    err(rep, file, m.id, "expected an array of records");
                return;
            }

            for (const json &r : recs)
            {
                if (!r.is_object() || !r.contains("type") || !r["type"].is_string())
                {
                    if (pass == 0)
                        err(rep, file, m.id, "record has no string 'type'");
                    continue;
                }
                const std::string type = r["type"].get<std::string>();
                if (passOf(type) != pass)
                    continue;
                if (type == "text")
                    applyText(db, m, file, r, rep);
                else if (type == "anim")
                    applyAnim(db, m, file, r, rep);
                else if (type == "tileset")
                    applyTileset(db, m, file, r, rep);
                else if (type == "collision")
                    applyCollision(db, m, file, r, rep);
                else if (type == "objtype")
                    applyObjType(db, m, file, r, rep);
                else if (type == "palette")
                    applyPalette(db, m, file, r, rep);
                else if (type == "sound")
                    applyAudio(db, m, file, r, rep, false);
                else if (type == "music")
                    applyAudio(db, m, file, r, rep, true);
                else if (type == "instrument")
                    applyInstrument(m, file, r, rep);
                else if (type == "drum")
                    applyDrum(m, file, r, rep);
                else if (type == "area")
                    applyArea(db, m, file, r, rep);
                else if (type == "object")
                    applyObject(db, m, file, r, rep);
                else if (type == "exit")
                    applyExit(db, m, file, r, rep);
                else if (type == "npc")
                    applyNpc(db, m, file, r, rep);
                else if (type == "item")
                    applyItem(db, m, file, r, rep);
                else if (type == "spot")
                    applySpot(db, m, file, r, rep);
                else if (type == "find")
                    applyFind(db, m, file, r, rep);
                else if (type == "quest_spawn")
                    applyQuestSpawn(db, m, file, r, rep);
                else if (type == "gate")
                    applyGate(db, m, file, r, rep);
                else if (type == "minigame")
                    applyMinigame(db, m, file, r, rep);
                else if (type == "shop")
                    applyShop(db, m, file, r, rep);
                else if (type == "shop_stock")
                    applyShopStock(db, m, file, r, rep);
                else if (type == "side_quest")
                    applySideQuest(db, m, file, r, rep);
                else if (type == "species")
                    applySpecies(db, m, file, r, rep);
                else
                    err(rep, file, m.id, "unknown record type '" + type + "'");
            }
        }
    } // namespace modimpl

    // Call once after importRom. Errors don't stop other records from loading; the caller decides whether
    // a non-ok report means "refuse to start" or "warn and continue" (--validate-mods should print and exit).
    inline ModReport importMods(ContentDB &db, const fs::path &modsDir)
    {
        ModReport rep;
        forgetModAudio();
        forgetModSpecies(); // the front end and the game each load the mods: the species of a mod must not be added twice
        if (!fs::exists(modsDir))
            return rep;

        std::vector<ModManifest> mods;
        for (const auto &de : fs::directory_iterator(modsDir))
        {
            if (!de.is_directory() || !fs::exists(de.path() / "manifest.json"))
                continue;
            ModManifest m;
            std::string e;
            if (parseManifest(de.path(), m, e))
                mods.push_back(std::move(m));
            else
                rep.errors.push_back(e);
        }
        if (!sortMods(mods, rep.errors))
            return rep; // dependency problems: load nothing rather than a half-working set

        for (const ModManifest &m : mods)
        {
            const fs::path dataDir = m.dir / "data";
            if (fs::exists(dataDir))
            {
                std::vector<fs::path> files;
                for (const auto &f : fs::directory_iterator(dataDir))
                    if (f.is_regular_file() && f.path().extension() == ".json")
                        files.push_back(f.path());
                std::sort(files.begin(), files.end());
                for (int pass = 0; pass < modimpl::kPasses; ++pass)
                    for (const fs::path &f : files)
                        modimpl::applyFile(db, m, f, rep, pass);
            }
            rep.loaded.push_back(m.id);
            rep.versions[m.id] = m.version;
        }
        // a song's instruments named by id must exist now that every mod's are there (a warning: the channel plays instrument 0 / the drum is left out)
        for (const auto *list : {&audioRegistry().songs})
            for (const ModSound &song : *list)
                if (song.midi)
                {
                    std::map<int, int> p, d, c;
                    std::vector<std::string> unknown;
                    resolveModSound(song, p, d, c, &unknown);
                    for (const std::string &u : unknown)
                        rep.warnings.push_back(song.id + ": " + u);
                }
        for (const auto &kv : audioRegistry().songPatches)
        {
            std::map<int, int> p, d, c;
            std::vector<std::string> unknown;
            resolveModSound(kv.second, p, d, c, &unknown);
            for (const std::string &u : unknown)
                rep.warnings.push_back(kv.second.id + ": " + u);
            if (audioRegistry().songOver.count(kv.first))
                rep.warnings.push_back(kv.second.id + ": ignored, a mod replaces the song with a file");
        }
        for (const auto &kv : audioRegistry().songOver)
            if (kv.second.midi)
            {
                std::map<int, int> p, d, c;
                std::vector<std::string> unknown;
                resolveModSound(kv.second, p, d, c, &unknown);
                for (const std::string &u : unknown)
                    rep.warnings.push_back(kv.second.id + ": " + u);
            }
        // an npc's species is found by its id now that every mod's species is there; its number is what the code keeps
        for (const NpcDef *seen : db.allNpcs())
            if (!seen->speciesId.empty())
            {
                const SpeciesDef *sd = db.species(seen->speciesId);
                if (!sd || !knownSpecies(sd->legacyIndex))
                    rep.errors.push_back("npc '" + seen->id + "': unknown species '" + seen->speciesId + "'");
                else
                    db.npcMutable(seen->id)->speciesOrAnim = sd->legacyIndex;
            }
        return rep;
    }

} // namespace gp