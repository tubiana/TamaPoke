// Exploring, catching and the boxes -- through the real screens.
//
// What is being held: the outing bar fills at its documented rates and never
// past full, even after two weeks off; a wild creature only ever comes from the
// place tapped, from an installed region, never a legendary; one never seen is
// favoured; a ball holds more often on a hurt or common creature; the creature
// caught is exactly the one fought; it lands at the end of the boxes and in the
// Pokedex; catching a species again simply adds a second one; and the boxes and
// the team screen move creatures between them without ever losing one.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "pcbox.h"
#include "backs.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=2024; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void openExplore();
void exploreTap(int16_t x, int16_t y);
void exploreSpotAt(uint8_t i, int *x, int *y);
void battleTap(int16_t x, int16_t y);
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
void boxTap(int16_t x, int16_t y);
void partyTap(int16_t x, int16_t y);
void boxOpenFrom(bool menu);
void sheetBtnRect(uint8_t n, uint8_t i, int *x, int *w);
void pcStartup();
extern bool boxOpen, partyOpen;
extern uint16_t boxDetail, boxToTeam;
extern uint8_t partyDetail;
extern Pet pet;
extern Party party;
extern bool exploreOpen, battleOpen, btlWild, btlCaught, btlOver;
extern int8_t expPlace;
extern uint8_t expActive, btlMenu, btlMsgCount;
extern uint32_t expActiveUntil;
extern Combatant btlYou, btlFoe;
extern PartyMon btlWildMon;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void refill(){ pet.expGain(EXP_CHARGE_UNITS * EXP_MAX); }

// Stir spot 0 and tap it: one encounter through the real tap path.
static void meet(){
  expActive = 0; expActiveUntil = millis() + 1000;
  int x, y; exploreSpotAt(0, &x, &y);
  exploreTap(x, y);
}

