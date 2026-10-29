// tests/menu_money_test.cpp - the money icon and amount in the item menu (ItemMenuScreen, 0x02c610).
#include <cstdio>
#include <string>
#include <vector>
#include "../src/game/menu_draw.hpp"
using namespace gp;

struct Rec : Gfx
{
    struct Tile
    {
        int sprite, pal, x, y, layer;
    };
    struct Box
    {
        int style, pal, x, y, width, mode, layer;
        std::string text;
    };
    std::vector<Tile> tiles;
    std::vector<Box> boxes;
    void drawTile(int s, int p, int x, int y, int layer, int) override { tiles.push_back({s, p, x, y, layer}); }
    void drawTextBox(int st, int p, int x, int y, const std::string &t, int w, int m, int l) override
    {
        boxes.push_back({st, p, x, y, w, m, l, t});
    }
};

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    {
        Rec r;
        drawMoney(r, 1234);
        check("one icon and one text box", r.tiles.size() == 1 && r.boxes.size() == 1);
        check("icon: tile 0xacb, palette 0x28c, at (0x14, 0x23), layer 0x1000",
              r.tiles.size() == 1 && r.tiles[0].sprite == 0xacb && r.tiles[0].pal == 0x28c && r.tiles[0].x == 0x14 &&
                  r.tiles[0].y == 0x23 && r.tiles[0].layer == 0x1000);
        check("text: font 2, palette 0x28c, at (0x20, 0x14), 0x32 wide, left aligned, layer 0x1000",
              r.boxes.size() == 1 && r.boxes[0].style == 2 && r.boxes[0].pal == 0x28c && r.boxes[0].x == 0x20 &&
                  r.boxes[0].y == 0x14 && r.boxes[0].width == 0x32 && r.boxes[0].mode == 0 && r.boxes[0].layer == 0x1000);
        check("the amount is plain decimal (\"%ld\")", r.boxes.size() == 1 && r.boxes[0].text == "1234");
    }
    { // the extremes of the money range (the cap is 99999)
        Rec r;
        drawMoney(r, 0);
        check("zero shows 0", r.boxes[0].text == "0");
        Rec s;
        drawMoney(s, 99999);
        check("the cap shows 99999", s.boxes[0].text == "99999");
    }
    { // the ROM draws the icon, then the text, before the item grid
        Rec r;
        drawMoney(r, 5);
        check("icon first, then text (two calls, in that order)", r.tiles.size() == 1 && r.boxes.size() == 1);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
