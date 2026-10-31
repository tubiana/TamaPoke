// Gym fights happen in the leader's arena, and the fight stays in the region it
// was started from.
//
// The second half is why this test exists at all. `btlRegion` was declared to
// keep a running fight's region apart from the gym list's, and NOTHING ever set
// it: every ladder past Kanto led with the right creature, then chained into
// KANTO's team and paid out a Kanto badge. The arena is chosen from the same
// variable, which is how it surfaced.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "backs.h"
#include "moves.h"
#include <cstdio>
uint32_t g_seed=777; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void startTrainerBattle(uint8_t idx, bool hard);
void battleTap(int16_t x, int16_t y);
uint8_t battleArena();
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool battleOpen, btlOver, btlWon;
extern Combatant btlYou, btlFoe;
extern uint8_t gymRegion, btlMenu, btlMsgCount;
extern int8_t btlTrainer;
extern uint32_t btlWinUntil;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

#define KANTO 0
#define JOHTO 1
#define CHAMP (TRAINER_COUNT - 1)

static void start(uint8_t region, uint8_t idx) {
  battleOpen = false; btlWinUntil = 0; btlMsgCount = 0;
  gymRegion = region;
  startTrainerBattle(idx, false);
}

// how much of one backdrop row on the panel is exactly this scene's pixels
static int rowMatch(const BackScene &b, int r) {
  const int y = 30 + r * 2, x0 = 233 - b.w;          // drawBack(…, 30), scaled 2x
  int hit = 0, n = 0;
  for (int x = 120; x < 346; x += 2, n++) {
    uint16_t want = b.pal[b.idx[r * b.w + (x - x0) / 2]];
    if (gfx->buffer()[y * 466 + x] == want) hit++;
  }
  return hit * 100 / n;
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 60UL * 99;                         // old enough for every ladder
  pet.energy = 100;

  // --- every gym fight has an arena, and it is the leader's region's leader
  int tested = 0; bool champ = true, sameType = true;
  uint8_t byType[TYPE_COUNT]; for (auto &v : byType) v = 0xFF;
  for (uint8_t r = 0; r < GYM_REGIONS; r++)
    for (uint8_t i = 0; i < TRAINER_COUNT; i++) {
      start(r, i);
      if (!battleOpen) continue;
      tested++;
      uint8_t a = battleArena();
      if (a >= ARENA_COUNT) { ck(false, "a gym fight with no arena"); continue; }
      if (i == CHAMP) { champ &= (a == ARENA_DRAGON); continue; }
      uint8_t t = TRAINER_SETS[r].list[i].type;
      if (byType[t] == 0xFF) byType[t] = a;
      sameType &= (byType[t] == a);
    }
  ck(tested == GYM_REGIONS * TRAINER_COUNT, "every trainer of every region could be fought");
  ck(champ, "every Champion fights in the grand hall");
  ck(sameType, "two leaders of one type always get the same arena, across regions");

  // anchors, so the table cannot drift into nonsense and stay consistent
  start(KANTO, 6);                                    // Blaine
  ck(battleArena() == ARENA_FIRE, "Blaine (fire) fights in the fire arena");
  start(KANTO, 1);                                    // Misty
  ck(battleArena() == ARENA_WATER, "Misty (water) fights at the pool");
  start(JOHTO, 6);                                    // Pryce, where Kanto has Blaine
  ck(battleArena() == ARENA_ICE, "Johto's 7th leader (Pryce, ice) is NOT Kanto's 7th (Blaine)");

  // --- and it is what the panel actually shows
  start(KANTO, 6);
  render();
  int fire = rowMatch(ARENAS[ARENA_FIRE], 3), biome = rowMatch(BACKS[0][0], 3);
  printf("      fire arena row matches %d%%, the meadow %d%%\n", fire, biome);
  ck(fire >= 90, "the fire arena is what is drawn behind the fight");

  // a fight that is not a gym stays where the foe lives
  btlTrainer = -1;
  ck(battleArena() == ARENA_COUNT, "a one-off fight has no arena");

  // --- the whole Johto gym, played through the real tap path: the second
  // creature and the badge must be Johto's
  uint8_t move = 0;
  for (uint8_t m = 1; m < MOVE_COUNT; m++)
    if (MOVE_TBL[m].power >= 40 && MOVE_TBL[m].acc == 100 && MOVE_TBL[m].effect == 0) { move = m; break; }
  ck(move != 0, "found a plain 100%-accurate attack to finish fights with");
  const Trainer &falkner = TRAINER_SETS[JOHTO].list[0];
  const Trainer &brock = TRAINER_SETS[KANTO].list[0];
  start(JOHTO, 0);
  bool foes = true; int faced = 0; int16_t last = 0;
  for (int step = 0; step < 200 && battleOpen && !btlWinUntil; step++) {
    if (btlMsgCount) { battleTap(233, 233); continue; }
    bool his = false;
    for (int k = 0; k < falkner.count; k++) his |= (btlFoe.dex == falkner.team[k].dex);
    foes &= his;
    if (btlFoe.dex != last) { faced++; last = btlFoe.dex; }
    btlYou.hp = btlYou.maxHp; btlYou.moves[0] = move;
    btlFoe.hp = 1;
    btlMenu = 1;
    battleTap(69 + 10, 286 + 10);                     // move slot 0
  }
  ck(faced == falkner.count, "the fight went through Falkner's whole team");
  ck(btlWinUntil && btlWon, "the Johto gym can be won");
  ck(foes, "every creature that stepped in was Falkner's, never Brock's");
  ck(pet.hasBadge(JOHTO, 0, false), "the badge is Johto's");
  ck(!pet.hasBadge(KANTO, 0, false), "and Kanto's Boulder Badge was not handed out instead");
  (void)brock;

  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
