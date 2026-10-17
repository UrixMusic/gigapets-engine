// g++ -std=c++17 -Wall -Wextra sprite_anim_test.cpp -o sprite_anim_test && ./sprite_anim_test
#include "sprite_anim.hpp"
#include <cstdio>
using namespace gp;

static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-50s %s\n", n, ok ? "ok" : "FAIL"); if (!ok) ++fails; }

int main() {
    check("frame number: 1 second at rate 14 = frame 14", frameNumber(60, 14) == 14);
    check("frame number: 30 ticks (timer 60)",            frameNumber(kTickDelta * 30, 14) == 14);
    { SpriteAnimState s; playSpriteAnimation(s, 7, -1, 10, 20, 14, 0, 0x1000, 4, 99, 8);
      check("play: rate override kept, duration from table", s.frameRate == 14 && s.duration == 99 && s.currentFrame == 0);
      check("one-shot starts busy", isAnimationBusy(&s));
      int n = 0; while (isAnimationBusy(&s) && n < 100) { advanceSpriteAnimFrame(s, 4); ++n; }
      check("one-shot finishes on the last frame", !isAnimationBusy(&s) && s.currentFrame == 3 && (s.flags & 1));
      check("4-frame one-shot at rate 14 ends after 9 ticks", n == 9);
      check("finished animation stays put", !advanceSpriteAnimFrame(s, 4) && s.currentFrame == 3); }
    { SpriteAnimState s; playSpriteAnimation(s, 7, -1, 0, 0, -1, 2, 0, 4, 0, 10);
      check("rate < 0 uses the table default", s.frameRate == 10);
      for (int i = 0; i < 300; ++i) advanceSpriteAnimFrame(s, 4);
      check("loop never finishes", isAnimationBusy(&s) && s.currentFrame >= 0 && s.currentFrame < 4); }
    { SpriteAnimState s; playSpriteAnimation(s, 7, -1, 0, 0, 14, 4, 0, 4, 0, 0);
      check("reverse: start frame is the last", s.startFrame == 3);
      advanceSpriteAnimFrame(s, 4);
      check("reverse: first frame shown is 3", s.currentFrame == 3);
      int n = 0; while (isAnimationBusy(&s) && n < 100) { advanceSpriteAnimFrame(s, 4); ++n; }
      check("reverse one-shot ends on frame 0", !isAnimationBusy(&s) && s.currentFrame == 0); }
    { SpriteAnimState s; playSpriteAnimation(s, 7, -1, 0, 0, 14, 2, 0, 10, 0, 0); int changes = 0;
      for (int i = 0; i < 30; ++i) changes += advanceSpriteAnimFrame(s, 10);
      check("loop over one second shows 14 frame changes", changes == 14); }
    check("null state is not busy", !isAnimationBusy(nullptr));
    return fails ? 1 : 0;
}
