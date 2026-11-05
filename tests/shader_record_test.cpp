// shader_record_test.cpp - the `shader` record: a mod's fragment shader file, its checks and its storage.
#include "../src/mods/mod_loader.hpp"
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

static void writeFile(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

static std::vector<std::string> load(const std::string &name, const std::string &record, const std::string &shaderText, bool writeShader = true)
{
    const fs::path root = fs::temp_directory_path() / ("gp_shader_" + name);
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "q" / "manifest.json", R"({"id":"q","version":"1.0","game_api":"1"})");
    if (writeShader)
        writeFile(root / "q" / "shaders" / "a.frag", shaderText);
    writeFile(root / "q" / "data" / "a.json", "[" + record + "]");
    ContentDB db;
    const auto errs = importMods(db, root).errors;
    CHECK(errs.empty() == (db.shader("q:shader/a") != nullptr));
    if (errs.empty())
    {
        const std::string *s = db.shader("q:shader/a");
        CHECK(s && *s == shaderText);
    }
    fs::remove_all(root, ec);
    return errs;
}

int main()
{
    const std::string good = "void main() { gl_FragColor = frame(uv); }\n";
    CHECK(load("good", R"({"type":"shader","mode":"add","id":"q:shader/a","file":"shaders/a.frag"})", good).empty());
    CHECK(!load("noversion", R"({"type":"shader","mode":"add","id":"q:shader/a","file":"shaders/a.frag"})", "#version 120\nvoid main() { gl_FragColor = frame(uv); }").empty()); // the game adds it
    CHECK(!load("nomain", R"({"type":"shader","mode":"add","id":"q:shader/a","file":"shaders/a.frag"})", "float x;").empty());
    CHECK(!load("empty", R"({"type":"shader","mode":"add","id":"q:shader/a","file":"shaders/a.frag"})", "").empty());
    CHECK(!load("nofile", R"({"type":"shader","mode":"add","id":"q:shader/a","file":"shaders/a.frag"})", "", false).empty());
    CHECK(!load("nofield", R"({"type":"shader","mode":"add","id":"q:shader/a"})", good).empty());
    CHECK(!load("badid", R"({"type":"shader","mode":"add","id":"other:shader/a","file":"shaders/a.frag"})", good).empty());
    CHECK(!load("patch", R"({"type":"shader","mode":"patch","id":"q:shader/a","file":"shaders/a.frag"})", good).empty());
    CHECK(!load("extra", R"({"type":"shader","mode":"add","id":"q:shader/a","file":"shaders/a.frag","bogus":1})", good).empty());
    if (g_fail)
    {
        std::cerr << "shader_record_test: " << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "shader_record_test: all passed\n";
    return 0;
}
