// walk_bar_gfx.hpp - the menu bar's host, the two drawing adapters and the script NPC bookkeeping of a walk session (they were local structs of runWalk()).
#pragma once

struct WalkBarHost : menubar::MenuBarHost
{
    int &busy;
    bool &inHouse;
    int &slot;
    std::function<void(int)> play;
    bool pressed = false;
    WalkBarHost(int &b, bool &h, int &s, std::function<void(int)> p)
        : busy(b), inHouse(h), slot(s), play(std::move(p)) {}
    std::function<void(int)> onState;
    int category = 0;
    void switchGameState(int s) override
    {
        if (onState)
            onState(s);
        else
            std::printf("bar: SwitchGameState(0x%x)\n", s);
    }
    std::function<bool(int, int, int, int)> onActivity; // true = handled (0x27: wash)
    void startPetActivity(int id, int prm, int x, int y) override
    {
        if (onActivity && onActivity(id, prm, x, y))
            return;
        std::printf("bar: StartPetActivity(0x%x, %d) at (%d,%d)\n", id, prm, x, y);
    }
    int rollPetPrizeId(int kind) override
    {
        std::printf("bar: prize roll %d\n", kind);
        return 0;
    }
    void setPetBusy(int v) override { busy = v; }
    void setLoopState(int v) override { std::printf("bar: loopState=%d\n", v); }
    void setItemMenuMode(int) override {}
    void setItemMenuCategory(int c) override { category = c; }
    std::function<void(int)> onDialogue;
    void startDialogue(int id) override
    {
        if (onDialogue)
            onDialogue(id);
        else
            std::printf("bar: dialogue 0x%x\n", id);
    }
    std::function<void()> onTricks;
    void openTricksPanel() override
    {
        if (onTricks)
            onTricks();
    }
    int currentSaveSlot() override { return slot; }
    void requestRedraw() override {}
    void startCloseAnim() override { play(inHouse ? 1 : 3); } // CloseMenuBar()
    void playOpenAnim(int) override {}
    void playSound(int id, int vol) override { gp::audio().playSfx(id, vol); }
    void moveCursor(int ev, int &sel, int count) override // still a guess
    {
        sel = (sel + (ev == 1 ? 1 : count - 1)) % count;
    }
};

struct WalkGfx : gp::Gfx
{
    std::function<void(int, int, int, int)> tile;
    void drawTile(int s, int p, int x, int y, int, int) override { tile(s, p, x, y); }
    void drawTextBox(int, int, int, int, const std::string &, int, int, int) override {}
};

struct ItemGfx : gp::Gfx
{
    std::function<void(int, int, int, int, int)> tile;
    std::function<void(int, int, int, const std::string &, int, int, int)> box;
    std::function<int(int)> spriteW; // new
    void drawTile(int s, int p, int x, int y, int layer, int) override { tile(s, p, x, y, layer); }
    void drawTextBox(int st, int, int x, int y, const std::string &t, int w, int m, int l) override { box(st, x, y, t, w, m, l); }
    int getSpriteWidth(int s) override { return spriteW ? spriteW(s) : 0; } // new
};

// What scripts changed about the NPCs of the area the pet is in (npc.spawn / move / remove). loadNpcs is run again at times
// (a quest moving on) and must not undo it; leaving the area forgets it. `defs` keeps the records of moved and spawned
// NPCs, actors point into it, so it is only cleared together with the actors.
struct ScriptNpcState
{
    gp::Id area;
    std::deque<gp::NpcDef> defs;
    std::set<gp::Id> removed;                                 // this area's own NPCs a script removed
    std::map<gp::Id, gp::IPoint> moved;                       // ... or moved
    std::vector<std::pair<gp::Id, gp::IPoint>> spawned;       // NPCs a script put here
    struct Op
    {
        enum Kind { Spawn, Move, Remove, Walk, Face, Play, Stop, Release } kind;
        gp::Id id;
        gp::IPoint at;
        std::string str; // Play: the anim ('' = stop playing); Face: at.x is the facing
    };
    std::vector<Op> pending;                                  // changes to the live actors, made at the start of a safe tick
};
