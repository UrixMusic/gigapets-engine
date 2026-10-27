// sprite_cache.hpp - SDL textures for db animations and object art, built on first use and freed together.
// Moved out of walk.cpp's runWalk (it was two maps, three lambdas and a cleanup loop at the end).
#pragma once
#include <SDL2/SDL.h>
#include <map>
#include <vector>

#include "../content/db.hpp"

// A db animation plus its SDL textures (one per frame, same order).
struct AnimRef
{
    const gp::Anim *anim = nullptr;
    const std::vector<SDL_Texture *> *tex = nullptr;
    size_t size() const { return tex ? tex->size() : 0; }
};

class SpriteCache
{
public:
    // The db is read on every lookup, so anims imported after construction (importAnim) are picked up.
    SpriteCache(SDL_Renderer *ren, const gp::ContentDB &db) : ren_(ren), db_(db) {}
    ~SpriteCache() { clear(); }
    SpriteCache(const SpriteCache &) = delete; // owns textures
    SpriteCache &operator=(const SpriteCache &) = delete;

    // Textures of one anim (coloured frames come from the db), or nullptr if the db doesn't know the id.
    // The pointer stays valid until clear(); the map never moves its vectors.
    const std::vector<SDL_Texture *> *animTextures(const gp::Id &animId)
    {
        const gp::Anim *a = db_.anim(animId);
        if (!a)
            return nullptr;
        auto it = anim_.find(animId);
        if (it == anim_.end())
        {
            std::vector<SDL_Texture *> v;
            for (const gp::SpriteFrame &f : a->frames)
            {
                SDL_Texture *t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, f.w, f.h);
                SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.w * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); // transparent pixels stay transparent
                v.push_back(t);
            }
            it = anim_.emplace(animId, std::move(v)).first;
        }
        return &it->second;
    }

    // The db anim and its textures together; empty (size() == 0) if the id is unknown.
    AnimRef animRef(const gp::Id &id)
    {
        const gp::Anim *a = db_.anim(id);
        return a ? AnimRef{a, animTextures(id)} : AnimRef{};
    }

    // Object art as one texture (nullptr if the type has none).
    SDL_Texture *objTexture(const gp::ObjectType &t)
    {
        auto it = obj_.find(t.id);
        if (it == obj_.end())
        {
            SDL_Texture *tx = nullptr;
            if (t.hasArt && t.art.w > 0 && t.art.h > 0)
            {
                tx = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.art.w, t.art.h);
                SDL_UpdateTexture(tx, nullptr, t.art.rgba.data(), t.art.w * 4);
                SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
            }
            it = obj_.emplace(t.id, tx).first;
        }
        return it->second;
    }

    // Objects are drawn as sprites (not baked into the background) so they can sort against NPCs and the pet.
    void drawObject(const gp::ObjectDef &o, float camX, float camY)
    {
        const gp::ObjectType *t = db_.objectType(o.type);
        SDL_Texture *tx = t ? objTexture(*t) : nullptr;
        if (!tx)
            return;
        SDL_FRect r{o.x - camX - t->art.anchorX + t->art.dx, o.y - camY - t->art.anchorY + t->art.dy, float(t->art.w), float(t->art.h)};
        SDL_RenderCopyF(ren_, tx, nullptr, &r);
    }

    void clear()
    {
        for (auto &kv : anim_)
            for (SDL_Texture *t : kv.second)
                SDL_DestroyTexture(t);
        for (auto &kv : obj_)
            if (kv.second)
                SDL_DestroyTexture(kv.second);
        anim_.clear();
        obj_.clear();
    }

private:
    SDL_Renderer *ren_;
    const gp::ContentDB &db_;
    std::map<gp::Id, std::vector<SDL_Texture *>> anim_;
    std::map<gp::Id, SDL_Texture *> obj_;
};