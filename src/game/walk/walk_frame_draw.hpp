// walk_frame_draw.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of FrameDraw.
// What they need from the rest of the walk session comes in through FrameDrawEnv, under the names they had, so the bodies read as before.
#pragma once
#include "../../engine/render/gl_shader.hpp" // shaders on things of the world (world.shade)
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct FrameDrawEnv
{
    const int &H;
    const int &W;
    double&acc;
    int&animFrame;
    int&animId;
    std::function<AnimRef(const std::string&)> animRefById;
    menubar::MenuBar &bar;
    BarAnim&barAnim;
    std::function<AnimRef(int)> barAnimRef;
    int&barBusy;
    WalkGfx &barGfx;
    WalkBarHost &barHost;
    bool&barInHouse;
    SDL_Texture *&bgTex;
    std::vector<Blast> &blasts;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::Dialogue &dialogue;
    int&digAnimDY;
    int&digStep;
    std::function<void(const gp::DialogueBox &)> drawDialogue;
    std::function<void(const gp::WorldItem &, float, float)> drawEvent;
    std::function<void(const gp::Id &, int, int, float, float)> drawGroundSprite;
    std::function<void(int, int, int, float, float)> drawItemShadow;
    std::function<void(const gp::NpcActor &, float, float)> drawNpc;
    std::function<void(const gp::NpcActor &, float, float)> drawNpcShadow;
    std::function<void(const gp::ObjectDef &, float, float)> drawObject;
    std::function<void(bool, bool)> drawQuestHud;
    std::function<void()> drawRoom;
    std::function<void(const std::vector<gp::DrawCmd> &)> drawScriptCmds;
    std::function<void()> drawScriptObjectives;
    std::function<void(int, float, float, bool, float, float)> drawShadow;
    std::function<void(const gp::Id &, float, float, float, float)> drawShadowSprite;
    std::function<void(int, float, float, int)> drawUiF;
    const std::string &emptyText;
    std::unique_ptr<gp::EncounterSequence>&enc;
    gp::WorldItems &events;
    int &exclaimTicks;
    bool&faceLeft;
    int&facing;
    Fade&fade;
    long long unsigned int &fadeLast;
    int&feedStep;
    int&flipMode;
    const long long unsigned int &freq;
    unsigned int&gameTicks;
    std::function<AnimRef(int)> getAnim;
    std::vector<HoleFill> &holeFills;
    ItemMenu&im;
    const std::vector<gp::ItemDef>&itemDefs;
    ItemGfx &itemGfx;
    std::function<std::string(int)> itemName;
    std::function<int()> itemRows;
    int &itemSlot;
    std::function<std::vector<int>()> itemWindow;
    const int &kEvPal;
    const int &kFrameRate;
    const int &kHoleFillFps;
    const int &kHoleSprite;
    const int (&kWalkAnim)[3];
    std::vector<Cmd>&menuCmds;
    Minigames &minigames;
    std::function<gp::NpcActor*()> nearestNpc;
    std::function<const gp::ShopEvent*()> nearestShopItem;
    std::function<AnimRef(const gp::NpcActor&, int)> npcAnim;
    std::function<const gp::NpcDef*(int)> npcAt;
    std::function<std::pair<float, float>(const gp::NpcActor&)> npcPosF;
    std::vector<gp::NpcActor>&npcs;
    TextDrawer &numbers;
    int&offX;
    int&offY;
    int&overId;
    int&overTicks;
    std::function<int(int)> ownedCount;
    BarAnim &panelAnim;
    bool &panelClosing;
    gp::PendingList &pending;
    std::optional<gp::ExitDef>&pendingExit;
    int &petPal;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    int &playerSpecies;
    gp::Point&pos;
    Pose&pose;
    gp::Point&prevPos;
    int &questionBubble;
    int &questionStep;
    int &questionTicks;
    gp::QuestRuntime &quests;
    std::function<std::string(int, int)> reactAnimId;
    SDL_Renderer *&ren;
    float&renderAlpha;
    gp::Rom&rom;
    std::unique_ptr<RoomView> &room;
    bool &sceneActive;
    gp::ScriptHost &scripts;
    std::function<void(int, int)> sfxFn;
    std::function<int(const gp::Id &)> shadowFor;
    int &shakeDy;
    gp::NpcActor *&showNpc;
    std::function<float(int, int)> smoothAt;
    int &sp;
    gp::StatNotice &statNotice;
    SDL_Texture *&statsBgTex;
    bool &statsOpen;
    int &stinkTicks;
    std::function<std::string(bool)> tabAnimId;
    SDL_Texture *&tex;
    TextDrawer &text;
    int &thoughtIcon;
    int &thoughtTicks;
    std::function<double()> tickHz;
    const gp::TrickLayout &trickLay;
    std::function<std::string(int, int)> uiIdP;
    int&walkTicks;
};

