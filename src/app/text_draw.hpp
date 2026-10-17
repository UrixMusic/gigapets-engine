#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include "../content/records.hpp"
#include "../engine/text_layout.hpp"

class TextDrawer {
public:
    TextDrawer(SDL_Renderer* r, const gp::FontDef* f) : ren_(r), font_(f) {
        if (!font_) return;
        for (const gp::SpriteFrame& g : font_->glyph) {
            SDL_Texture* t = nullptr;
            if (g.w > 0 && g.h > 0) {
                t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, g.w, g.h);
                SDL_UpdateTexture(t, nullptr, g.rgba.data(), g.w * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            }
            tex_.push_back(t);
        }
    }
    ~TextDrawer() { for (SDL_Texture* t : tex_) if (t) SDL_DestroyTexture(t); }
    TextDrawer(const TextDrawer&) = delete;
    TextDrawer& operator=(const TextDrawer&) = delete;

    int lineHeight() const { return font_ ? font_->lineHeight : 12; }

    int advance(unsigned char c) const {
        const int k = c - 0x21;
        if (font_ && k >= 0 && k < 0x5e && k < int(font_->adv.size())) return font_->adv[k];
        return font_ ? font_->space : 10;       // space and anything outside 0x21..0x7e
    }

    gp::TextLayout layout(const std::string& s, int x, int y, int width, gp::Align a = gp::Align::Left,
                          int indent = 0, int indentH = 0) const {
        return gp::layoutText(s, x, y, width, a, lineHeight(),
                              [&](unsigned char c) { return advance(c); }, indent, indentH);
    }

    // DrawTextBox / DrawFormattedText. Returns the height used.
    int draw(const std::string& s, int x, int y, int width, gp::Align a = gp::Align::Left,
             int indent = 0, int indentH = 0) const {
        const gp::TextLayout L = layout(s, x, y, width, a, indent, indentH);
        for (const gp::LaidLine& ln : L.lines) drawLine(ln.text, ln.x, ln.y);
        return L.height;
    }

private:
    void drawLine(const std::string& s, int x, int y) const {
        int pen = 0;
        for (unsigned char c : s) {
            const int k = c - 0x21;
            if (font_ && k >= 0 && k < 0x5e && k < int(tex_.size()) && tex_[k]) {
                const gp::SpriteFrame& g = font_->glyph[k];
                SDL_Rect r{x + pen + g.dx, y + g.dy, g.w, g.h};   // the game draws glyphs at the pen position + the sprite's offsets
                SDL_RenderCopy(ren_, tex_[k], nullptr, &r);
            }
            pen += advance(c);
        }
    }
    SDL_Renderer* ren_;
    const gp::FontDef* font_;
    std::vector<SDL_Texture*> tex_;
};