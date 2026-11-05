// walk_draw.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of WorldDraw.
// What they need from the rest of the walk session comes in through WorldDrawEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct Layout
{
    int frame, x, y, textX, textY, textW, btnX, btnY;
};

struct WorldDrawEnv
{
    std::function<AnimRef(const std::string&)> animRefById;
    gp::ContentDB &db;
    unsigned int&gameTicks;
    const std::vector<gp::ItemDef>&itemDefs;
    const int &kEvPal;
    const int &kFlowerSprite;
    int &kHoleDY;
    const int &kHoleSprite;
    const unsigned char &kShadowAlpha;
    std::function<AnimRef(const gp::NpcActor&, int)> npcAnim;
    std::function<std::pair<float, float>(const gp::NpcActor&)> npcPosF;
    TextDrawer &numbers;
    std::map<std::string, int> &objShadow;
    int &playerSpecies;
    gp::QuestRuntime &quests;
    std::function<std::string(int, int)> reactAnimId;
    SDL_Renderer *&ren;
    gp::Rom&rom;
    std::function<void(int, int)> sfxFn;
    TextDrawer &signText;
    SpriteCache &sprites;
    int &stinkTicks;
    TextDrawer &swiftTitle;
    TextDrawer &text;
    std::function<std::string(int, int)> uiIdP;
};

class WorldDraw : public WorldDrawEnv
{
public:
    explicit WorldDraw(WorldDrawEnv env) : WorldDrawEnv(std::move(env)) {}
    ~WorldDraw()
    {
        for (auto &[name, c] : canvases_)
            if (c.tex)
                SDL_DestroyTexture(c.tex);
        if (frameTex_)
            SDL_DestroyTexture(frameTex_);
    }

    // ---- what a script can draw beyond the basics (see DrawCmd): canvases (offscreen pictures), the frame so far, transforms, shapes and meshes ----
    struct Canvas
    {
        SDL_Texture *tex = nullptr;
        int w = 0, h = 0;
        long used = 0;
    };
    std::map<std::string, Canvas> canvases_;
    long canvasClock_ = 0;
    SDL_Texture *frameTex_ = nullptr;
    int frameW_ = 0, frameH_ = 0;
    std::vector<unsigned char> framePixels_;

