// gl_shader.hpp - fragment shaders from mods, run over the finished frame (Stage B of the graphics work).
//
// SDL2's renderer has no shaders, but when it runs on its "opengl" driver its GL context is current between SDL_RenderFlush() calls, so a pass can be done with plain GL:
// the frame is copied out of the back buffer into a texture on the GPU (no read back), and drawn again over the same place through a mod's fragment shader. The vertex shader and
// a prelude are ours; a mod writes the rest (GLSL 1.20):
//
//     // mods/<id>/shaders/wave.frag      (registered with { "type": "shader", "mode": "add", "id": "<id>:shader/wave", "file": "shaders/wave.frag" })
//     void main() { vec2 p = uv + vec2(sin(uv.y * 20.0 + time) * 0.004, 0.0); gl_FragColor = frame(p); }
//
// What the prelude gives it: `uv` (0..1 over the screen, y = 0 at the TOP), `frame(vec2 p)` (the picture so far, sampled at p), `resolution` (the logical screen's size), `time`
// (seconds), and whatever float uniforms the script passes (g.shader(id, { strength = 0.5, tint = { 1, 0.5, 0.5 } }): a number, or a list of 2..4 numbers, declare them in the shader as
// `uniform float strength;` / `uniform vec3 tint;`). Without the "opengl" driver (or if the shader does not compile) a pass does nothing and says so once.
#pragma once
#include <SDL2/SDL.h>
#ifndef NOMINMAX
#define NOMINMAX // (the OpenGL header pulls in windows.h, whose min / max / near / far macros break the rest of the game)
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <SDL2/SDL_opengl.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

class GlShaderPass
{
public:
    using Uniforms = std::vector<std::pair<std::string, std::vector<float>>>;

    ~GlShaderPass()
    {
        if (!ok_)
            return;
        for (auto &[id, p] : programs_)
            if (p.program && glDeleteProgram_)
                glDeleteProgram_(p.program);
        if (tex_)
            glDeleteTextures(1, &tex_);
    }

