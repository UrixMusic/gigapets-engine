// fe_mods.hpp - the Mods screen of the title menu: the list of every mod folder, what the selected one is (name, version, description, icon, trusted), switch a mod on / off (Z),
// and the order mode (C): Z picks a mod up, Up / Down move it, Z puts it down. What the player chose is kept in mods.txt (mods/mod_state.hpp) and used the next time mods load.
#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "../../mods/mod_state.hpp"
#include "../../mods/native_mod.hpp"
#include "../data/settings.hpp"
#include "fe_strings.hpp"
#include "../../engine/keybinds.hpp"
#include "../../mods/mod_loader.hpp" // (and with it stb_image for the icons)

namespace gp::fe
{
    struct ModsUi
    {
        fs::path dir = "mods";
        std::string statePath;
        ModState state;
        std::vector<ModEntry> list;
        int cur = 0, scroll = 0;
        bool order = false; // order mode (C)
        bool held = false;  // order mode: the selected mod is picked up (Up / Down move it)
        bool changed = false; // something was switched or moved: the front end loads again when the player leaves
        NativeAllow allow;       // the native mods the player has allowed (native_allowed.txt)
        int confirm = -1;        // >= 0: the "allow native code?" question for list[confirm] is open
        int confirmYes = 0;      // its cursor: 0 = No (the start), 1 = Allow
        bool safeNext = false;   // the last game did not end normally: native mods stay off for the next one
        std::vector<std::string> problems; // what went wrong when the mods loaded (errors and warnings, as the loader worded them)
        std::map<std::string, SDL_Texture *> icons;
        static constexpr int kRows = 11;

        void open(const std::string &modsDir, const std::string &stateFile)
        {
            dir = modsDir;
            statePath = stateFile;
            state = loadModState(statePath);
            list = scanMods(dir, state);
            cur = scroll = 0;
            order = held = false;
            changed = false;
            allow = loadNativeAllow(nativeAllowPath());
            confirm = -1;
            safeNext = fileExists(nativeRunningPath());
        }
        static std::string dllOf(const ModEntry &e) { return (e.m.dir / e.m.native).string(); }
        // native code the player has not allowed (this exact dll)
        bool needsOk(const ModEntry &e) const { return !e.m.native.empty() && !allow.allowed(e.m.id, dllOf(e)); }
        // X while the question is open closes it
        bool cancelConfirm()
        {
            if (confirm < 0)
                return false;
            confirm = -1;
            return true;
        }
        const ModEntry *selected() const { return cur >= 0 && cur < int(list.size()) ? &list[size_t(cur)] : nullptr; }

        void follow()
        {
            if (cur < scroll)
                scroll = cur;
            if (cur >= scroll + kRows)
                scroll = cur - kRows + 1;
        }
        void moveCursor(int d)
        {
            if (list.empty())
                return;
            if (confirm >= 0) // the question: Up / Down / Left / Right swap the answer
            {
                confirmYes ^= 1;
                return;
            }
            if (held) // the picked up mod moves with the cursor
            {
                const int to = cur + d;
                if (to < 0 || to >= int(list.size()))
                    return;
                const std::string id = list[size_t(cur)].m.id;
                std::swap(list[size_t(cur)], list[size_t(to)]);
                commit(id);
                return;
            }
            cur = (cur + d + int(list.size())) % int(list.size());
            follow();
        }
        // Z
        void press()
        {
            if (list.empty())
                return;
            if (confirm >= 0)
            {
                const ModEntry &e = list[size_t(confirm)];
                const std::string eid = e.m.id;
                const bool yes = confirmYes == 1 && allow.allow(eid, dllOf(e));
                confirm = -1;
                if (yes)
                {
                    saveNativeAllow(allow, nativeAllowPath());
                    state.off.erase(eid); // allowing it also switches it on
                    commit(eid);
                }
                return;
            }
            if (order)
            {
                held = !held;
                return;
            }
            if (needsOk(list[size_t(cur)])) // native code: ask first (whether it is on or off)
            {
                confirm = cur;
                confirmYes = 0;
                return;
            }
            const std::string id = list[size_t(cur)].m.id;
            if (!state.off.erase(id))
                state.off.insert(id);
            commit(id);
        }
        // C
        void toggleOrder()
        {
            if (confirm >= 0)
                return;
            order = !order;
            held = false;
        }
        // keeps the new order and switches, saves them, and lists again (a mod can not go before one it requires: the list shows where it really loads)
        void commit(const std::string &keepId)
        {
            state.order.clear();
            for (const ModEntry &e : list)
                state.order.push_back(e.m.id);
            saveModState(state, statePath);
            changed = true;
            list = scanMods(dir, state);
            for (int i = 0; i < int(list.size()); ++i)
                if (list[size_t(i)].m.id == keepId)
                    cur = i;
            follow();
        }

