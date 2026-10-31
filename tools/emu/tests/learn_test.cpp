// A pending "wants to learn" prompt answers only the taps meant for IT.
//
// render() draws the party, the Pokedex, SETTINGS and the minigames on top of a
// pending prompt, but onTap() asked hasLearnOffer() first -- so a swipe right
// from the prompt showed the party while a tap on a party slot accepted or
// skipped a move behind it. Both now ask uiCurrentScreen(), whose order has to
// match render()'s; this checks the screens that sit above the prompt, and that
// the prompt itself still works (or the first checks would pass vacuously).
// A fresh harness, from a known state -- see CLAUDE.md, section 3.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=2024; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void onTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool partyOpen, galleryOpen, galleryPick, clockOpen, trainOpen, cardOpen;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// LEARN_ROW_Y(0) + half a row: the first "forget this move" row on the prompt,
// and inside party slot 0/1's row on the party screen
#define ROW0_Y (104 + 25)

static bool armOffer(){
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.dbgHatchAs(4,false);              // CHARMANDER, fresh: offers come as it ages
  pet.ageMinutes = 60UL*60;             // level 61: several gates crossed at once
  pet.checkLearnGates();
  return pet.hasLearnOffer();
}
static bool sameMoves(const uint8_t *a){ return !memcmp(a, pet.moves, MOVE_SLOTS); }

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);

  struct { const char *name; bool *flag; } above[] = {
    { "party",    &partyOpen },
    { "settings", &clockOpen },
    { "training", &trainOpen },
  };
  for (auto &s : above) {
    if (!armOffer()) { ck(false, "could not arm a learn offer"); break; }
    uint8_t before[MOVE_SLOTS]; memcpy(before, pet.moves, MOVE_SLOTS);
    uint8_t q = pet.learnOffer();
    *s.flag = true;
    char w[80];
    snprintf(w, sizeof(w), "%s is what the panel says is drawn over the prompt", s.name);
    ck(strcmp(SCREEN_NAME[uiCurrentScreen()], "learn") != 0, w);
    onTap(233, ROW0_Y);
    snprintf(w, sizeof(w), "a tap on %s leaves the hidden prompt alone", s.name);
    ck(pet.hasLearnOffer() && pet.learnOffer() == q && sameMoves(before), w);
    *s.flag = false;
  }
  // the Pokedex chooser sits above it too
  if (armOffer()) {
    uint8_t q = pet.learnOffer();
    galleryOpen = true; galleryPick = true;
    onTap(233, ROW0_Y);
    ck(pet.hasLearnOffer() && pet.learnOffer() == q, "a tap on the Pokedex chooser leaves it alone");
    galleryOpen = galleryPick = false;
  }

  // ...and the prompt itself still answers: without this, "left alone" above
  // would also pass if the prompt had simply stopped working
  if (armOffer()) {
    uint8_t q = pet.learnOffer();
    ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "learn"), "with nothing above it, the prompt is the screen");
    onTap(233, ROW0_Y);
    ck(!pet.hasLearnOffer() || pet.learnOffer() != q, "a tap on the prompt's own row answers it");
    // the card is drawn BELOW the prompt, so with both open the tap is the prompt's
    if (armOffer()) {
      uint8_t q2 = pet.learnOffer();
      cardOpen = true;
      ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "learn"), "the prompt covers the card, and says so");
      onTap(233, ROW0_Y);
      ck(!pet.hasLearnOffer() || pet.learnOffer() != q2, "...and takes the tap");
      cardOpen = false;
    }
  }

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