    // Does the renderer run on OpenGL, and can the shader functions be had? (Asked once.)
    bool available(SDL_Renderer *ren)
    {
        if (checked_)
            return ok_;
        checked_ = true;
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(ren, &info) != 0 || !info.name || std::strcmp(info.name, "opengl") != 0)
        {
            std::fprintf(stderr, "shader: the renderer is '%s', not 'opengl': shaders are skipped (set the renderer to opengl to use them)\n", info.name ? info.name : "?");
            return false;
        }
#define GP_GL(name, type)                                                       \
    name##_ = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));             \
    if (!name##_)                                                               \
    {                                                                           \
        std::fprintf(stderr, "shader: OpenGL has no %s: shaders are skipped\n", #name); \
        return false;                                                           \
    }
        GP_GL(glCreateShader, PFNGLCREATESHADERPROC)
        GP_GL(glShaderSource, PFNGLSHADERSOURCEPROC)
        GP_GL(glCompileShader, PFNGLCOMPILESHADERPROC)
        GP_GL(glGetShaderiv, PFNGLGETSHADERIVPROC)
        GP_GL(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)
        GP_GL(glDeleteShader, PFNGLDELETESHADERPROC)
        GP_GL(glCreateProgram, PFNGLCREATEPROGRAMPROC)
        GP_GL(glAttachShader, PFNGLATTACHSHADERPROC)
        GP_GL(glLinkProgram, PFNGLLINKPROGRAMPROC)
        GP_GL(glGetProgramiv, PFNGLGETPROGRAMIVPROC)
        GP_GL(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)
        GP_GL(glDeleteProgram, PFNGLDELETEPROGRAMPROC)
        GP_GL(glUseProgram, PFNGLUSEPROGRAMPROC)
        GP_GL(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)
        GP_GL(glUniform1f, PFNGLUNIFORM1FPROC)
        GP_GL(glUniform2f, PFNGLUNIFORM2FPROC)
        GP_GL(glUniform3f, PFNGLUNIFORM3FPROC)
        GP_GL(glUniform4f, PFNGLUNIFORM4FPROC)
        GP_GL(glUniform1i, PFNGLUNIFORM1IPROC)
#undef GP_GL
        ok_ = true;
        return true;
    }

    // Runs `source` (the mod's fragment shader body) over the frame. `logical` is the screen's logical size (what `resolution` is).
    void apply(SDL_Renderer *ren, const std::string &id, const std::string &source, const Uniforms &uniforms, float seconds, int logicalW, int logicalH)
    {
        if (!available(ren))
            return;
        Program &p = program(id, source);
        if (!p.program)
            return;
        SDL_RenderFlush(ren); // everything SDL has queued is drawn: the back buffer is the frame so far
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        int lw = 0, lh = 0;
        SDL_RenderGetLogicalSize(ren, &lw, &lh);
        int x0 = 0, y0 = 0, x1 = ow, y1 = oh;
        if (lw > 0 && lh > 0)
        {
            SDL_RenderLogicalToWindow(ren, 0.f, 0.f, &x0, &y0);
            SDL_RenderLogicalToWindow(ren, float(lw), float(lh), &x1, &y1);
        }
        x0 = x0 < 0 ? 0 : x0;
        y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > ow ? ow : x1;
        y1 = y1 > oh ? oh : y1;
        const int w = x1 - x0, h = y1 - y0;
        if (w < 1 || h < 1)
            return;
        const int glY = oh - y1; // GL's rows count from the bottom

        glPushAttrib(GL_ALL_ATTRIB_BITS);
        if (!tex_)
            glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        if (texW_ != w || texH_ != h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            texW_ = w;
            texH_ = h;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glReadBuffer(GL_BACK);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x0, glY, w, h); // GPU to GPU

        glViewport(x0, glY, w, h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        glUseProgram_(p.program);
        glUniform1i_(loc(p, "tex"), 0);
        glUniform2f_(loc(p, "resolution"), float(logicalW), float(logicalH));
        glUniform1f_(loc(p, "time"), seconds);
        for (const auto &[name, v] : uniforms)
        {
            const GLint l = loc(p, name);
            if (l < 0)
                continue;
            if (v.size() == 1)
                glUniform1f_(l, v[0]);
            else if (v.size() == 2)
                glUniform2f_(l, v[0], v[1]);
            else if (v.size() == 3)
                glUniform3f_(l, v[0], v[1], v[2]);
            else if (v.size() == 4)
                glUniform4f_(l, v[0], v[1], v[2], v[3]);
        }
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0.f, 0.f);
        glVertex2f(-1.f, -1.f);
        glTexCoord2f(1.f, 0.f);
        glVertex2f(1.f, -1.f);
        glTexCoord2f(0.f, 1.f);
        glVertex2f(-1.f, 1.f);
        glTexCoord2f(1.f, 1.f);
        glVertex2f(1.f, 1.f);
        glEnd();
        glUseProgram_(0);
        glPopAttrib();
    }

private:
    struct Program
    {
        unsigned program = 0;
        bool tried = false;
        std::map<std::string, GLint> locs;
    };
    bool checked_ = false, ok_ = false;
    GLuint tex_ = 0;
    int texW_ = 0, texH_ = 0;
    std::map<std::string, Program> programs_;

    PFNGLCREATESHADERPROC glCreateShader_ = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource_ = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader_ = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv_ = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader_ = nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram_ = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader_ = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram_ = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv_ = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram_ = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram_ = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ = nullptr;
    PFNGLUNIFORM1FPROC glUniform1f_ = nullptr;
    PFNGLUNIFORM2FPROC glUniform2f_ = nullptr;
    PFNGLUNIFORM3FPROC glUniform3f_ = nullptr;
    PFNGLUNIFORM4FPROC glUniform4f_ = nullptr;
    PFNGLUNIFORM1IPROC glUniform1i_ = nullptr;

    GLint loc(Program &p, const std::string &name)
    {
        auto it = p.locs.find(name);
        if (it != p.locs.end())
            return it->second;
        const GLint l = glGetUniformLocation_(p.program, name.c_str());
        p.locs[name] = l;
        return l;
    }

    GLuint compile(GLenum type, const std::string &src, const std::string &what)
    {
        const GLuint s = glCreateShader_(type);
        const char *c = src.c_str();
        glShaderSource_(s, 1, &c, nullptr);
        glCompileShader_(s);
        GLint ok = 0;
        glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[2048] = {0};
            glGetShaderInfoLog_(s, sizeof log - 1, nullptr, log);
            std::fprintf(stderr, "shader %s: does not compile:\n%s\n", what.c_str(), log);
            glDeleteShader_(s);
            return 0;
        }
        return s;
    }

    Program &program(const std::string &id, const std::string &body)
    {
        Program &p = programs_[id];
        if (p.tried)
            return p;
        p.tried = true;
        static const char *vertex =
            "#version 120\n"
            "varying vec2 uv;\n"
            "void main() { gl_Position = gl_Vertex; uv = vec2(gl_MultiTexCoord0.x, 1.0 - gl_MultiTexCoord0.y); }\n";
        const std::string prelude =
            "#version 120\n"
            "uniform sampler2D tex;\n"
            "uniform vec2 resolution;\n"
            "uniform float time;\n"
            "varying vec2 uv;\n"
            "vec4 frame(vec2 p) { return texture2D(tex, vec2(p.x, 1.0 - p.y)); }\n"
            "#line 1\n";
        const GLuint vs = compile(GL_VERTEX_SHADER, vertex, id + " (vertex)");
        const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, prelude + body, id) : 0;
        if (!vs || !fs)
        {
            if (vs)
                glDeleteShader_(vs);
            return p;
        }
        const GLuint prog = glCreateProgram_();
        glAttachShader_(prog, vs);
        glAttachShader_(prog, fs);
        glLinkProgram_(prog);
        glDeleteShader_(vs);
        glDeleteShader_(fs);
        GLint linked = 0;
        glGetProgramiv_(prog, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char log[2048] = {0};
            glGetProgramInfoLog_(prog, sizeof log - 1, nullptr, log);
            std::fprintf(stderr, "shader %s: does not link:\n%s\n", id.c_str(), log);
            glDeleteProgram_(prog);
            return p;
        }
        p.program = prog;
        return p;
    }
};