        // the mod's 32x32 icon (icon.png in its folder) as a texture, null if it has none
        SDL_Texture *icon(SDL_Renderer *ren, const ModEntry &e)
        {
            if (!e.m.hasIcon || !ren)
                return nullptr;
            auto it = icons.find(e.m.id);
            if (it != icons.end())
                return it->second;
            SDL_Texture *tex = nullptr;
            int w = 0, h = 0, n = 0;
            const std::string path = (e.m.dir / "icon.png").string();
            if (unsigned char *px = stbi_load(path.c_str(), &w, &h, &n, 4))
            {
                if (SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32))
                {
                    tex = SDL_CreateTextureFromSurface(ren, s);
                    SDL_FreeSurface(s);
                }
                stbi_image_free(px);
            }
            icons[e.m.id] = tex;
            return tex;
        }
    };

    // Breaks `text` into lines of at most `cols` characters (at spaces)
    inline std::vector<std::string> wrapLines(const std::string &text, size_t cols)
    {
        std::vector<std::string> out;
        std::string line, word;
        auto flushWord = [&]
        {
            if (word.empty())
                return;
            if (!line.empty() && line.size() + 1 + word.size() > cols)
            {
                out.push_back(line);
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
            word.clear();
        };
        for (char c : text)
        {
            if (word.size() >= cols) // a word longer than a line is cut
            {
                if (!line.empty())
                    out.push_back(line);
                line.clear();
                out.push_back(word);
                word.clear();
            }
            if (c == ' ' || c == '\n')
            {
                flushWord();
                if (c == '\n')
                {
                    out.push_back(line);
                    line.clear();
                }
            }
            else
                word += c;
        }
        flushWord();
        if (!line.empty())
            out.push_back(line);
        return out;
    }

    // Draws the screen (the background is the caller's). G: drawTextBox(style, pal, x, y, text, w, mode, layer).
    template <class G>
    void drawMods(G &g, SDL_Renderer *ren, ModsUi &ui)
    {
        constexpr int L = 0x3000;
        g.drawTextBox(4, 0x4c7, 0, 6, feText("mods_title"), 0x140, 1, L);
        if (ui.confirm >= 0) // the question about native code: over everything, red
        {
            SDL_Rect all{0, 0, 320, 240};
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
            SDL_RenderFillRect(ren, &all);
            SDL_Rect r{16, 28, 288, 176};
            SDL_SetRenderDrawColor(ren, 50, 8, 8, 255);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 255, 90, 70, 255);
            SDL_RenderDrawRect(ren, &r);
            g.drawTextBox(4, 0x4c7, 16, 34, feText("native_title"), 288, 1, L + 1);
            int y = 66;
            g.drawTextBox(2, 0x28c, 28, y, ui.list[size_t(ui.confirm)].m.name, 264, 0, L + 1);
            y += 16;
            for (const std::string &ln : wrapLines(feText("native_warn"), 32))
            {
                g.drawTextBox(2, 0x28c, 28, y, ln, 264, 0, L + 1);
                y += 12;
            }
            for (int i = 0; i < 2; ++i)
            {
                SDL_Rect b{28 + i * 130, 172, 120, 18};
                SDL_SetRenderDrawColor(ren, i == ui.confirmYes ? (i == 1 ? 200 : 60) : 30, i == ui.confirmYes ? (i == 1 ? 60 : 110) : 30, i == ui.confirmYes ? (i == 1 ? 40 : 200) : 40, 255);
                SDL_RenderFillRect(ren, &b);
                g.drawTextBox(2, 0x28c, b.x + 8, b.y + 3, feText(i == 0 ? "native_no" : "native_yes"), 110, 0, L + 1);
            }
            return; // nothing else is drawn under the question
        }
        if (ui.list.empty())
        {
            g.drawTextBox(2, 0x28c, 16, 40, feText("mods_none"), 280, 0, L);
            return;
        }
        // the list (left) and the info box (right)
        auto box = [&](int x, int y, int w, int h)
        {
            SDL_Rect r{x, y, w, h};
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 8, 20, 60, 190);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 200, 220, 255, 255);
            SDL_RenderDrawRect(ren, &r);
        };
        box(8, 26, 150, 176);
        box(164, 26, 148, 176);
        const int last = std::min(int(ui.list.size()), ui.scroll + ModsUi::kRows);
        for (int i = ui.scroll; i < last; ++i)
        {
            const ModEntry &e = ui.list[size_t(i)];
            const int y = 32 + (i - ui.scroll) * 15;
            const bool sel = i == ui.cur;
            if (sel) // the cursor's row; a mod picked up in order mode is yellow
            {
                SDL_Rect r{10, y - 3, 146, 14};
                if (ui.held)
                    SDL_SetRenderDrawColor(ren, 200, 150, 20, 220);
                else
                    SDL_SetRenderDrawColor(ren, 60, 110, 200, 220);
                SDL_RenderFillRect(ren, &r);
            }
            std::string name = e.m.name.size() > 18 ? e.m.name.substr(0, 17) + "." : e.m.name;
            g.drawTextBox(2, e.on ? 0x28c : 0x105, 24, y, name, 128, 0, L);
            if (!e.m.native.empty())
                g.drawTextBox(2, 0x28c, 146, y, "*", 10, 0, L);
            if (sel)
                g.drawTextBox(2, 0x28c, 12, y, ui.order ? (ui.held ? "=" : "+") : ">", 10, 0, L);
        }
        if (ui.scroll > 0)
            g.drawTextBox(2, 0x28c, 140, 28, "^", 10, 0, L);
        if (last < int(ui.list.size()))
            g.drawTextBox(2, 0x28c, 140, 190, "v", 10, 0, L);
        if (const ModEntry *e = ui.selected())
        {
            int y = 32;
            SDL_Rect r{170, y, 32, 32};
            if (SDL_Texture *t = ui.icon(ren, *e))
                SDL_RenderCopy(ren, t, nullptr, &r);
            else
            {
                SDL_SetRenderDrawColor(ren, 70, 90, 150, 255);
                SDL_RenderFillRect(ren, &r);
            }
            for (const std::string &ln : wrapLines(e->m.name, 16))
            {
                g.drawTextBox(2, 0x28c, 208, y, ln, 100, 0, L);
                y += 12;
            }
            y = std::max(y, 68);
            g.drawTextBox(2, 0x28c, 170, y, feText("mods_version") + e->m.version, 140, 0, L);
            y += 12;
            g.drawTextBox(2, 0x28c, 170, y, feText(e->on ? "mods_on" : "mods_off"), 140, 0, L);
            y += 12;
            if (!e->m.native.empty())
            {
                const bool ok = !ui.needsOk(*e);
                for (const std::string &ln : wrapLines(feText(ok ? "mods_native" : ui.allow.changed(e->m.id, ModsUi::dllOf(*e)) ? "mods_native_changed" : "mods_native_ask"), 16))
                {
                    g.drawTextBox(2, ok ? 0x28c : 0x105, 170, y, ln, 140, 0, L);
                    y += 12;
                }
            }
            if (e->m.trusted)
            {
                g.drawTextBox(2, 0x28c, 170, y, feText("mods_trusted"), 140, 0, L);
                y += 12;
            }
            if (!e->note.empty())
                for (const std::string &ln : wrapLines(e->note, 17))
                {
                    g.drawTextBox(2, 0x105, 170, y, ln, 140, 0, L);
                    y += 12;
                }
            {   // what went wrong when this mod loaded
                int n = 0;
                std::string first;
                for (const std::string &p : ui.problems)
                    if (p.find("[" + e->m.id + "]") != std::string::npos || p.find("'" + e->m.id + "'") != std::string::npos)
                        if (n++ == 0)
                            first = p;
                if (n)
                {
                    g.drawTextBox(2, 0x105, 170, y, std::to_string(n) + feText(n == 1 ? "mods_problem" : "mods_problems"), 140, 0, L);
                    y += 12;
                    const size_t brk = first.find("] ");
                    int lines = 0;
                    for (const std::string &ln : wrapLines(brk != std::string::npos ? first.substr(brk + 2) : first, 17))
                    {
                        if (lines++ == 3)
                            break;
                        g.drawTextBox(2, 0x105, 170, y, ln, 140, 0, L);
                        y += 12;
                    }
                }
            }
            y += 4;
            for (const std::string &ln : wrapLines(e->m.description, 17))
            {
                if (y > 190)
                    break;
                g.drawTextBox(2, 0x28c, 170, y, ln, 140, 0, L);
                y += 12;
            }
        }
        std::string help = feText(ui.order ? (ui.held ? "mods_help_held" : "mods_help_order") : "mods_help");
        for (const auto &[tag, action] : {std::pair<const char *, Action>{"{select}", Action::Select}, {"{menu}", Action::Menu}, {"{cancel}", Action::Cancel}}) // the keys as the player has set them
            for (size_t at; (at = help.find(tag)) != std::string::npos;)
                help.replace(at, std::strlen(tag), keys().name(int(action)));
        g.drawTextBox(2, 0x28c, 8, 208, help, 304, 0, L);
        g.drawTextBox(2, 0x28c, 8, 222, feText(ui.safeNext ? "mods_safe" : "mods_note"), 304, 0, L);
    }
} // namespace gp::fe
