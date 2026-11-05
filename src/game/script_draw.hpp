// script_draw.hpp - draws a script's draw list (the DrawCmd list a mod's onDraw / onHud / onPostDraw produced) with SDL.
//
// Shared by the walking world and the front end. What it does itself: rectangles, lines, circles, polygons, canvases (offscreen pictures), blits and meshes of those (and of
// "@frame": the picture as it was when the list began), and sprite transforms. What the caller supplies: how a picture id becomes frames (`sprite`) and how text and numbers are
// written (`text`), since those need the game's fonts and text records.
#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../engine/sprite_cache.hpp" // AnimRef
#include "../mods/script_host.hpp"    // DrawCmd

class ScriptDrawer
{
public:
    explicit ScriptDrawer(SDL_Renderer *ren) : ren_(ren) {}
    ScriptDrawer(const ScriptDrawer &) = delete;
    ScriptDrawer &operator=(const ScriptDrawer &) = delete;
    ~ScriptDrawer()
    {
        for (auto &[name, c] : canvases_)
            if (c.tex)
                SDL_DestroyTexture(c.tex);
        if (frameTex_)
            SDL_DestroyTexture(frameTex_);
    }

    std::function<AnimRef(const std::string &id)> sprite; // a picture id -> its frames (empty AnimRef = unknown)
    std::function<void(const gp::DrawCmd &c)> text;       // writes a Text or Num command
    std::function<void(const gp::DrawCmd &c)> shader;     // runs a Shader command over the picture so far (needs OpenGL; unset = ignored)
    int clearW = 320, clearH = 240;                        // what g.clear fills
    int frameDrawW = 320, frameDrawH = 240;                // the size "@frame" is drawn at by default (the logical screen now)

    void run(const std::vector<gp::DrawCmd> &cmds)
    {
        SDL_Renderer *ren = ren_;
        bool inCanvas = false, frameCaptured = false;
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
                const SDL_Rect all{0, 0, clearW, clearH};
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
                drawSprite(c);
                break;
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
                const int n = std::clamp(c.w / 2 + 12, 16, 96);
                std::vector<SDL_FPoint> ring;
                for (int i = 0; i < n; ++i)
                {
                    const float a = 6.2831853f * float(i) / float(n);
                    ring.push_back({float(c.x) + std::cos(a) * float(c.w), float(c.y) + std::sin(a) * float(c.w)});
                }
                shape(ring, c);
                break;
            }
            case gp::DrawCmd::Kind::Poly:
            {
                std::vector<SDL_FPoint> ring;
                for (size_t i = 0; i + 1 < c.pts.size(); i += 2)
                    ring.push_back({c.pts[i], c.pts[i + 1]});
                shape(ring, c);
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
                if (!bt || (inCanvas && c.str == "@frame"))
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
                if (!mt || (inCanvas && c.str == "@frame"))
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
                if (text)
                    text(c);
                break;
            case gp::DrawCmd::Kind::Shader:
                if (shader && !inCanvas)
                    shader(c);
                break;
            }
        }
        if (inCanvas) // (a canvas the list left open: back to the screen)
            SDL_SetRenderTarget(ren, nullptr);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }

private:
    struct Canvas
    {
        SDL_Texture *tex = nullptr;
        int w = 0, h = 0;
        long used = 0;
    };
    SDL_Renderer *ren_;
    std::map<std::string, Canvas> canvases_;
    long canvasClock_ = 0;
    SDL_Texture *frameTex_ = nullptr;
    int frameW_ = 0, frameH_ = 0;
    std::vector<unsigned char> framePixels_;

    static SDL_BlendMode blendFor(int b) { return b == 1 ? SDL_BLENDMODE_ADD : b == 2 ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND; }

    void drawSprite(const gp::DrawCmd &c)
    {
        const AnimRef A = sprite ? sprite(c.str) : AnimRef();
        if (!A.size())
            return;
        const size_t fi = size_t(c.frame) % A.size();
        const gp::SpriteFrame &f = A.anim->frames[fi];
        SDL_Texture *st = (*A.tex)[fi];
        const bool plain = c.sx == 1.f && c.sy == 1.f && c.angle == 0.f && !c.hasPivot && c.tr == 255 && c.tg == 255 && c.tb == 255 && c.ta == 255 && c.blend == 0;
        if (plain)
        {
            const SDL_FRect r{float(c.x - f.anchorX + f.dx), float(c.y - f.anchorY + f.dy), float(f.w), float(f.h)};
            SDL_RenderCopyExF(ren_, st, nullptr, &r, 0.0, nullptr, c.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
            return;
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
        SDL_RenderCopyExF(ren_, st, nullptr, &r, double(c.angle), &ctr, c.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        SDL_SetTextureColorMod(st, or_, og, ob);
        SDL_SetTextureAlphaMod(st, oa);
        SDL_SetTextureBlendMode(st, ob_);
    }

    // A filled convex shape, or its outline
    void shape(std::vector<SDL_FPoint> ring, const gp::DrawCmd &c)
    {
        const SDL_Color col{Uint8(c.r), Uint8(c.g), Uint8(c.b), Uint8(c.a)};
        if (c.fill)
            fillPoly(ring, col);
        else if (!ring.empty())
        {
            ring.push_back(ring.front());
            std::vector<SDL_Point> ip;
            for (const SDL_FPoint &p : ring)
                ip.push_back({int(std::lround(p.x)), int(std::lround(p.y))});
            SDL_RenderDrawLines(ren_, ip.data(), int(ip.size()));
        }
    }
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
        SDL_RenderGeometry(ren_, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
    }
    void thickLine(float x1, float y1, float x2, float y2, float w, SDL_Color col)
    {
        const float dx = x2 - x1, dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f)
            return;
        const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
        fillPoly({{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}}, col);
    }

    // The window picture as it is now (the whole logical screen), as a texture of its own: "@frame". Read back from the screen, so it is only done when a script asks.
    bool captureFrame()
    {
        int lw = 0, lh = 0;
        SDL_RenderGetLogicalSize(ren_, &lw, &lh);
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren_, &ow, &oh);
        int x0 = 0, y0 = 0, x1 = ow, y1 = oh;
        if (lw > 0 && lh > 0)
        {
            SDL_RenderLogicalToWindow(ren_, 0.f, 0.f, &x0, &y0);
            SDL_RenderLogicalToWindow(ren_, float(lw), float(lh), &x1, &y1);
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
            frameTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            frameW_ = w;
            frameH_ = h;
            if (!frameTex_)
                return false;
        }
        framePixels_.resize(size_t(w) * size_t(h) * 4);
        const SDL_Rect r{x0, y0, w, h};
        if (SDL_RenderReadPixels(ren_, &r, SDL_PIXELFORMAT_ARGB8888, framePixels_.data(), w * 4) != 0)
            return false;
        SDL_UpdateTexture(frameTex_, nullptr, framePixels_.data(), w * 4);
        return true;
    }
    // The texture a blit / mesh names; w, h = its size in the units it is drawn in (a canvas: its pixels; the frame: the logical screen)
    SDL_Texture *picture(const std::string &name, bool frameCaptured, int &w, int &h)
    {
        if (name == "@frame")
        {
            if (!frameCaptured)
                return nullptr;
            w = frameDrawW;
            h = frameDrawH;
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
            c.tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
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
        SDL_SetRenderTarget(ren_, c.tex);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
        SDL_RenderClear(ren_);
        return true;
    }
};
