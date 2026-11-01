// The main screen's scene -- read back from the panel, not from the code.
//
// What is being held: the creature stands in ITS biome's picture, in the light
// the hour calls for (dawn, day, dusk, night), and each of the four lights is
// really a different palette -- so a firmware stuck on one light, or drawing
// every creature in the meadow, fails here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "backs.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
extern Arduino_Canvas *gfx; extern Pet pet;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// Share of sampled panel pixels that match scene `b` in light `ph`. Sampled
// away from the header text and the creature, on the 2x pixel grid.
static int matchPct(int b, int ph) {
  const BackScene &s = HOMES[b];
  const uint16_t *buf = gfx->buffer();
  int x0 = 233 - s.w;
  int hit = 0, n = 0;
  for (int iy = 4; iy < s.h; iy += 7)
    for (int ix = 3; ix < s.w; ix += 9) {
      if (iy < 60 && ix > 40 && ix < 193) continue;         // the name and status
      if (iy > 60 && ix > 70 && ix < 163) continue;         // the creature
      int x = x0 + ix * 2, y = iy * 2;
      uint16_t want = s.pal[ph * s.palCount + s.idx[iy * s.w + ix]];
      n++; hit += buf[y * 466 + x] == want;
    }
  return hit * 100 / n;
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  static const int HOURS[HOME_PHASE_COUNT] = { 7, 13, 19, 23 };    // dawn day dusk night
  char w[96];
  for (int b = 0; b < HOME_COUNT; b++) {
    int16_t d = 0;
    for (int i = 1; i <= DEX_COUNT && !d; i++) if (DEX_TBL[i].biome == b) d = i;
    pet.dbgHatchAs(d, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    for (int ph = 0; ph < HOME_PHASE_COUNT; ph++) {
      pet.setClock(20700u * 86400u + HOURS[ph] * 3600u + 600);
      render();
      int got = matchPct(b, ph);
      int other = 0;                       // the best any OTHER light or biome does
      for (int b2 = 0; b2 < HOME_COUNT; b2++)
        for (int p2 = 0; p2 < HOME_PHASE_COUNT; p2++)
          if (b2 != b || p2 != ph) { int m = matchPct(b2, p2); if (m > other) other = m; }
      snprintf(w, sizeof(w), "biome %d at %02d:00 draws its own scene in its light (%d%%, next best %d%%)",
               b, HOURS[ph], got, other);
      ck(got >= 75 && other < got - 20, w);   // falling snow and a medal pop-up cover a little
    }
  }
  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
