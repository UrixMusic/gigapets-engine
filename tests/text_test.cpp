// tests/text_test.cpp
#include <cstdio>
#include "text.hpp"   // as in your other tests: needs -Isrc/loaders (and ../content/db.hpp resolves from there)

static int fails = 0;
static void check(const char* what, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    std::printf("%s  %s: \"%s\"%s\n", ok ? "PASS" : "FAIL", what, got.c_str(),
                ok ? "" : ("   (wanted \"" + want + "\")").c_str());
    if (!ok) fails++;
}
static void dumpWords(const gp::Rom& rom, uint32_t at, int n) {
    std::printf("      words at 0x%x:", at);
    for (int i = 0; i < n; i++) std::printf(" %04x", rom.u16(at + i));
    std::printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: text_test <rom>\n"); return 1; }
    gp::Rom rom(argv[1]);

    // 1. wide literals: addresses come from Ghidra's own labels (u_..._00d8e0)
    check("wide 0xd8e0", gp::readWideText(rom, 0xd8e0), "I have a gift for you!");
    check("wide 0xd95d", gp::readWideText(rom, 0xd95d), "%s's Stats");
    if (fails) dumpWords(rom, 0xd8e0, 12);

    // 2. packed text: find the table, then check the names the editor documents
    const auto t = gp::findTextPool(rom);      // was findTextTable
    if (!t) { std::printf("FAIL  name table not found\n"); return 1; }
    std::printf("      table at byte 0x%x, entry size %u bytes (compare with GigaEditr's footer line)\n",
                t->tableWord * 2, t->strideWords * 2);

    gp::ContentDB db;
    gp::importText(db, rom, *t);
    check("text 95",  db.textOr("base:text/95"),  "Chew Toy");     // fills its chunk exactly, no terminator
    check("text 96",  db.textOr("base:text/96"),  "Dodge Ball");   // spans two chunks
    check("text 97",  db.textOr("base:text/97"),  "Fuzzy Ball");
    check("text 287", db.textOr("base:text/287"), "Sooty");        // first NPC name

    std::printf(fails ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}