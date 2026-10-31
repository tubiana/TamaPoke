// Experience: banked creatures grow by fighting.
//
// Held here: the arithmetic (a level's cost, several levels at once, the cap);
// evolution on reaching the species' level, gated like every evolution by art
// and installed pack; experience travelling with its creature through every
// Party method that moves one, and surviving a reload; a save from before
// experience starting at zero with a sensible cap; and -- through the real
// battle screen -- the share: full for whoever was out, half for the bench,
// once per foe, nothing past the cap.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "moves.h"
#include <cstdio>
uint32_t g_seed=4040; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void battleTap(int16_t x, int16_t y);
bool startWildBattle(int16_t dex, uint8_t lvl, uint8_t place);
void startTrainerBattle(uint8_t idx, bool hard);
extern Pet pet;
extern Party party;
extern bool battleOpen, btlOver, btlWild;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMenu, btlMsgCount, btlSquadAt, btlSquadN;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static PartyMon mon(int16_t dex, uint16_t lvl){ PartyMon m; makeMon(dex, lvl, false, m); return m; }
static void clearTeam(){ for (int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i); for (int i=0;i<BOX_SLOTS;i++) party.boxReleaseAt(i); }

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 60UL * 29;              // level 30
  pet.dbgTick();
  ck(pet.xpCap() >= 30, "the cap is at least the live creature's level");

  // --- the arithmetic
  clearTeam();
  party.replaceAt(0, mon(16, 10));
  bool rising = true;
  for (uint16_t l = 1; l < 100; l++) rising &= Party::xpToNext(l + 1) > Party::xpToNext(l);
  ck(rising, "each level costs more than the last");
  XpResult r = party.gainXp(0, Party::xpToNext(10) - 1, 30);
  ck(r.levels == 0 && party.slots[0].level == 10 && party.xp[0] == Party::xpToNext(10) - 1, "one short of a level is no level");
  r = party.gainXp(0, 1, 30);
  ck(r.levels == 1 && party.slots[0].level == 11 && party.xp[0] == 0, "and one more is");
  r = party.gainXp(0, Party::xpToNext(11) + Party::xpToNext(12) + 5, 30);
  ck(r.levels == 2 && party.slots[0].level == 13 && party.xp[0] == 5, "a big win can be worth two levels, with the rest kept");
  r = party.gainXp(0, 60000, 15);
  ck(party.slots[0].level == 15 && party.xp[0] == 0, "never past the cap, and nothing banked at it");
  r = party.gainXp(0, 500, 15);
  ck(r.levels == 0 && party.slots[0].level == 15, "at the cap it stays put");

  // --- evolution
  party.replaceAt(1, mon(4, 15));          // CHARMANDER, evolves at 16
  r = party.gainXp(1, Party::xpToNext(15), 30);
  ck(r.evolvedFrom == 4 && party.slots[1].dex == 5, "reaching its level, CHARMANDER becomes CHARMELEON");
  party.replaceAt(2, mon(DEX_EEVEE, DEX_TBL[DEX_EEVEE].evolveLevel - 1));
  r = party.gainXp(2, Party::xpToNext(party.slots[2].level), 60);
  bool branch = false;
  for (int i = 0; i < EEVEE_EVO_COUNT; i++) branch |= party.slots[2].dex == EEVEE_EVOS[i];
  ck(r.evolvedFrom == DEX_EEVEE && branch, "an Eevee takes one of its branches");
  // the pack gate: a Kanto creature whose evolution lives in another region
  int16_t cross = 0;
  for (int16_t d = 1; d <= 151 && !cross; d++)
    if (DEX_TBL[d].evolvesTo > 151 && DEX_TBL[d].evolveLevel && d != DEX_EEVEE) cross = d;
  if (cross) {
    uint16_t was = gRegionArt;
    gRegionArt = 1u << 0;
    party.replaceAt(3, mon(cross, DEX_TBL[cross].evolveLevel - 1));
    r = party.gainXp(3, Party::xpToNext(party.slots[3].level), 100);
    ck(!r.evolvedFrom && party.slots[3].dex == cross, "it never evolves into a region whose pack is missing");
    gRegionArt = was;
    party.replaceAt(3, mon(cross, DEX_TBL[cross].evolveLevel - 1));
    r = party.gainXp(3, Party::xpToNext(party.slots[3].level), 100);
    ck(r.evolvedFrom == cross, "...and does once the pack is there");
  }

  // --- experience travels with its creature
  clearTeam();
  party.replaceAt(0, mon(16, 10));
  party.gainXp(0, 33, 30);
  party.boxAdd(mon(19, 12));
  party.swapPartyBox(0, 0);
  ck(party.xp[0] == 0 && party.boxXp[0] == 33, "into the box and back out, the experience goes with it");
  party.swapPartyBox(0, 0);
  ck(party.xp[0] == 33 && party.boxXp[0] == 0, "...both ways");
  party.begin();
  ck(party.xp[0] == 33, "and survives a reload");
  party.releaseAt(0);
  ck(party.xp[0] == 0, "letting one go takes its experience with it");
  party.xp[0] = 33;                        // stale, however it got there
  party.add(mon(25, 10));
  ck(party.xp[0] == 0, "a newcomer in a freed slot does not inherit any");
  party.replaceAt(0, mon(25, 10));
  ck(party.xp[0] == 0, "nor one put over it");

  // --- through the real fight: full share out, half on the bench, once
  clearTeam();
  party.replaceAt(0, mon(16, 10));
  party.replaceAt(1, mon(19, 10));
  ck(startWildBattle(10, 5, PL_FOREST), "a wild fight starts");
  ck(btlSquadN == 3, "the squad is the live creature and the two banked");
  while (btlMsgCount) battleTap(233, 233); // "A wild ... appeared!"
  btlMenu = 2;
  battleTap(69 + 168 + 10, 274 + 10);      // POKEMON, cell 1: party slot 0 goes out
  while (btlMsgCount) battleTap(233, 233);
  ck(btlSquadAt == 1, "slot 0's creature is out");
  uint8_t hit = 0;
  for (uint8_t m = 1; m < MOVE_COUNT; m++)
    if (MOVE_TBL[m].power >= 40 && MOVE_TBL[m].acc == 100 && MOVE_TBL[m].effect == 0) { hit = m; break; }
  for (int i = 0; i < 40 && !btlOver; i++) {
    if (btlMsgCount) { battleTap(233, 233); continue; }
    btlYou.hp = btlYou.maxHp; btlYou.moves[0] = hit; btlFoe.hp = 1;
    btlMenu = 1;
    battleTap(69 + 10, 274 + 10);
  }
  uint16_t full = Party::xpForFoe(5, false);
  printf("      out: %u, bench: %u (a level-5 foe is worth %u)\n", party.xp[0], party.xp[1], full);
  ck(btlOver && party.xp[0] == full, "the one that fought takes the full share");
  ck(party.xp[1] == full * XP_BENCH_PCT / 100, "the bench takes half");
  while (btlMsgCount) battleTap(233, 233);
  ck(party.xp[0] == full, "and a knockout pays once");

  // at the cap a fight gives nothing
  clearTeam();
  party.replaceAt(0, mon(16, pet.xpCap()));
  startWildBattle(10, 5, PL_FOREST);
  for (int i = 0; i < 40 && !btlOver; i++) {
    if (btlMsgCount) { battleTap(233, 233); continue; }
    btlYou.hp = btlYou.maxHp; btlYou.moves[0] = hit; btlFoe.hp = 1;
    btlMenu = 1;
    battleTap(69 + 10, 274 + 10);
  }
  ck(party.slots[0].level == pet.xpCap() && party.xp[0] == 0, "at the player's own best, a fight gives nothing");
  while (btlMsgCount) battleTap(233, 233);
  render();

  // --- a save from before experience
  Preferences p; p.begin("tamapoke");
  clearTeam();
  party.replaceAt(0, mon(16, 64));
  p.kv.erase("bstlv");
  Pet older; older.begin();
  ck(older.bestLevel >= 64, "an old save's cap is the best it already raised, banked ones included");

  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