    // The window picture as it is now (the whole logical screen), as a texture of its own: "@frame" in g.blit / g.mesh. Read back from the screen, so it is only done when a script asks.
    bool captureFrame()
    {
        int lw = 0, lh = 0;
        SDL_RenderGetLogicalSize(ren, &lw, &lh);
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        int x0 = 0, y0 = 0, x1 = ow, y1 = oh;
        if (lw > 0 && lh > 0)
        {
            SDL_RenderLogicalToWindow(ren, 0.f, 0.f, &x0, &y0);
            SDL_RenderLogicalToWindow(ren, float(lw), float(lh), &x1, &y1);
        }
        x0 = std::clamp(x0, 0, ow);
        y0 = std::clamp(y0, 0, oh);
        x1 = std::clamp(x1, 0, ow);
        y1 = std::clamp(y1, 0, oh);
        const int w = x1 - x0, h = y1 - y0;
        if (w < 1 || h < 1)
            return false;
        if (!frameTex_ || frameW_ != w || frameH_ != h)
        {
            if (frameTex_)
                SDL_DestroyTexture(frameTex_);
            frameTex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            frameW_ = w;
            frameH_ = h;
            if (!frameTex_)
                return false;
        }
        framePixels_.resize(size_t(w) * size_t(h) * 4);
        const SDL_Rect r{x0, y0, w, h};
        if (SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, framePixels_.data(), w * 4) != 0)
            return false;
        SDL_UpdateTexture(frameTex_, nullptr, framePixels_.data(), w * 4);
        return true;
    }
    static SDL_BlendMode blendFor(int b) { return b == 1 ? SDL_BLENDMODE_ADD : b == 2 ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND; }

    // The texture a blit / mesh names; w, h = its size in the units it is drawn in (a canvas: its pixels; the frame: the logical screen)
    SDL_Texture *picture(const std::string &name, bool &frameCaptured, int &w, int &h)
    {
        if (name == "@frame")
        {
            if (!frameCaptured)
                frameCaptured = captureFrame();
            if (!frameCaptured)
                return nullptr;
            w = gp::logicalW();
            h = 240;
            return frameTex_;
        }
        const auto it = canvases_.find(name);
        if (it == canvases_.end() || !it->second.tex)
            return nullptr;
        w = it->second.w;
        h = it->second.h;
        return it->second.tex;
    }
    // Starts a canvas: its texture (made if there is none of that size) becomes the target, cleared to nothing
    bool beginCanvas(const std::string &name, int w, int h)
    {
        Canvas &c = canvases_[name];
        if (!c.tex || c.w != w || c.h != h)
        {
            if (c.tex)
                SDL_DestroyTexture(c.tex);
            c.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
            c.w = w;
            c.h = h;
            if (c.tex)
                SDL_SetTextureBlendMode(c.tex, SDL_BLENDMODE_BLEND);
        }
        if (!c.tex)
            return false;
        c.used = ++canvasClock_;
        while (canvases_.size() > 8) // keep the 8 used last
        {
            auto oldest = canvases_.end();
            for (auto it = canvases_.begin(); it != canvases_.end(); ++it)
                if (it->first != name && (oldest == canvases_.end() || it->second.used < oldest->second.used))
                    oldest = it;
            if (oldest == canvases_.end())
                break;
            if (oldest->second.tex)
                SDL_DestroyTexture(oldest->second.tex);
            canvases_.erase(oldest);
        }
        SDL_SetRenderTarget(ren, c.tex);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
        SDL_RenderClear(ren);
        return true;
    }
    // A filled or outlined fan / circle as triangles
    void fillPoly(const std::vector<SDL_FPoint> &pts, SDL_Color col)
    {
        if (pts.size() < 3)
            return;
        std::vector<SDL_Vertex> v(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            v[i] = {pts[i], col, {0.f, 0.f}};
        std::vector<int> idx;
        for (size_t i = 1; i + 1 < pts.size(); ++i)
        {
            idx.push_back(0);
            idx.push_back(int(i));
            idx.push_back(int(i + 1));
        }
        SDL_RenderGeometry(ren, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
    }
    // A thick line as a quad
    void thickLine(float x1, float y1, float x2, float y2, float w, SDL_Color col)
    {
        float dx = x2 - x1, dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f)
            return;
        const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
        fillPoly({{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}}, col);
    }
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }

    static inline const Layout kLayouts[3] = {
        {0x48, 0x30, 0x30, 0x10, 0x12, 0xbd, 0x74 + 0x2b, 0x57},
        {0x49, 0x30, 0x14, 0x10, 0x12, 0xbd, 0x74 + 0x2b, 0x8c},
        {0x4a, 0x16, 0x14, 0x0c, 0x10, 0xf4, 0xb0 + 0x2b, 0x8c},
    };

    // ---- objects: drawn as sprites (not baked into the background) so they can sort against NPCs and the pet ----
    auto drawObject(const gp::ObjectDef &o, float camX, float camY)
    { sprites.drawObject(o, camX, camY); }

    auto drawNpc(const gp::NpcActor &n, float camX, float camY)
    {
        const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
        if (A.size() == 0)
            return;
        const int fr = std::min(n.anim.currentFrame, int(A.size()) - 1);
        const gp::SpriteFrame &s = A.anim->frames[fr];
        const int key = n.animKey();
        // Key 7 (idle toward the pet): dog and monkey art faces left, panda art faces right (PlayIdleAnim_Scripted).
        const int npcSp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
        const bool sideIdleFlip = n.body.facing == (gp::artOf(npcSp).facesRight ? gp::FaceLeft : gp::FaceRight);
        const bool flip = key == 3                 ? n.body.facing == gp::FaceLeft
                          : (key == 4 || key == 5) ? n.body.facing == gp::FaceRight
                          : key == 6               ? n.forcedFlip
                          : key == 7               ? !n.def->idleSideAnim.empty() && sideIdleFlip
                                                   : false;
        const auto np = npcPosF(n);
        const float x = flip ? np.first - camX + s.anchorX - s.dx - s.w
                             : np.first - camX - s.anchorX + s.dx;
        SDL_FRect r{x, np.second - camY - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
        SDL_RenderCopyExF(ren, (*A.tex)[fr], nullptr, &r, 0, nullptr,
                          flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        if (n.overheadTick >= 0 && n.def->stationary) // its own overhead animation, over the NPC
        {
            const AnimRef B = animRefById(reactAnimId(n.def->behaviorFlags, n.def->appearancePalette));
            if (B.size())
            {
                const int bf = std::min(n.overheadTick * gp::kOverheadFps / 30, int(B.size()) - 1);
                const gp::SpriteFrame &bs = B.anim->frames[size_t(bf)];
                SDL_FRect br{np.first - camX - bs.anchorX + bs.dx, np.second - camY - bs.anchorY + bs.dy + 1, float(bs.w), float(bs.h)};
                SDL_RenderCopyF(ren, (*B.tex)[size_t(bf)], nullptr, &br);
            }
        }
        if (n.stinky && !n.def->stationary) // SpawnMessageBubble(6, ...): the stink cloud loops over a stinky NPC
        {
            const AnimRef K = animRefById("base:anim/" + std::to_string(gp::kStinkAnim) + "@28c");
            if (K.size())
            {
                const int dir = key == 3 ? (n.body.facing == gp::FaceLeft ? 0 : 1) : 2;
                const gp::BubbleOffset bo = gp::overheadOffset(npcSp, dir, 0);
                const int f = (stinkTicks * gp::kStinkFps / 30) % int(K.size());
                const gp::SpriteFrame &ks = K.anim->frames[size_t(f)];
                SDL_FRect kc{np.first - camX + bo.dx - ks.anchorX + ks.dx,
                             np.second - camY + bo.dy + gp::bubbleLower(npcSp) - ks.anchorY + ks.dy, float(ks.w), float(ks.h)};
                SDL_RenderCopyF(ren, (*K.tex)[size_t(f)], nullptr, &kc);
            }
        }
    }

    // Draws a UI sprite the way objects are drawn: position minus anchor, plus the frame's offset.
    auto drawUi(int spriteId, int x, int y, int pal = gp::kDialoguePalette)
    {
        const gp::Id uid = gp::uiSpriteId(spriteId, pal);
        const AnimRef A = animRefById(uid);
        static std::set<gp::Id> warned; // #include <set>
        if (A.size() == 0)
        {
            if (warned.insert(uid).second)
                std::printf("ui sprite missing: %s\n", uid.c_str());
            return;
        }
        const gp::SpriteFrame &s = A.anim->frames[0];
        // std::printf("ui %x: %dx%d anchor(%d,%d) d(%d,%d)\n", spriteId, s.w, s.h, s.anchorX, s.anchorY, s.dx, s.dy);
        SDL_Rect r{x + s.dx, y + s.dy, s.w, s.h}; // try this first
        SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
    }

    auto drawUiF(int spriteId, float x, float y, int pal = gp::kDialoguePalette)
    {
        const AnimRef A = animRefById(gp::uiSpriteId(spriteId, pal));
        if (A.size() == 0)
            return;
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_FRect r{x + s.dx, y + s.dy, float(s.w), float(s.h)};
        SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
    }

    auto drawUiId(const gp::Id &id, int x, int y)
    {
        const AnimRef A = animRefById(id);
        if (A.size() == 0)
            return;
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_Rect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy, s.w, s.h};
        SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
    }

    // The game draws icons at y = rowY + E - 12, where E comes from a call the decompiler calls
    // GetSpriteWidth. E = the frame height is a first guess: adjust by eye.
    auto drawIcon(int spriteId, int x, int rowY)
    {
        const AnimRef A = animRefById(gp::uiSpriteId(spriteId));
        if (A.size() == 0)
            return;
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_Rect r{x - s.anchorX + s.dx, rowY - 12 + s.h - s.anchorY + s.dy, s.w, s.h};
        SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
    }

    // Layout numbers are from RenderActiveDialogueWindow (stats box: x 0x16, y 0x14, width 0xf4).
    auto drawDialogue(const gp::DialogueBox &b)
    {
        const bool stats = b.kind == gp::BoxKind::Stats || b.kind == gp::BoxKind::StatsLow;
        const Layout &L = kLayouts[stats ? 2 : std::clamp(b.layout, 0, 2)];
        // With a portrait the original passes indent 0x36 over the first 0x28 px of height.
        const int indent = b.portrait.empty() ? 0 : 0x36;
        const int indentH = b.portrait.empty() ? 0 : 0x28;

        if (b.kind == gp::BoxKind::Picture) // the island map (RenderActiveDialogueWindow, type 6)
        {
            // Colour 0 of the map is see-through; behind it is the screen's backdrop, not the world.
            gp::clearFrame(ren); // the whole screen: the border around the picture in 16:9
            drawUi(gp::kMapTile, 0, 0, gp::kMapPalette);
            drawUi(gp::kMapMarkerTile, b.markX, b.markY, gp::kMapMarkerPalette);
            return;
        }
        if (b.kind == gp::BoxKind::Sign) // <-- the block goes here, inside the lambda
        {
            drawUi(0x17, 0, 0, 0x11f);
            const int h = signText.layout(b.body, 0x1e, 0, 0x105, gp::Align::Center).height;
            signText.draw(b.body, 0x1e, 0x6e - h / 2, 0x105, gp::Align::Center);
            return;
        }

        drawUi(L.frame, L.x, L.y);
        if (!b.portrait.empty())
            drawUiId(b.portrait, L.x + 0x14, L.y + 0x14);

        if (b.kind == gp::BoxKind::Quest) // DrawQuestObjectiveScreen: everything at fixed screen positions
        {
            auto str = [&](const char *k)
            { return db.textOr(std::string("base:str/quest/") + k); };
            int y = 0x32;
            text.draw(b.questHeading, 100, y, 0xfa, gp::Align::Left);
            y += 0x10;
            for (const auto &row : b.questRows)
            {
                numbers.draw(std::to_string(row.first), 0x6e, y, 0x14, gp::Align::Left);
                numbers.draw(row.second, 0x7d, y, 0xaf, gp::Align::Left);
                y += 0xc;
            }
            y += 8;
            text.draw(str("objective_bring"), 100, y, 0xfa, gp::Align::Left);
            y += 0x10;
            numbers.draw(b.questBringTo, 0x6e, y, 0xaf, gp::Align::Left);
            y += numbers.layout(b.questBringTo, 0x6e, y, 0xaf, gp::Align::Left).height + 3;
            if (!b.questLookIn.empty())
            {
                text.draw(str("objective_look"), 100, y, 0xfa, gp::Align::Left);
                y += 0x10;
                numbers.draw(b.questLookIn, 0x6e, y, 0xaf, gp::Align::Left);
            }
            drawUi(0x27, L.x + L.btnX, L.y + L.btnY);
            return;
        }

        if (!stats)
        {
            // Message / yes-no boxes: centred (align 1), indented around the portrait if there is one.
            text.draw(b.body, L.x + L.textX, L.y + L.textY, L.textW, gp::Align::Center, indent, indentH);
            if (b.kind == gp::BoxKind::Confirm)
            {
                const int y = L.y + L.btnY;
                drawUi(b.yesSelected ? 0x4f : 0x4e, L.x + L.btnX - 0x2b, y); // Yes
                drawUi(b.yesSelected ? 0x4b : 0x4c, L.x + L.btnX, y);        // No
            }
            else if (b.kind == gp::BoxKind::Quantity)
            {
                // RenderActiveDialogueWindow type 3: SprintF("%d") drawn in the body's own font, centred over the text width, 13
                // px below the button row in the first layout and on it in the others.
                text.draw(std::to_string(b.qty), L.x + L.textX, L.y + L.btnY + (b.layout == 0 ? 13 : 0), L.textW, gp::Align::Center);
                drawUi(0x27, L.x + L.btnX, L.y + L.btnY);
            }
            else
                drawUi(0x27, L.x + L.btnX, L.y + L.btnY);

            return;
        }

        const int base = b.kind == gp::BoxKind::StatsLow ? 0x8c : 0x3c;
        text.draw(b.title, 0x32, base - 0x19, 0xdc, gp::Align::Center);
        if (b.kind == gp::BoxKind::Stats) // type 5 (unfriended): centred, forced to y 0x78
            text.draw(b.body, L.x + L.textX, 0x78, L.textW, gp::Align::Center);
        else // type 4 (friended): left-aligned at the top, indented around the portrait
            text.draw(b.body, L.x + L.textX, L.y + L.textY, L.textW, gp::Align::Left, indent, indentH);

        struct Cell
        {
            int icon, iconX, numX, rowY;
        };
        const Cell cells[4] = {
            {0xac2, 0x5a, 0x6e, base},        // halos
            {0xace, 0x5a, 0x6e, base + 0x23}, // pitchforks
            {0xad6, 0xb4, 0xc8, base},        // stars
            {0xac6, 0xb4, 0xc8, base + 0x23}, // lightbulbs
        };
        for (int i = 0; i < 4; i++)
        {
            drawIcon(cells[i].icon, cells[i].iconX, cells[i].rowY);
            numbers.draw(std::to_string(b.req[i]), cells[i].numX, cells[i].rowY, 0x1e, gp::Align::Right);
        }
        drawUi(0x27, L.x + L.btnX, L.y + L.btnY);
    }

    auto drawWorldSprite(const gp::Id &id, int wx, int wy, float camX, float camY)
    {
        const AnimRef A = animRefById(id);
        if (!A.size())
        {
            static std::set<gp::Id> warnedW;
            if (warnedW.insert(id).second)
                std::printf("world sprite missing: %s\n", id.c_str());
            return;
        }
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_FRect r{wx - camX - s.anchorX + s.dx, wy - camY - s.anchorY + s.dy, float(s.w), float(s.h)};
        SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
    }

    int shadowFor(const gp::Id &type)
    {
        const auto cached = objShadow.find(type);
        if (cached != objShadow.end())
            return cached->second;
        int sprite = -1, t = -1;
        const std::string head = type.substr(0, type.find('@')); // ignore a trailing "@palette"
        const size_t last = head.find_last_of("0123456789");
        // only the base game's own types ("base:objtype/12") are ROM types with a shadow; a mod's type is not, whatever digits its id ends in
        if (last != std::string::npos && head.rfind("base:objtype/", 0) == 0)
        {
            size_t b = last;
            while (b > 0 && std::isdigit(static_cast<unsigned char>(head[b - 1])))
                --b;
            t = std::atoi(head.c_str() + b); // the last run of digits in the id
        }
        if (t >= 0 && db.objectType(type))
        {
            const int base = 0xe9bd + t * 5; // hasMarker, tile, palette, markerTile, overlayTile
            const int marker = rom.u16(base + 3);
            if (rom.u16(base) != 0 && marker != 0xffff)
                try
                {
                    gp::importUiSprite(db, rom, marker, 0x28c);
                    sprite = marker;
                }
                catch (const std::exception &e)
                {
                    std::printf("object shadow 0x%x import failed: %s\n", marker, e.what());
                }
        }
        std::printf("object type '%s' -> index %d, shadow sprite %d\n", type.c_str(), t, sprite);
        objShadow[type] = sprite;
        return sprite;
    }

    auto drawShadowSprite(const gp::Id &id, float wx, float wy, float camX, float camY)
    {
        const AnimRef A = animRefById(id);
        if (!A.size())
            return;
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_Texture *tx = (*A.tex)[0];
        SDL_SetTextureAlphaMod(tx, kShadowAlpha); // these textures are only ever used for shadows
        SDL_FRect r{wx - camX - s.anchorX + s.dx, wy - camY - s.anchorY + s.dy, float(s.w), float(s.h)};
        SDL_RenderCopyF(ren, tx, nullptr, &r);
    }

    // DrawShadow: the blob is chosen by the sprite's WIDTH (the game's "GetSpriteHeight" is really the width);
    // "alt" is the species-2 variant.
    auto drawShadow(int spriteW, float wx, float wy, bool alt, float camX, float camY)
    {
        int idx = (spriteW - 16 >= 0) ? (spriteW - 16) >> 3 : (spriteW - 9) >> 3;
        idx = std::clamp(idx, 0, 11);
        drawShadowSprite(gp::uiSpriteId((alt ? 0x9a0 : 0x994) + idx, 0x28c),
                         wx - (idx * 8 + 8) / 2, wy - (alt ? 0x10 : 6), camX, camY);
    }

    auto drawNpcShadow(const gp::NpcActor &n, float camX, float camY)
    {
        if (n.def->index == 0x39)
            return;
        const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
        if (A.size() == 0)
            return;
        const int fr = std::min(n.anim.currentFrame, int(A.size()) - 1);
        const int nsp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
        const auto np = npcPosF(n);
        drawShadow(A.anim->frames[fr].w, np.first, np.second, gp::artOf(nsp).wideShadow, camX, camY);
    }

    auto drawItemShadow(int item, int wx, int wy, float camX, float camY)
    {
        if (item < 0 || item >= int(itemDefs.size()))
            return;
        const AnimRef A = animRefById(uiIdP(itemDefs[item].worldSprite, gp::kMenuPalette));
        if (A.size())
            drawShadow(A.anim->frames[0].w, wx, wy, false, camX, camY);
    }

    auto drawGroundSprite(const gp::Id &id, int wx, int wy, float camX, float camY)
    {
        const AnimRef A = animRefById(id);
        if (!A.size())
        {
            static std::set<gp::Id> warnedW;
            if (warnedW.insert(id).second)
                std::printf("world sprite missing: %s\n", id.c_str());
            return;
        }
        const gp::SpriteFrame &s = A.anim->frames[0];
        static bool shown = false;
        if (!shown && id == gp::uiSpriteId(kHoleSprite, kEvPal))
        {
            shown = true;
            std::printf("hole frame: w=%d h=%d anchor(%d,%d) d(%d,%d)\n", s.w, s.h, s.anchorX, s.anchorY, s.dx, s.dy);
        }
        SDL_FRect r{wx - camX - float(s.w / 2) + s.dx, wy - camY - float(s.h / 2) + s.dy + kHoleDY, float(s.w), float(s.h)};
        SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
    }

    auto drawEvent(const gp::WorldItem &e, float camX, float camY)
    {
        if (e.state == gp::EventState::BuriedRandom) // kind 5: flower
            drawWorldSprite(gp::uiSpriteId(kFlowerSprite, kEvPal), e.x, e.y, camX, camY);
        else if (e.state == gp::EventState::Visible && e.item < int(itemDefs.size())) // kind 2: the item itself
            drawWorldSprite(uiIdP(itemDefs[e.item].worldSprite, gp::kMenuPalette), e.x, e.y, camX, camY);
    }

    // DrawQuestRewardHud. gateA = pet not busy, gateB = no mini pet connected (not ported: pass true).
    auto drawQuestHud(bool gateA, bool gateB)
    {
        const gp::QuestState &qs = quests.q;
        if (qs.phase == 0 || !qs.hudActive)
            return;
        if (gateA && gateB)
        {
            int x = 0x23;
            for (size_t k = 0; k < qs.def.objectives.size() && k < 3; ++k, x += 100)
            {
                const gp::QuestEntry &o = qs.def.objectives[k];
                const gp::ItemDef *dp = db.item(o.item);
                if (!dp)
                    continue;
                const gp::ItemDef &d = *dp;
                const AnimRef A = animRefById(uiIdP(d.worldSprite, gp::kMenuPalette)); // ROM uses item record word 7 as the palette
                if (A.size())
                {
                    const gp::SpriteFrame &s = A.anim->frames[0];
                    const int y = s.h + 200;
                    SDL_Rect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy + 1, s.w, s.h};
                    SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
                }
                const int shown = std::min(player.itemCount(o.item), o.count);
                text.draw(std::to_string(shown) + "/" + std::to_string(o.count), x, 210, 60, gp::Align::Right);
            }
        }
        if (gp::timerShown(qs))
        {
            const int rem = gp::timerRemaining(qs, gp::elapsedSeconds(int(gameTicks), qs.timerStart));
            char b[16];
            std::snprintf(b, sizeof b, "%d:%02d", rem / 60, rem % 60);
            text.draw(b, 200, 20, 90, gp::Align::Right);
        }
    }

    // What scripts put on the HUD with objective.set: up to three lines at the top left, "have/need  text" (the counter
    // only for an item objective, counting what the player holds). Unknown items or texts (a mod that is gone) are skipped.
    auto drawScriptObjectives()
    {
        int y = 8, shown = 0;
        for (const gp::ScriptObjective &o : player.objectives)
        {
            if (shown >= 3)
                break;
            std::string line;
            if (!o.item.empty())
            {
                if (!db.item(o.item))
                    continue;
                line = std::to_string(std::min(player.itemCount(o.item), o.need)) + "/" + std::to_string(o.need) + "  ";
                if (o.text.empty()) // an item with no text of its own (a side quest's): the item's name
                    line += gp::fmtAll(db.textOr(db.item(o.item)->name, "?"), player.name);
            }
            if (!o.text.empty())
            {
                if (!db.hasText(o.text))
                    continue;
                line += gp::fmtAll(db.textOr(o.text), player.name);
            }
            numbers.draw(line, 8, y, 200, gp::Align::Left);
            y += 14;
            ++shown;
        }
        for (const gp::SideQuestDef &q : db.sideQuests()) // the running side quests: a line per objective
        {
            if (gp::sideState(player, q) != gp::SideState::Active)
                continue;
            if (q.timeLimit > 0) // the clock of a timed quest, top right like the story quest's
            {
                const int left = int(std::ceil(gp::sideTimeLeft(player, q, gp::tuning().tickHz)));
                char t[16];
                std::snprintf(t, sizeof t, "%d:%02d", left / 60, left % 60);
                numbers.draw(t, 200, 34, 90, gp::Align::Right);
            }
            for (const gp::SideObjective &o : q.objectives)
            {
                if (shown >= 3)
                    return;
                if (o.kind == gp::SideObjective::Item && !db.item(o.item))
                    continue;
                const std::string line = gp::sideLine(
                    player, o, [&](const gp::Id &it) { return gp::fmtAll(db.textOr(db.item(it)->name, "?"), player.name); },
                    [&](const gp::Id &t) { return gp::fmtAll(db.textOr(t, "?"), player.name); });
                numbers.draw(line, 8, y, 200, gp::Align::Left);
                y += 14;
                ++shown;
            }
        }
    }

    // The drawing calls a script made (scene or HUD): rects, pictures, texts and numbers on the 320 x 240 screen.
    auto drawScriptCmds(const std::vector<gp::DrawCmd> &cmds)
    {
        bool inCanvas = false, frameCaptured = false; // (the frame is read back at most once per list, when something asks for "@frame")
        for (const gp::DrawCmd &c : cmds) // "@frame" is the picture as it was when the list began: read it back first, before anything of this list is drawn
            if ((c.kind == gp::DrawCmd::Kind::Blit || c.kind == gp::DrawCmd::Kind::Mesh) && c.str == "@frame")
            {
                frameCaptured = captureFrame();
                break;
            }
        for (const gp::DrawCmd &c : cmds)
        {
            SDL_SetRenderDrawBlendMode(ren, c.blend == 1 ? SDL_BLENDMODE_ADD : c.blend == 2 ? SDL_BLENDMODE_MOD : c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, Uint8(c.r), Uint8(c.g), Uint8(c.b), Uint8(c.a));
            switch (c.kind)
            {
            case gp::DrawCmd::Kind::Clear:
            {
                const SDL_Rect all{0, 0, gp::kLogicalW, gp::kLogicalH};
                SDL_RenderFillRect(ren, &all);
                break;
            }
            case gp::DrawCmd::Kind::Rect:
            {
                const SDL_Rect r{c.x, c.y, c.w, c.h};
                c.fill ? SDL_RenderFillRect(ren, &r) : SDL_RenderDrawRect(ren, &r);
                break;
            }
            case gp::DrawCmd::Kind::Sprite:
            {
                AnimRef A = animRefById(c.str);
                int sprite = 0, pal = 0;
                if (!A.size() && std::sscanf(c.str.c_str(), "base:sprite/%x@%x", &sprite, &pal) == 2) // a game picture: brought in on first use
                {
                    try
                    {
                        gp::importUiSprite(db, rom, sprite, pal);
                    }
                    catch (const std::exception &)
                    {
                    }
                    A = animRefById(c.str);
                }
                if (!A.size())
                {
                    static std::set<gp::Id> warnedScene;
                    if (warnedScene.insert(c.str).second)
                        std::fprintf(stderr, "scene: no picture '%s'\n", c.str.c_str());
                    break;
                }
                const size_t fi = size_t(c.frame) % A.size();
                const gp::SpriteFrame &f = A.anim->frames[fi];
                SDL_Texture *st = (*A.tex)[fi];
                const bool plain = c.sx == 1.f && c.sy == 1.f && c.angle == 0.f && !c.hasPivot && c.tr == 255 && c.tg == 255 && c.tb == 255 && c.ta == 255 && c.blend == 0;
                if (plain)
                {
                    const SDL_FRect r{float(c.x - f.anchorX + f.dx), float(c.y - f.anchorY + f.dy), float(f.w), float(f.h)};
                    SDL_RenderCopyExF(ren, st, nullptr, &r, 0.0, nullptr, c.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                    break;
                }
                // scaled / turned / tinted: the picture's anchor stays at x, y, and it turns around the anchor (or `pivot`); the texture's own colour state is put back afterwards
                const float ax = float(f.anchorX - f.dx), ay = float(f.anchorY - f.dy); // the anchor, from the picture's top left
                const SDL_FRect r{float(c.x) - ax * c.sx, float(c.y) - ay * c.sy, float(f.w) * c.sx, float(f.h) * c.sy};
                const SDL_FPoint ctr = c.hasPivot ? SDL_FPoint{c.pivotX * c.sx, c.pivotY * c.sy} : SDL_FPoint{ax * c.sx, ay * c.sy};
                Uint8 or_ = 255, og = 255, ob = 255, oa = 255;
                SDL_BlendMode ob_ = SDL_BLENDMODE_BLEND;
                SDL_GetTextureColorMod(st, &or_, &og, &ob);
                SDL_GetTextureAlphaMod(st, &oa);
                SDL_GetTextureBlendMode(st, &ob_);
                SDL_SetTextureColorMod(st, Uint8(c.tr), Uint8(c.tg), Uint8(c.tb));
                SDL_SetTextureAlphaMod(st, Uint8(c.ta));
                SDL_SetTextureBlendMode(st, blendFor(c.blend));
                SDL_RenderCopyExF(ren, st, nullptr, &r, double(c.angle), &ctr, c.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                SDL_SetTextureColorMod(st, or_, og, ob);
                SDL_SetTextureAlphaMod(st, oa);
                SDL_SetTextureBlendMode(st, ob_);
                break;
            }
            case gp::DrawCmd::Kind::Line:
            {
                const SDL_Color col{Uint8(c.r), Uint8(c.g), Uint8(c.b), Uint8(c.a)};
                if (c.thick <= 1)
                    SDL_RenderDrawLine(ren, c.x, c.y, c.x2, c.y2);
                else
                    thickLine(float(c.x), float(c.y), float(c.x2), float(c.y2), float(c.thick), col);
                break;
            }
            case gp::DrawCmd::Kind::Circle:
            {
                const SDL_Color col{Uint8(c.r), Uint8(c.g), Uint8(c.b), Uint8(c.a)};
                const int n = std::clamp(c.w / 2 + 12, 16, 96);
                std::vector<SDL_FPoint> ring;
                for (int i = 0; i < n; ++i)
                {
                    const float a = 6.2831853f * float(i) / float(n);
                    ring.push_back({float(c.x) + std::cos(a) * float(c.w), float(c.y) + std::sin(a) * float(c.w)});
                }
                if (c.fill)
                    fillPoly(ring, col);
                else
                {
                    ring.push_back(ring.front());
                    std::vector<SDL_Point> ip;
                    for (const SDL_FPoint &p : ring)
                        ip.push_back({int(std::lround(p.x)), int(std::lround(p.y))});
                    SDL_RenderDrawLines(ren, ip.data(), int(ip.size()));
                }
                break;
            }
            case gp::DrawCmd::Kind::Poly:
            {
                const SDL_Color col{Uint8(c.r), Uint8(c.g), Uint8(c.b), Uint8(c.a)};
                std::vector<SDL_FPoint> ring;
                for (size_t i = 0; i + 1 < c.pts.size(); i += 2)
                    ring.push_back({c.pts[i], c.pts[i + 1]});
                if (c.fill)
                    fillPoly(ring, col);
                else
                {
                    ring.push_back(ring.front());
                    std::vector<SDL_Point> ip;
                    for (const SDL_FPoint &p : ring)
                        ip.push_back({int(std::lround(p.x)), int(std::lround(p.y))});
                    SDL_RenderDrawLines(ren, ip.data(), int(ip.size()));
                }
                break;
            }
            case gp::DrawCmd::Kind::CanvasBegin:
                if (!inCanvas)
                    inCanvas = beginCanvas(c.str, c.w, c.h);
                break;
            case gp::DrawCmd::Kind::CanvasEnd:
                if (inCanvas)
                {
                    SDL_SetRenderTarget(ren, nullptr);
                    inCanvas = false;
                }
                break;
            case gp::DrawCmd::Kind::Blit:
            {
                int tw = 0, th = 0;
                SDL_Texture *bt = picture(c.str, frameCaptured, tw, th);
                if (!bt || inCanvas && c.str == "@frame")
                    break;
                const float dw = c.w > 0 ? float(c.w) : float(tw), dh = c.h > 0 ? float(c.h) : float(th);
                const SDL_FRect r{float(c.x), float(c.y), dw * c.sx, dh * c.sy};
                const SDL_FPoint ctr = c.hasPivot ? SDL_FPoint{c.pivotX * c.sx, c.pivotY * c.sy} : SDL_FPoint{r.w * 0.5f, r.h * 0.5f};
                SDL_SetTextureColorMod(bt, Uint8(c.tr), Uint8(c.tg), Uint8(c.tb));
                SDL_SetTextureAlphaMod(bt, Uint8(c.ta));
                SDL_SetTextureBlendMode(bt, bt == frameTex_ && c.blend == 0 ? SDL_BLENDMODE_BLEND : blendFor(c.blend));
                SDL_RenderCopyExF(ren, bt, nullptr, &r, double(c.angle), &ctr, c.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                SDL_SetTextureColorMod(bt, 255, 255, 255);
                SDL_SetTextureAlphaMod(bt, 255);
                break;
            }
            case gp::DrawCmd::Kind::Mesh:
            {
                int tw = 0, th = 0;
                SDL_Texture *mt = picture(c.str, frameCaptured, tw, th);
                if (!mt || inCanvas && c.str == "@frame")
                    break;
                std::vector<SDL_Vertex> v(c.pts.size() / 8);
                for (size_t i = 0; i < v.size(); ++i)
                {
                    const float *p = &c.pts[i * 8];
                    v[i] = {{p[0], p[1]}, {Uint8(p[4]), Uint8(p[5]), Uint8(p[6]), Uint8(p[7])}, {p[2], p[3]}};
                }
                SDL_SetTextureColorMod(mt, 255, 255, 255);
                SDL_SetTextureAlphaMod(mt, 255);
                SDL_SetTextureBlendMode(mt, mt == frameTex_ && c.blend == 0 ? SDL_BLENDMODE_BLEND : blendFor(c.blend));
                SDL_RenderGeometry(ren, mt, v.data(), int(v.size()), c.idx.data(), int(c.idx.size()));
                break;
            }
            case gp::DrawCmd::Kind::Text:
            case gp::DrawCmd::Kind::Num:
            {
                std::string str;
                if (c.kind == gp::DrawCmd::Kind::Num)
                {
                    char b[40];
                    if (c.num == std::floor(c.num) && std::fabs(c.num) < 1e15)
                        std::snprintf(b, sizeof b, "%lld", static_cast<long long>(c.num));
                    else
                        std::snprintf(b, sizeof b, "%g", c.num);
                    str = b;
                }
                else
                    str = gp::fillTemplateVars(gp::fmtAll(db.textOr(c.str, "?"), player.name), c.vars);
                const gp::Align al = c.align == 1 ? gp::Align::Center : c.align == 2 ? gp::Align::Right : gp::Align::Left;
                int bx = c.x, bw = c.width;
                if (bw == 0) // no box given: the text is placed around x
                {
                    bw = gp::kLogicalW;
                    bx = c.align == 1 ? c.x - bw / 2 : c.align == 2 ? c.x - bw : c.x;
                }
                (c.font == 3 ? swiftTitle : text).draw(str, bx, c.y, bw, al);
                break;
            }
            }
        }
        if (inCanvas) // (a canvas the list left open: back to the screen)
            SDL_SetRenderTarget(ren, nullptr);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }
};