// Win the fight by ball: keep both alive and throw until it holds.
static bool catchIt(){
  for (int i = 0; i < 200 && battleOpen; i++) {
    if (btlMsgCount) { battleTap(233, 233); render(); continue; }
    btlYou.hp = btlYou.maxHp; btlFoe.hp = 1; btlMenu = 0;
    battleTap(237 + 60, 274 + 22);          // cell 1 of the grid: BALL
  }
  render();                                 // exploreAfterBattle runs here
  return !battleOpen;
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 60UL * 30;              // level 31: evolved forms can turn up
  pet.sleeping = false;

  // --- the bar
  ck(pet.expCharges() == EXP_MAX, "a save with no bar yet starts with it full");
  for (int i = 0; i < EXP_MAX; i++) pet.expSpend();
  ck(pet.expCharges() == 0 && !pet.expSpend(), "three outings, then none");
  ck(pet.expMinutesToNext() == EXP_CHARGE_UNITS, "the next one is a full charge away");
  for (int i = 0; i < EXP_CHARGE_UNITS - 1; i++) pet.dbgTick();
  ck(pet.expCharges() == 0, "not a minute early");
  pet.dbgTick();
  ck(pet.expCharges() == 1, "one back after 200 awake minutes (10 h for three)");
  pet.sleeping = true; pet.sleepAuto = SLEEP_PLAYER;
  for (int i = 0; i < EXP_CHARGE_UNITS / EXP_SLEEP_RATE; i++) pet.dbgTick();
  pet.sleeping = false; pet.sleepAuto = SLEEP_NONE;
  ck(pet.expCharges() == 2, "asleep it comes back five times as fast (2 h for three)");
  pet.expGain(14UL * 24 * 60 * EXP_SLEEP_RATE);                       // two weeks asleep, off
  ck(pet.expCharges() == EXP_MAX && pet.expMinutesToNext() == 0, "and never past full, however long");

  // --- who lives where
  bool every = true;
  for (uint8_t p = 0; p < PLACE_COUNT; p++) every &= placeCount(p) > 0;
  ck(every, "every place has someone to meet with every pack installed");
  bool honest = true, noLegend = true;
  for (uint8_t p = 0; p < PLACE_COUNT; p++)
    for (int i = 0; i < 150; i++) {
      int16_t d = pet.rollWild(p);
      honest &= placeHas(p, d);
      noLegend &= d && DEX_TBL[d].rarity != R_LEGENDARIO;
    }
  ck(honest, "a wild creature always comes from the place it was met in");
  ck(noLegend, "and is never a legendary");

  // only installed regions -- and a companion proving the gate engaged
  uint16_t was = gRegionArt;
  gRegionArt = 1u << 0;                    // Kanto alone
  bool kanto = true;
  for (int i = 0; i < 600; i++) kanto &= regionOfDex(pet.rollWild(PL_MEADOW)) == 0;
  gRegionArt = was;
  bool beyond = false;
  for (int i = 0; i < 600 && !beyond; i++) beyond = regionOfDex(pet.rollWild(PL_MEADOW)) != 0;
  ck(kanto, "with only the Kanto pack, only Kanto creatures turn up");
  ck(beyond, "...and with every pack, others do (the gate is what held it)");

  // the unseen are favoured: half the meadow's commons seen, half not
  {
    int16_t seenOnes[400], unseenOnes[400]; int ns = 0, nu = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      if (!placeHas(PL_MEADOW, d) || !wildCandidate(d) || DEX_TBL[d].rarity != R_COMUN) continue;
      if ((ns + nu) % 2) { pet.markSeen(d); seenOnes[ns++] = d; } else unseenOnes[nu++] = d;
    }
    long hitS = 0, hitU = 0;
    for (int i = 0; i < 20000; i++) {
      int16_t d = pet.rollWild(PL_MEADOW);
      for (int k = 0; k < ns; k++) hitS += seenOnes[k] == d;
      for (int k = 0; k < nu; k++) hitU += unseenOnes[k] == d;
    }
    double ratio = (hitU / (double)nu) / (hitS / (double)ns);
    printf("      an unseen common turns up %.2fx as often as a seen one\n", ratio);
    ck(ratio > 2.2 && ratio < 4.0, "a species never seen is about three times as likely");
  }

  // --- the ball
  ck(catchChancePct(16, 100, 100, false) < catchChancePct(16, 1, 100, false), "a hurt creature is easier to catch");
  ck(catchChancePct(147, 10, 100, false) < catchChancePct(16, 10, 100, false), "a rare one is harder than a common one");
  ck(catchChancePct(16, 100, 100, true) > catchChancePct(16, 100, 100, false), "kid mode makes it easier");
  bool bounded = true;
  for (int16_t d = 1; d <= DEX_COUNT; d += 7)
    for (int hp = 0; hp <= 100; hp += 25) {
      uint8_t c = catchChancePct(d, hp, 100, hp & 1);
      bounded &= c >= 1 && c <= 95;
    }
  ck(bounded, "never certain, never hopeless");

  // --- the screens: map -> place -> a stirring spot -> a wild fight
  openExplore();
  render();
  ck(exploreOpen && expPlace < 0, "EXPLORE opens on the map");
  // the back arrow beside the title: it leaves, and no pin sits under it
  {
    const int AX = 233 - 120, AY = 46, AR = 22 + 12, PIN_R = 30;
    bool clear = true;
    for (uint8_t p = 0; p < PLACE_COUNT; p++) {
      int dx = MAP_PINS[p].x * 2 - AX, dy = MAP_PINS[p].y * 2 + 8 - AY;
      clear &= dx * dx + dy * dy > (AR + PIN_R) * (AR + PIN_R);
    }
    ck(clear, "the back arrow's hit area overlaps no place's pin");
    exploreTap(AX + 14, AY + 14);          // a finger near the arrow, not on its centre
    ck(!exploreOpen, "tapping the back arrow leaves the map");
    openExplore();
    render();
  }
  exploreTap(MAP_PINS[PL_FOREST].x * 2, MAP_PINS[PL_FOREST].y * 2);
  ck(expPlace == PL_FOREST, "tapping the forest's pin goes to the forest");
  render();
  int sx, sy; exploreSpotAt(1, &sx, &sy);
  expActive = 0; expActiveUntil = millis() + 1000;
  uint8_t before = pet.expCharges();
  exploreTap(sx, sy);                       // a STILL spot
  ck(!battleOpen && pet.expCharges() == before, "a still spot costs nothing and meets nobody");

  int fled = 0, fights = 0;
  for (int i = 0; i < 300; i++) {
    refill();
    meet();
    if (battleOpen) { fights++; battleOpen = false; btlWild = false; }
    else fled++;
    render();
  }
  printf("      %d of 300 took fright\n", fled);
  ck(fled >= 12 && fled <= 54, "about one in ten takes fright before a fight");

  // a real catch
  refill();
  uint8_t c0 = pet.expCharges();
  do { meet(); if (!battleOpen) refill(); } while (!battleOpen);
  ck(pet.expCharges() < c0, "meeting one spends an outing");
  ck(btlWild && btlFoe.dex == btlWildMon.dex, "the fight is against the creature that came out");
  int16_t dex = btlWildMon.dex;
  PartyMon fought = btlWildMon;
  ck(pet.isSeen(dex), "and it is SEEN from the moment it appears");
  ck(catchIt(), "a ball holds sooner or later");
  ck(pcCount() >= 1, "the catch lands in the boxes");
  PartyMon got; pcGet(pcCount() - 1, got);
  ck(got.dex == dex, "at the END: the boxes keep the order things arrived in");
  ck(pet.isRegistered(dex), "and in the Pokedex");
  { Pet again; again.begin();    // what a reboot right after the catch would load
    ck(again.isRegistered(dex) && again.registeredCount() == pet.registeredCount(),
       "and it is SAVED: a reboot right after still has it in the Pokedex"); }
  ck(got.level == fought.level && got.ivAtk == fought.ivAtk && got.moves[0] == fought.moves[0],
     "the creature kept is exactly the one fought");

  // the same species again: no question, a second one
  uint16_t pcBefore = pcCount();
  btlWild = true; btlCaught = true; battleOpen = false;
  makeMon(dex, got.level + 7, false, btlWildMon);
  render();
  PartyMon second; pcGet(pcCount() - 1, second);
  ck(pcCount() == pcBefore + 1 && second.dex == dex && second.level == got.level + 7,
     "catching a species again keeps BOTH, like the real games");

  // --- the boxes <-> the team, through the real sheets, never losing one
  auto total = [&](int16_t d) {
    int n = 0;
    for (uint16_t i = 0; i < pcCount(); i++) { PartyMon m; pcGet(i, m); n += m.dex == d; }
    for (auto &m : party.slots) n += m.dex == d;
    return n;
  };
  auto tapSheet = [&](bool fromBox, uint8_t i) {
    int x, w; sheetBtnRect(fromBox ? 2 : 3, i, &x, &w);
    if (fromBox) boxTap(x + w / 2, 336 + 24); else partyTap(x + w / 2, 336 + 24);
    render();
  };
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  int had = total(dex);
  uint16_t at = pcCount() - 1;
  boxOpenFrom(true);
  boxDetail = at + 1;
  tapSheet(true, 0);                        // TO TEAM, with room
  ck(party.slots[0].dex == dex && pcCount() == at, "TO TEAM moves it out of the boxes");
  ck(boxOpen && !boxDetail, "and leaves you on the boxes");
  boxOpen = false;
  partyOpen = true; partyDetail = 1;
  tapSheet(false, 1);                       // TO BOX
  ck(party.slots[0].empty() && pcCount() == at + 1, "TO BOX puts it back, at the end");
  ck(total(dex) == had, "nobody gained, nobody lost");

  // a FULL team: TO TEAM opens the team to choose who goes to the boxes
  PartyMon filler; makeMon(16, 10, false, filler);
  for (int i = 0; i < PARTY_SLOTS; i++) party.replaceAt(i, filler);
  party.xp[2] = 7;
  uint16_t n0 = pcCount();
  boxOpenFrom(true);
  boxDetail = n0;                           // the last one: our species
  tapSheet(true, 0);
  ck(!boxOpen && partyOpen && boxToTeam == n0, "with the team full, the team opens to pick a slot");
  partyTap(78 + 150 / 2, 88 + 78 + 70 / 2);  // slot 2: left column, second row
  render();
  PartyMon out; uint16_t outXp = 0; pcGet(n0 - 1, out, &outXp);
  ck(party.slots[2].dex == dex && out.dex == 16, "the two change places");
  ck(outXp == 7, "and the one that left takes its experience with it");
  ck(pcCount() == n0 && total(dex) == had, "still nobody gained or lost");
  ck(boxOpen && !boxToTeam, "and you are back on the boxes");
  boxOpen = false; partyOpen = false;

  // --- the repair: a catch that reached the boxes but not the Pokedex (what a
  // closed NVS handle left behind before 3.25) is put back at boot
  {
    int16_t lost = 0;
    for (int16_t d = 1; d <= DEX_COUNT && !lost; d++)
      if (!pet.isRegistered(d) && !pcHas(d) && wildCandidate(d)) lost = d;
    PartyMon m; makeMon(lost, 9, false, m);
    pcAdd(m);
    ck(pcHas(lost) && !pet.isRegistered(lost), "a species in the boxes but not the Pokedex (the 3.24 state)");
    pcStartup();
    Pet again; again.begin();
    ck(pet.isRegistered(lost) && again.isRegistered(lost), "the boot repair registers it, and it is saved");
  }

  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