class FrameDraw : public FrameDrawEnv
{
public:
    explicit FrameDraw(FrameDrawEnv env) : FrameDrawEnv(std::move(env)) {}
    ~FrameDraw()
    {
        if (worldTex_)
            SDL_DestroyTexture(worldTex_);
        if (entityTex_)
            SDL_DestroyTexture(entityTex_);
    }
    // The world is drawn into this picture (the size of the logical screen) when the camera is zoomed or turned, and then shown scaled / rotated (see "camera.view": zoom, angle)
    SDL_Texture *worldTex_ = nullptr;
    GlShaderPass entityGl_;            // world.shade: a shader on one thing of the world
    SDL_Texture *entityTex_ = nullptr; // that thing, drawn alone on a transparent picture of kEntityPic pixels, feet at (middle, three quarters down)
    static constexpr int kEntityPic = 384;
    int worldTexW_ = 0, worldTexH_ = 0;
    SDL_Texture *worldTarget(int w, int h)
    {
        if (!worldTex_ || worldTexW_ != w || worldTexH_ != h)
        {
            if (worldTex_)
                SDL_DestroyTexture(worldTex_);
            worldTex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
            worldTexW_ = w;
            worldTexH_ = h;
        }
        return worldTex_;
    }
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }

    // One frame of the walking world: the camera, then everything drawn back to front, then the screen is shown.
    auto drawFrame()
    {
        renderAlpha = smoothCam ? float(std::min(acc * tickHz(), 1.0)) : 1.f;
        const float petFX = smoothAt(prevPos.x, pos.x), petFY = smoothAt(prevPos.y, pos.y); // pet's drawn position
        float zoom = 1.f, angle = 0.f;
        float camX = clampCamF(petFX - W / 2, cur->img.w, W);
        float camY = clampCamF(petFY - gp::tuning().camY - shakeDy, cur->img.h, H); // the belly flop moves the camera's lead
        {
            // the engine system "camera.view" (every frame): the top left corner of the view in the area, as the game worked it out (x, y: the pet's drawn place). A mod changes
            // camX / camY (follow something else, pan), zoom (1 = as it is, 2 = twice as big, 0.5 = twice as far; 0.25 to 4) and angle (degrees, the world turns around the middle of the
            // view; the screens, dialogue and HUD do not), or replaces it and sets them itself; w, h = the area's size, W, H the view's
            gp::SystemCtx cc;
            cc.set("x", petFX).set("y", petFY).set("camX", camX).set("camY", camY).set("w", cur->img.w).set("h", cur->img.h).set("viewW", W).set("viewH", H).set("zoom", 1).set("angle", 0);
            scripts.system("camera.view", cc, [] {});
            camX = float(cc.num("camX"));
            camY = float(cc.num("camY"));
            zoom = std::clamp(float(cc.num("zoom", 1)), 0.25f, 4.f);
            angle = std::fmod(float(cc.num("angle", 0)), 360.f);
            if (!(zoom > 0.f) || room) // (a house room has its own camera)
                zoom = 1.f;
            if (room)
                angle = 0.f;
        }
        const bool worldFx = zoom != 1.f || angle != 0.f;
        // The world is drawn into a picture of (view size / zoom), around the same middle, and that picture is shown over the whole view: zoomed out, more of the area is in it
        const int tw = worldFx ? std::max(8, int(std::ceil(float(W) / zoom))) : W, th = worldFx ? std::max(8, int(std::ceil(float(H) / zoom))) : H;
        if (worldFx)
        {
            camX += float(W - tw) * 0.5f;
            camY += float(H - th) * 0.5f;
        }
        {
            const float spread = angle != 0.f ? 1.5f : 1.f; // (turned, the corners of the view show more of the area)
            const int vw = int(std::ceil(float(tw) * spread)), vh = int(std::ceil(float(th) * spread));
            cur->runtime->updateVisible(int(std::floor(camX - float(vw - tw) * 0.5f)), int(std::floor(camY - float(vh - th) * 0.5f)), vw, vh);
        }

        // ---- DRAW ----
        gp::clearFrame(ren);
        SDL_Texture *worldTarget_ = nullptr;
        if (worldFx && (worldTarget_ = worldTarget(tw, th))) // the world is drawn in its own picture and shown zoomed / turned below
        {
            SDL_SetRenderTarget(ren, worldTarget_);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
        }
        SDL_FRect dst{-camX, -camY, float(cur->img.w), float(cur->img.h)};
        SDL_RenderCopyF(ren, tex, nullptr, &dst);

        for (const gp::Hole &h : events.holes())
            if (h.area == cur->def->legacyId)
                drawGroundSprite(gp::uiSpriteId(kHoleSprite, kEvPal), h.x, h.y, camX, camY);
        if (!holeFills.empty()) // a hole filling in: the dig animation backwards
        {
            const AnimRef FA = animRefById("base:anim/22@28c");
            for (const HoleFill &hf : holeFills)
                if (FA.size())
                {
                    const int fi = std::max(0, int(FA.size()) - 1 - hf.ticks * kHoleFillFps / 30);
                    const gp::SpriteFrame &fs = FA.anim->frames[size_t(fi)];
                    SDL_FRect fr{hf.x - camX - fs.anchorX + fs.dx, hf.y - camY + digAnimDY - fs.anchorY + fs.dy, float(fs.w), float(fs.h)};
                    SDL_RenderCopyF(ren, (*FA.tex)[size_t(fi)], nullptr, &fr);
                }
        }

        // Shadows first, so they lie on the ground under every sprite (layer 0x4000 in the original).
        for (int idx : cur->runtime->visible())
        {
            const gp::ObjectDef *o = cur->runtime->objects()[size_t(idx)];
            const gp::ObjectType *sot = db.objectType(o->type);
            const int mode = sot ? sot->shadow : gp::ObjectType::ShadowDefault;
            if (mode == gp::ObjectType::ShadowNone)
                continue;
            if (mode == gp::ObjectType::ShadowAuto)
            {
                if (sot->hasArt)
                    drawShadow(sot->art.w, float(o->x), float(o->y), false, camX, camY); // the blob DrawShadow picks for a sprite this wide
                continue;
            }
            if (mode == gp::ObjectType::ShadowPicture)
            {
                drawShadowSprite(gp::ObjectType::shadowAnim(o->type), float(o->x), float(o->y), camX, camY);
                continue;
            }
            const int sprite = shadowFor(o->type);
            if (sprite >= 0)
                drawShadowSprite(gp::uiSpriteId(sprite, 0x28c), o->x, o->y + 1, camX, camY);
        }
        for (const gp::NpcActor &n : npcs)
            drawNpcShadow(n, camX, camY);
        for (const gp::WorldItem &e : events.events())
            if (e.area == cur->def->legacyId && e.state == gp::EventState::Visible)
                drawItemShadow(e.item, e.x, e.y, camX, camY);
        float headX = 0, headY = 0;
        auto drawPet = [&]()
        {
            const bool faceRight = !(facing == 2 && faceLeft);
            const bool panda = gp::artOf(sp).facesRight; // the art faces right (the panda's)
            const bool walking = pose == Pose::Move || pose == Pose::Wait;
            bool flip = false;
            const bool negClass = animId == gp::reactId(gp::ReactAnim::Negative, sp, 1, true) ||
                                  animId == gp::reactId(gp::ReactAnim::Negative, sp, 2, true) ||
                                  animId == gp::reactId(gp::ReactAnim::NegIntro, sp, 0, true);
            // Includes the intro/outro art around the happy hop, which the ROM mirrors with the same rule.
            const bool sideClass = animId == gp::artOf(sp).idleSide || animId == gp::artOf(sp).callSide ||
                                   gp::isSideReactArt(sp, animId);
            if (walking)
                flip = facing == 2 && faceLeft;
            else if (animId == gp::artOf(sp).turn180 || animId == gp::artOf(sp).turn90)
                flip = !faceLeft;
            else if (negClass)
                flip = panda ? !faceRight : faceRight; // same rule host.npc.play uses
            else if (sideClass)
                flip = facing == 2 && gp::reactMirror(sp, faceRight);
            const AnimRef A = getAnim(walking ? kWalkAnim[facing] : animId);
            if (A.size() == 0)
                return;
            const int n = int(A.size());
            const int f = walking ? (walkTicks * kFrameRate / 30) % n
                                  : std::min(animFrame, n - 1);
            const gp::SpriteFrame &s = A.anim->frames[f];
            drawShadow(s.w, petFX, petFY, gp::artOf(sp).wideShadow, camX, camY); // DrawEntitySpriteWithShadow

            const int ax = s.anchorX, ay = s.anchorY;
            float x;
            if (!flip)
                x = petFX - camX - ax + s.dx;
            else if (flipMode == 0)
                x = petFX - camX + ax - s.dx - s.w;
            else
            {
                // hypothesis: record word 2 gives the flipped x; the owner x is the sprite's right edge
                const float X = petFX - camX + s.dr;
                x = ax != 0 ? X - s.w : X;
            }
            float y = petFY - camY - ay + s.dy;
            x += offX;
            y += offY;

            SDL_FRect r{x, y, float(s.w), float(s.h)};
            SDL_RenderCopyExF(ren, (*A.tex)[f], nullptr, &r, 0, nullptr,
                              flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
            headX = x + s.w / 2; // top centre of the drawn frame: where the thought bubble hangs (an approximation)
            headY = y;

            // marker 1 (magenta): the pet's position
            // SDL_SetRenderDrawColor(ren, 255, 0, 255, 255);
            // SDL_FRect dot{petFX - camX - 1, petFY - camY - 1, 3.f, 3.f};
            // SDL_RenderFillRectF(ren, &dot);

            // marker 2 (green): bottom-centre of the drawn sprite
            // SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
            // SDL_FRect dot2{x + s.w / 2 - 1, y + s.h - 1, 3.f, 3.f};
            // SDL_RenderFillRectF(ren, &dot2);
        };

        // One y-sorted pass: objects, NPCs and the pet all sort by their feet anchor y. At equal y, objects
        // draw first, then NPCs, then the pet (a guess; the game's tie order is not known).
        struct Item
        {
            int y;
            int kind; // 0 object, 1 npc, 2 pet
            const void *p;
        };
        std::vector<Item> items;
        for (int idx : cur->runtime->visible())
        {
            const gp::ObjectDef *o = cur->runtime->objects()[size_t(idx)];
            // RenderStoryObjectsInYRange draws a type with no marker flag (word 0 of its 0xe9bd record) with the layer bits cleared:
            // bridges and the like lie under everything instead of sorting against the pet.
            const gp::ObjectType *ot = db.objectType(o->type);
            const bool ground = ot && !ot->hasMarker;
            items.push_back({ground ? o->y - 100000 : o->y, 0, o});
        }
        for (const gp::NpcActor &n : npcs)
            items.push_back({n.y(), 1, &n});

        for (const gp::WorldItem &e : events.events())
        {
            if (e.area != cur->def->legacyId)
                continue;
            items.push_back({e.y, 4, &e}); // filter removed: drawEvent already checks the state
        }
        if (overId == 0x16)
            items.push_back({pos.y - 1, 5, nullptr}); // just behind the pet, sorted against objects
        else if (overId == gp::kWashBubbleAnim)
            items.push_back({pos.y + 1, 5, nullptr}); // the wash bubble sits over the pet
        items.push_back({pos.y, 2, nullptr});
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b)
                         { return a.y < b.y; });

        for (const gp::ShopEvent &e : pending.v)
            if (e.type == 3 && e.area == cur->def->legacyId)
                items.push_back({e.y, 3, &e});
        // The pet's facing as the bubble offsets read it: 0 left, 1 right, 2 up or down.
        // The ROM's facing flag follows the walk and snaps to "front" when the pet stops, so bubbles re-centre on the pet.
        const int romDir = (pose == Pose::Move || pose == Pose::Wait) && facing == 2 ? (faceLeft ? 0 : 1) : 2;
        // The stink cloud, the personality bubble, the potion's exclamation and the "?" hang over the pet at its depth: objects
        // in front of the pet cover them (the thought bubbles further down are drawn over everything).
        auto drawPetBubbles = [&]()
        {
            if (!enc && !showNpc)
            {
                int anim = -1;
                int fps = gp::kStinkFps;
                if (player.care[gp::Cleanliness] < gp::kStinkBelow)
                    anim = gp::kStinkAnim; // the stink cloud, looping over the pet
                else
                {
                    anim = gp::personalityBubble(player.personality[gp::Halos], player.personality[gp::Pitchforks], player.personality[gp::Stars]);
                    fps = gp::kPersonalityBubbleFps;
                }
                const AnimRef K = anim >= 0 ? animRefById("base:anim/" + std::to_string(anim) + "@28c") : AnimRef{};
                if (anim >= 0 && K.size())
                {
                    const gp::BubbleOffset bo = gp::overheadOffset(sp, romDir, animId);
                    const int f = (stinkTicks * fps / 30) % int(K.size());
                    const gp::SpriteFrame &bs = K.anim->frames[size_t(f)];
                    SDL_FRect rc{petFX - camX + bo.dx - bs.anchorX + bs.dx, petFY - camY + bo.dy + gp::bubbleLower(sp) - bs.anchorY + bs.dy, float(bs.w), float(bs.h)};
                    SDL_RenderCopyF(ren, (*K.tex)[size_t(f)], nullptr, &rc);
                }
            }
            if (exclaimTicks >= 0) // the exclamation over the head after a potion (bubble 0xb, then 0xc)
            {
                const AnimRef I = animRefById("base:anim/" + std::to_string(gp::kExclaimIntroAnim) + "@28c");
                const AnimRef O = animRefById("base:anim/" + std::to_string(gp::kExclaimOutroAnim) + "@28c");
                const gp::ExclaimView v = gp::exclaimView(exclaimTicks, int(I.size()), int(O.size()));
                if (v.done)
                    exclaimTicks = -1;
                else
                {
                    const AnimRef &A = v.anim == gp::kExclaimOutroAnim ? O : I;
                    if (A.size())
                    {
                        const size_t f = size_t(std::min(v.frame, int(A.size()) - 1));
                        const gp::SpriteFrame &s = A.anim->frames[f];
                        const gp::BubbleOffset bo = gp::overheadOffset(sp, romDir, animId);
                        SDL_FRect rc{petFX - camX + bo.dx - s.anchorX + s.dx, petFY - camY + bo.dy + gp::bubbleLower(sp) - s.anchorY + s.dy, float(s.w), float(s.h)};
                        SDL_RenderCopyF(ren, (*A.tex)[f], nullptr, &rc);
                    }
                }
            }
            if (questionStep) // the "?" over the pet when it has not learned the trick (activity 0x14)
            {
                const AnimRef Q = animRefById("base:anim/" + std::to_string(questionBubble) + "@28c");
                if (Q.size())
                {
                    const size_t f = questionStep == 2 ? Q.size() - 1 : size_t(std::min(questionTicks * gp::kExclaimFps / 30, int(Q.size()) - 1));
                    const gp::SpriteFrame &s = Q.anim->frames[f];
                    const gp::BubbleOffset bo = gp::overheadOffset(sp, romDir, animId);
                    SDL_FRect rc{petFX - camX + bo.dx - s.anchorX + s.dx, petFY - camY + bo.dy + gp::bubbleLower(sp) - s.anchorY + s.dy, float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*Q.tex)[f], nullptr, &rc);
                }
            }
        };
        // A thing of the world drawn through its shader, if a script gave it one (world.shade): it is drawn alone onto a transparent picture with its feet at a fixed place, and the picture
        // is drawn again over that place through the shader (also into the zoomed / turned world's picture). Without OpenGL it is drawn plainly.
        const auto shaded = [&](const std::string &key, float wx, float wy, const std::function<void()> &plain)
        {
            const gp::ScriptHost::EntityShade *es = scripts.anyEntityShade() ? scripts.entityShade(key) : nullptr;
            const std::string *src = es ? db.shader(es->shader) : nullptr;
            if (es && !src)
            {
                static std::set<gp::Id> warned;
                if (warned.insert(es->shader).second)
                    std::fprintf(stderr, "world.shade: no shader '%s'\n", es->shader.c_str());
            }
            if (!src || !entityGl_.available(ren))
            {
                plain();
                return;
            }
            if (!entityTex_)
            {
                entityTex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, kEntityPic, kEntityPic);
                if (entityTex_)
                    SDL_SetTextureBlendMode(entityTex_, SDL_BLENDMODE_BLEND);
            }
            if (!entityTex_)
            {
                plain();
                return;
            }
            SDL_Texture *outer = SDL_GetRenderTarget(ren); // (the zoomed world is a picture: the thing is put back into it)
            const float sx = camX, sy = camY;
            camX = wx - float(kEntityPic) * 0.5f;
            camY = wy - float(kEntityPic) * 0.75f;
            SDL_SetRenderTarget(ren, entityTex_);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
            SDL_RenderClear(ren);
            plain();
            SDL_SetRenderTarget(ren, outer);
            camX = sx;
            camY = sy;
            const SDL_FRect dst{wx - sx - float(kEntityPic) * 0.5f, wy - sy - float(kEntityPic) * 0.75f, float(kEntityPic), float(kEntityPic)};
            entityGl_.applyEntity(ren, es->shader, *src, es->uniforms, float(SDL_GetTicks()) / 1000.f, entityTex_, dst, kEntityPic, kEntityPic, outer != nullptr);
        };
        for (const Item &it : items)
        {
            if (it.kind == 0)
            {
                const gp::ObjectDef &o = *static_cast<const gp::ObjectDef *>(it.p);
                shaded(o.type, float(o.x), float(o.y), [&] { drawObject(o, camX, camY); });
            }
            else if (it.kind == 1)
            {
                const gp::NpcActor &n = *static_cast<const gp::NpcActor *>(it.p);
                shaded(n.def->id, float(n.x()), float(n.y()), [&] { drawNpc(n, camX, camY); });
            }

            else if (it.kind == 3)
            {
                const auto &e = *static_cast<const gp::ShopEvent *>(it.p);
                const AnimRef A = animRefById(uiIdP(itemDefs[e.item].worldSprite, gp::kMenuPalette));
                if (A.size())
                {
                    const gp::SpriteFrame &s = A.anim->frames[0];
                    SDL_FRect r{e.x - camX - s.anchorX + s.dx, e.y - camY - s.anchorY + s.dy, float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
                }
            }
            else if (it.kind == 4)
                drawEvent(*static_cast<const gp::WorldItem *>(it.p), camX, camY);
            else if (it.kind == 5)
            {
                const AnimRef A = animRefById("base:anim/" + std::to_string(overId) + "@28c");
                if (A.size())
                {
                    const int fps = overId == gp::kWashBubbleAnim ? gp::kWashBubbleFps : kBubbleFps;
                    const int f = std::min(overTicks * fps / 30, int(A.size()) - 1);
                    const gp::SpriteFrame &s = A.anim->frames[f];
                    const float ox = petFX - camX, oy = petFY - camY + digAnimDY;
                    SDL_FRect r{ox - s.anchorX + s.dx, oy - s.anchorY + s.dy, float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*A.tex)[f], nullptr, &r);
                }
            }
            else
            {
                shaded("player", petFX, petFY, [&] { drawPet(); });
                drawPetBubbles();
            }
        }
        if (!blasts.empty()) // the explosion, over the scenery
        {
            const AnimRef BA = animRefById("base:anim/" + std::to_string(gp::kBlastAnim) + "@28c");
            for (const Blast &b : blasts)
                if (BA.size())
                {
                    const size_t fi = size_t(std::min(b.ticks * gp::kBlastFps / 30, int(BA.size()) - 1));
                    const gp::SpriteFrame &bs = BA.anim->frames[fi];
                    SDL_FRect br{b.x - camX - bs.anchorX + bs.dx, b.y - camY - bs.anchorY + bs.dy, float(bs.w), float(bs.h)};
                    SDL_RenderCopyF(ren, (*BA.tex)[fi], nullptr, &br);
                }
        }

        if (thoughtIcon >= 0) // the hungry / tired bubble over the pet (DrawActiveBubbles)
        {
            const AnimRef G = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimGrow) + "@28c");
            const AnimRef H = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimHold) + "@28c");
            const gp::ThoughtView v = gp::thoughtView(thoughtTicks, int(G.size()), int(H.size()));
            const AnimRef &A = v.anim == gp::kThoughtAnimHold ? H : G;
            if (!v.done && A.size())
            {
                const gp::SpriteFrame &s = A.anim->frames[size_t(std::min(v.frame, int(A.size()) - 1))];
                const gp::BubbleOffset bo = gp::thoughtOffset(sp, romDir, animId);
                const float bx = petFX - camX + bo.dx, by = petFY - camY + bo.dy + gp::bubbleLower(sp);
                SDL_FRect rc{bx - s.anchorX + s.dx, by - s.anchorY + s.dy, float(s.w), float(s.h)};
                SDL_Texture *bt = (*A.tex)[size_t(std::min(v.frame, int(A.size()) - 1))];
                SDL_SetTextureBlendMode(bt, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(bt, 175); // spawned with layer 0x4000, the shadow layer, which is translucent (as the shadows are)
                SDL_RenderCopyF(ren, bt, nullptr, &rc);
                if (v.icon) // DrawTile(icon, 0x28c, x + 0x20, y - 0x23)
                {
                    const AnimRef I = animRefById(gp::uiSpriteId(thoughtIcon, 0x28c));
                    if (I.size())
                    {
                        const gp::SpriteFrame &t = I.anim->frames[0];
                        // drawn like the sprites: from the tile's own anchor
                        SDL_FRect ic{bx + gp::kThoughtIconDX - t.anchorX + t.dx, by + gp::kThoughtIconDY - t.anchorY + t.dy, float(t.w), float(t.h)};
                        SDL_SetTextureBlendMode((*I.tex)[0], SDL_BLENDMODE_BLEND);
                        SDL_RenderCopyF(ren, (*I.tex)[0], nullptr, &ic);
                    }
                }
            }
        }

        if (!dialogue.open() && !enc && !showNpc)
        {
            // The map marker (RenderRoomObjects, see selector_icon.hpp): one bobbing arrow or ring over what the action
            // button would act on. A ground item first, else the nearest interactable object, else the selected NPC.
            const int area = cur->def->legacyId;
            const gp::ShopEvent *shopT = nearestShopItem();
            const int itemIdx = events.nearestPrompt(area, pos.x, pos.y);
            const gp::ObjectDef *objT = cur->runtime->nearestInteractable(pos.x, pos.y);
            const gp::NpcActor *npcT = nearestNpc();
            const gp::MarkerKind kind = gp::chooseMarker(shopT || itemIdx >= 0, objT != nullptr,
                                                         objT ? objT->legacySlot : -1, area, npcT != nullptr);
            auto spriteH = [&](const gp::Id &id) // GetSpriteHeight of a tile: its first frame
            {
                const AnimRef S = animRefById(id);
                return S.size() ? S.anim->frames[0].h : 0;
            };
            int anim = -1;
            gp::MarkerPos mp{0, 0};
            if (kind == gp::MarkerKind::Item)
            {
                const int item = shopT ? shopT->item : int(events.events()[size_t(itemIdx)].item);
                const int ix = shopT ? shopT->x : events.events()[size_t(itemIdx)].x;
                const int iy = shopT ? shopT->y : events.events()[size_t(itemIdx)].y;
                const int h = item >= 0 && item < int(itemDefs.size())
                                  ? spriteH(uiIdP(itemDefs[size_t(item)].worldSprite, gp::kMenuPalette))
                                  : 0;
                anim = gp::itemMarkerAnim(area);
                mp = gp::itemMarkerPos(area, int(ix - camX), int(iy - camY), h);
            }
            else if (kind == gp::MarkerKind::Npc)
            {
                const gp::NpcActor &n = *npcT;
                const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
                const int h = A.size() ? A.anim->frames[std::min(n.anim.currentFrame, int(A.size()) - 1)].h : 0;
                const auto np = npcPosF(n);
                anim = gp::kMarkerArrowAnim;
                mp = gp::npcMarkerPos(n.def->index, int(np.first - camX), int(np.second - camY), h);
            }
            else if (kind == gp::MarkerKind::Object)
            {
                const gp::ObjectType *t = db.objectType(objT->type);
                const int w = t ? t->art.w : 0, h = t ? t->art.h : 0;
                const int left = int(objT->x - camX) - (t ? t->art.anchorX : 0) + (t ? t->art.dx : 0);
                const int top = int(objT->y - camY) - (t ? t->art.anchorY : 0) + (t ? t->art.dy : 0);
                mp = gp::objectMarkerPos(left, top, w, h);
                anim = gp::objectMarkerOnScreen(mp.y) ? gp::kMarkerObjectAnim : -1;
            }
            if (anim >= 0)
            {
                const AnimRef A = animRefById(reactAnimId(anim, gp::kMarkerPalette));
                if (A.size())
                {
                    const int f = gp::markerFrame(gameTicks, int(A.size()));
                    const gp::SpriteFrame &s = A.anim->frames[size_t(f)];
                    SDL_FRect r{float(mp.x) - s.anchorX + s.dx, float(mp.y) - s.anchorY + s.dy + 1, float(s.w), float(s.h)}; // DrawTile's +1
                    SDL_RenderCopyF(ren, (*A.tex)[size_t(f)], nullptr, &r);
                }
            }
        }
        if (worldTarget_) // the world is done: show its picture zoomed / turned about the middle of the screen
        {
            SDL_SetRenderTarget(ren, nullptr);
            const SDL_FRect dstW{0.f, 0.f, float(W), float(H)};
            SDL_SetTextureBlendMode(worldTarget_, SDL_BLENDMODE_NONE);
            SDL_RenderCopyExF(ren, worldTarget_, nullptr, &dstW, double(angle), nullptr, SDL_FLIP_NONE);
        }
        // Game.hooks().onPostWorld: the world is drawn and nothing of the screens, dialogue or HUD is yet: a shader here shades the world only
        {
            gp::useWholeScreen(ren);
            drawScriptCmds(scripts.drawPostWorld(gp::logicalW()));
        }
        if (W > 320) // 16:9: the 320 x 240 screens (and the HUD, dialogue and bar) sit in the middle of the wide world
        {
            if (room) // a house is a 1152 px room: it fills the whole wide screen and scrolls with the pet
                drawRoom();
            gp::useUiScreen(ren);
            const bool wholeBox = dialogue.open() && dialogue.box->kind == gp::BoxKind::Picture; // (a sign is drawn over the world)
            if (im.open || statsOpen || minigames.active() || sceneActive || wholeBox) // full-screen screens: the world is not behind them
                gp::clearFrame(ren);
        }
        else if (room)
            drawRoom();
        drawQuestHud(barBusy == 0 && feedStep == 0 && digStep == 0 && !im.open && !statsOpen, true);
        if (barBusy == 0 && feedStep == 0 && digStep == 0 && !im.open && !statsOpen && !minigames.active() && !room)
            drawScriptObjectives();
        if (statNotice.visible() && !im.open && !statsOpen) // DrawStatChangeNotification
        {
            const gp::StatNoticeSlot &sl = gp::kStatNoticeSlots[statNotice.idx];
            const int shift = gp::statNoticeShift(statNotice.shown);
            // The panel is drawn 1 px right and down of the ROM's numbers (seen against the real game); the number uses the
            // dialogue font (font 0).
            const float nOff = statNotice.offsetAt(smoothCam, renderAlpha); // glides between the 5 px steps in smooth mode
            drawUiF(gp::kStatNoticeBg, float(gp::kStatNoticeBgX + 1), nOff + float(gp::kStatNoticeBgDY + 1), 0x28c);
            // The icon's coordinates are its lower right corner (it then sits at the panel's left with the number at the
            // right, as in the game); GUESS from "too low and too far right" with the anchor as the origin.
            if (const AnimRef I = animRefById(gp::uiSpriteId(sl.tile, sl.palette)); I.size())
            {
                const gp::SpriteFrame &f = I.anim->frames[0];
                SDL_FRect ir{float(gp::kStatNoticeIconX + shift - f.w + f.dx + gp::kStatNoticeIconNudgeX),
                             nOff - float(f.h) + float(f.dy + gp::kStatNoticeIconNudgeY), float(f.w), float(f.h)};
                SDL_RenderCopyF(ren, (*I.tex)[0], nullptr, &ir);
            }
            text.setYShift(nOff - float(statNotice.offset));
            text.draw(std::to_string(statNotice.shown), 0, statNotice.offset + gp::kStatNoticeTextDY, gp::kStatNoticeTextRight - shift,
                      gp::Align::Right);
            text.setYShift(0.f);
        }
        if (!sceneActive && !room)
            drawScriptCmds(scripts.drawHud(cur->def->id)); // a mod's HUD (Game / Area onHud)
        if (dialogue.open())
            drawDialogue(*dialogue.box);
        minigames.draw(renderAlpha);
        if (sceneActive)
        {
            // a mod's scene: the whole 320 x 240 screen is its own (drawing calls are the script's list; see DrawCmd)
            gp::clearFrame(ren);
            drawScriptCmds(scripts.drawScene());
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            if (const char *shot = std::getenv("GP_SCENE_SHOT")) // debug: save the 20th frame of a scene as a BMP (for tests and screenshots)
            {
                static int sceneFrames = 0;
                const char *at = std::getenv("GP_SCENE_SHOT_FRAME"); // which frame (default the 20th)
                if (++sceneFrames == (at ? std::atoi(at) : 20))
                {
                    int w = 0, h = 0;
                    SDL_GetRendererOutputSize(ren, &w, &h);
                    if (SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888))
                    {
                        if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
                            SDL_SaveBMP(surf, shot);
                        SDL_FreeSurface(surf);
                    }
                }
            }
        }
        if (showStats)
        {
            constexpr int kRow = 16; // raise if letters still overlap
            struct Row
            {
                const char *label;
                int value;
            };
            const Row rows[] = {
                {"HNGR", player.care[gp::Hunger]},
                {"HLTH", player.care[gp::Health]},
                {"???", player.care[gp::Unknown2]},
                {"MOOD", player.care[gp::Happiness]},
                {"SLPY", player.care[gp::Tiredness]},
                {"HYGN", player.care[gp::Cleanliness]},
                {"HALO", player.personality[gp::Halos]},
                {"PTCH", player.personality[gp::Pitchforks]},
                {"STAR", player.personality[gp::Stars]},
                {"BULB", player.personality[gp::Lightbulbs]},
            };
            constexpr int n = int(sizeof rows / sizeof rows[0]);

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
            SDL_Rect bg{4, 4, 110, n * kRow + 8};
            SDL_RenderFillRect(ren, &bg);

            int y = 8;
            for (const Row &r : rows)
            {
                text.draw(r.label, 8, y, 70, gp::Align::Left);
                numbers.draw(std::to_string(r.value), 80, y, 30, gp::Align::Right);
                y += kRow;
            }
        }
        if (im.open)
        {
            SDL_RenderCopy(ren, bgTex, nullptr, nullptr);
            menuCmds.clear();
            gp::drawSlotScene(itemGfx, rom, itemSlot);
            gp::drawMoney(itemGfx, player.money); // the money icon and amount, drawn before the grid like the ROM does
            const std::vector<int> win = itemWindow();
            gp::GridView v;
            v.list = &win;
            v.count = int(win.size());
            v.firstRow = im.firstRow;
            v.selRow = im.selRow;
            v.selCol = im.selCol;
            v.totalRows = itemRows();
            v.category = im.category;
            gp::drawItemGrid(itemGfx, itemDefs, v, ownedCount, itemName, emptyText);
            {
                const gp::ItemTabs &tb = im.tabs;
                if (tb.sliding())
                {
                    const AnimRef A = animRefById(tabAnimId(tb.sub == gp::MenuSub::ToTabs));
                    if (A.size())
                    {
                        const gp::SpriteFrame &f = A.anim->frames[size_t(std::min(tb.animFrame(), int(A.size()) - 1))];
                        const SDL_Rect r{gp::kTabAnimX - f.anchorX + f.dx, gp::kTabAnimY - f.anchorY + f.dy + 1, f.w, f.h};
                        SDL_Texture *t = (*A.tex)[size_t(std::min(tb.animFrame(), int(A.size()) - 1))];
                        menuCmds.push_back({0x3000, [=]
                                            { SDL_RenderCopy(ren, t, nullptr, &r); }});
                    }
                }
                gp::drawTabMarker(itemGfx, rom, tb.inTabs() || tb.sliding() ? tb.tab : im.category, tb.pressed, tb.sub, tb.sliding() ? tb.animFrame() : -1);
            }
            std::stable_sort(menuCmds.begin(), menuCmds.end(), [](const Cmd &a, const Cmd &b)
                             { return a.layer < b.layer; });
            for (Cmd &c : menuCmds)
                c.draw();
            if (dialogue.open()) // a question about the selected item, over the item screen
                drawDialogue(*dialogue.box);
        }
        if (statsOpen)
        {
            gp::clearFrame(ren); // wipe the world drawing
            SDL_SetTextureBlendMode(statsBgTex, SDL_BLENDMODE_NONE); // new: ignore the texture's alpha
            SDL_RenderCopy(ren, statsBgTex, nullptr, nullptr);
            menuCmds.clear();
            auto rs = [&](int a)
            { return readRomString(rom, a); };
            gp::StatsView v;
            v.species = playerSpecies;
            v.petPalette = petPal;
            v.name = player.name;
            v.tricksKnown = player.tricksKnown;
            v.healthy = player.isHealthy();
            for (int i = 0; i < 4; ++i)
                v.personality[i] = player.personality[i];
            v.hunger = gp::statLabel(rom, gp::kHungerLabels, player.care[gp::Hunger], rs);
            v.happiness = gp::statLabel(rom, gp::kHappinessLabels, player.care[gp::Happiness], rs);
            v.cleanliness = gp::statLabel(rom, gp::kCleanlinessLabels, player.care[gp::Cleanliness], rs);
            v.health = gp::statLabel(rom, gp::kHealthLabels, player.care[gp::Health], rs);
            v.tiredness = gp::statLabel(rom, gp::kTirednessLabels, player.care[gp::Tiredness], rs);
            v.illness = gp::kIllnessNames[std::clamp(player.illnessId, 0, 9)];

            std::string qt;
            const int qn = quests.statusText(qt);
            v.questText = qt;
            v.questPortrait = -1;
            if (const gp::NpcDef *d = npcAt(qn); d && d->portraitSprite > 0 && d->portraitSprite != 0xffff)
            {
                v.questPortrait = d->portraitSprite;
                v.questPortraitPal = d->portraitPalette;
            }
            gp::drawPetStats(itemGfx, rom, v, "Sick");
            std::stable_sort(menuCmds.begin(), menuCmds.end(), [](const Cmd &a, const Cmd &b)
                             { return a.layer < b.layer; });
            for (Cmd &c : menuCmds)
                c.draw();
        }
        if (barAnim.visible && barBusy != 4 && !im.open && !statsOpen) // (the bar is gone once the panel is up)
        {
            const AnimRef A = barAnimRef(barAnim.id);
            if (A.size())
            {
                const int f = std::min(barAnim.frame, int(A.size()) - 1);
                const gp::SpriteFrame &s = A.anim->frames[f];
                SDL_Rect r{0 - s.anchorX + s.dx, 0xb4 - s.anchorY + s.dy, s.w, s.h}; // screen space, no camera
                SDL_RenderCopy(ren, (*A.tex)[f], nullptr, &r);
            }
        }
        if (panelAnim.visible && !im.open && !statsOpen)
        {
            const AnimRef P = barAnimRef(panelAnim.id);
            if (P.size())
            {
                const int f = std::min(panelAnim.frame, int(P.size()) - 1);
                const gp::SpriteFrame &s = P.anim->frames[f];
                SDL_Rect r{gp::kTrickPanelX - s.anchorX + s.dx, gp::kTrickPanelY - s.anchorY + s.dy, s.w, s.h};
                SDL_RenderCopy(ren, (*P.tex)[f], nullptr, &r);
            }
        }
        if (barBusy == 4 && !panelClosing && !im.open && !statsOpen)
            for (int slot = 0; slot < gp::kTrickSlots; ++slot)
            {
                const gp::TrickIcon ic = gp::trickIcon(sp, slot, player.trickMask, trickLay);
                barGfx.tile(ic.sprite, 0x28c, ic.x, ic.y);
            }
        if (bar.isOpen() && barBusy == 2)
            gp::drawBarCursor(barGfx, rom, barInHouse, bar.selected(), barHost.pressed);
        static bool prevFullUi = false; // or a local declared before the loop
        const bool fullUi = im.open;
        if (fullUi != prevFullUi)
        {
            prevFullUi = fullUi;
            if (!pendingExit)
            {
                fade.a = 0.8; // quick fade in from near-black on both open and close
                fade.go(0.0, 6.0 / 30);
            }
        }
        gp::useWholeScreen(ren); // the whole window, side bands included
        drawScriptCmds(scripts.drawPost(gp::logicalW())); // Game.hooks().onPostDraw: over the finished frame
        {
            const Uint64 t = SDL_GetPerformanceCounter();
            fade.step(std::min(double(t - fadeLast) / double(freq), 1.0 / 20)); // clamp so loadArea hitches don't skip the fade
            fadeLast = t;
            if (fade.a > 0.0)
            {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, Uint8(255.0 * std::min(fade.a, 1.0)));
                SDL_RenderFillRect(ren, nullptr);
            }
        }
        if (const char *shot = std::getenv("GP_WALK_SHOT")) // debug: save frame GP_WALK_SHOT_FRAME (default the 200th) of the walking world as a BMP
        {
            static int walkFrames = 0;
            const char *at = std::getenv("GP_WALK_SHOT_FRAME");
            if (++walkFrames == (at ? std::atoi(at) : 200))
            {
                int w = 0, h = 0;
                SDL_GetRendererOutputSize(ren, &w, &h);
                if (SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888))
                {
                    if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
                        SDL_SaveBMP(surf, shot);
                    SDL_FreeSurface(surf);
                }
            }
        }
        SDL_RenderPresent(ren);
    }
};
