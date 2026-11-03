// mod_loader.hpp - importMods: scan mods/, sort, apply records on top of the db (after importRom).
// Phase 1: only the "text" record type exists. Every other type is a loud error, so typos can't pass silently.
#pragma once
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
                                                       "stationary", "anim", "lines", "palette", "based_on"};
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
            if (!intField("x", n.x) || !intField("y", n.y))
                return;
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
            if (mode == "add" && n.anim.empty())
                return err(rep, file, m.id, "add '" + id + "': missing 'anim'");
            if (n.stationary && n.lines.size() < 3)
                return err(rep, file, m.id, "'" + id + "': a stationary npc needs 3 lines");

            db.put(std::move(n));
            rep.lastWriter[id] = m.id;
        }

        inline void applyAnim(ContentDB &db, const ModManifest &m, const fs::path &file, const json &rec, ModReport &rep)
        {
            static const std::set<std::string> allowed{"type", "mode", "id", "frames"};
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
            if (!optInt(rec, "y_offset", a.collisionYOffset, -64, 64, why) || !optInt(rec, "music", a.music, 0, 8, why) ||
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
            if (!optInt(rec, "y_offset", a.collisionYOffset, -64, 64, why) || !optInt(rec, "music", a.music, 0, 8, why) ||
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

        // Records that others refer to are applied first, so item.json may load before text.json or anim.json
        // and still find its references (file names sort alphabetically, which used to decide this by luck).
        //   pass 0: text, anim, tileset, collision, objtype (and every file-level error)
        //   pass 1: area (needs the tileset and collision)
        //   pass 2: everything else: npc, item, object, exit (they refer to areas, texts, anims and object types)
        inline int passOf(const std::string &t)
        {
            if (t == "text" || t == "anim" || t == "tileset" || t == "collision" || t == "objtype" || t == "palette")
                return 0;
            if (t == "area")
                return 1;
            return t == "spot" || t == "find" || t == "minigame" ? 3 : 2; // they name areas and items, so they come last
        }
        inline constexpr int kPasses = 4;

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
                else if (type == "minigame")
                    applyMinigame(db, m, file, r, rep);
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
        }
        return rep;
    }

} // namespace gp