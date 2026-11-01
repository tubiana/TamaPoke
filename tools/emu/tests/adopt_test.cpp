// Adopting a caught creature in place of the waiting egg -- through the real
// Pokedex page.
//
// What is being held: the ADOPT button exists only while an egg waits; one tap
// only arms it, since giving up the egg cannot be undone; the second raises the
// caught individual (its species, IVs, name) at level 1 as a LIVING creature --
// not the frozen companion a revive makes -- so it ages and evolves; it leaves
// the Pokedex box; and it survives a reload.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dexbox.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=2024; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void galleryTap(int16_t x, int16_t y);
bool dexPageCanAdopt(int16_t dex);
extern Pet pet;
extern bool galleryOpen;
extern int16_t galleryDetail;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static const int16_t DEX = 4;          // CHARMANDER: evolves at 16
static void openPage(){ galleryOpen = true; galleryDetail = DEX; render(); }
static void tapAdopt(){ galleryTap(233 + 80, 344 + 23); render(); }

int main(){
  setup();
  dexBoxWipe();
  pet.dbgHatchAs(1, false);            // someone alive, so the starter screen is past
  PartyMon caught; makeMon(DEX, 24, false, caught);
  caught.ivAtk = 29; caught.ivDef = 3; caught.ivSpe = 17; caught.ivHp = 8;
  strcpy(caught.nick, "EMBER");
  ck(dexBoxPut(caught), "a caught CHARMANDER sits in the Pokedex box");
  pet.registerCaught(DEX, false);

  // --- no egg waiting: nothing to adopt into
  openPage();
  ck(!dexPageCanAdopt(DEX), "with a creature alive there is no ADOPT");
  tapAdopt(); tapAdopt();
  ck(pet.speciesId == 1 && dexBoxHas(DEX), "and tapping where it would be changes nothing");

  // --- an egg waits
  pet.newEgg();
  ck(pet.isEgg() && !pet.awaitingStarter(), "an egg is waiting");
  openPage();
  ck(dexPageCanAdopt(DEX), "now the page offers ADOPT");
  tapAdopt();
  ck(pet.isEgg() && dexBoxHas(DEX), "the first tap only arms it");
  tapAdopt();
  ck(!pet.isEgg() && pet.speciesId == DEX, "the second raises the CHARMANDER instead of the egg");
  ck(pet.level() == 1, "at level 1, whatever level it was caught at");
  ck(pet.ivAtk == 29 && pet.ivDef == 3 && pet.ivSpe == 17 && pet.ivHp == 8,
     "the very individual that was caught (its IVs)");
  ck(!strcmp(pet.nick, "EMBER"), "with its name");
  ck(!pet.frozen, "and ALIVE -- not the frozen companion a revive makes");
  ck(!dexBoxHas(DEX), "it has left the Pokedex box");
  ck(!galleryOpen, "and the screen goes back to it");

  // --- it grows up like anything that hatched
  pet.ageMinutes = (uint32_t)(DEX_TBL[DEX].evolveLevel + 1) * MINUTES_PER_LEVEL;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 90;
  pet.careMistakes = 0;   // evoPenalty() is 0: no early retire here
  ck(pet.canEvolveNow(), "it evolves when it is old enough");

  // --- and it is the saved creature
  pet.saveNow();
  Pet again; again.begin();
  ck(again.speciesId == DEX && !again.frozen && !strcmp(again.nick, "EMBER"),
     "a reload brings back the adopted one");

  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
