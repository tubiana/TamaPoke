// The birthday and stickers through the real screens: OPTIONS sets the date,
// the present is drawn and opened by a tap, the sticker page is reachable and
// draws, and every event day's decorations draw and flush.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=909; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void onTap(int16_t x, int16_t y); void onSwipe(int dir); void onSwipeV(int dir);
void openClock();
const char *statusMsg();
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool clockOpen, playerOpen;
extern uint8_t settingsPage, playerPage;
extern uint32_t partyBannerUntil;
extern char partyBannerName[];
extern int g_emuCries;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static bool flushes(){ gfx->frameReady=false; render(); return gfx->frameReady; }

// from the sketch's constants: OPT_BD_* and PRESENT_*
#define BD_DAY_PLUS_X (172 + 20)
#define BD_MON_PLUS_X (334 + 20)
#define BD_Y (350 + 20)
#define PRESENT_CX (50 + 23)
#define PRESENT_CY (250 + 20)
#define D_2026_07_14 20648u           // days since 1970, from Python's datetime
static uint32_t noonOf(uint32_t day){ return day*86400u + 12*3600u; }

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  for (int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i);
  pet.setClock(noonOf(D_2026_07_14));

  // ---- OPTIONS: set the birthday to 14 July by taps alone ----
  openClock(); settingsPage = 1;
  ck(pet.bdayMonth == 0, "no birthday to begin with");
  onTap(BD_DAY_PLUS_X, BD_Y);
  ck(pet.bdayMonth == 1 && pet.bdayDay == 1, "the first + starts it at 1/1");
  for (int i=0;i<13;i++) onTap(BD_DAY_PLUS_X, BD_Y);
  for (int i=0;i<6;i++) onTap(BD_MON_PLUS_X, BD_Y);
  ck(pet.bdayDay == 14 && pet.bdayMonth == 7, "...and + on each reaches 14/7");
  ck(flushes(), "OPTIONS with a birthday flushes");
  clockOpen = false;

  // ---- the day itself: greeting, present, tap ----
  ck(pet.eventToday() == EV_BIRTHDAY && pet.giftReady(), "today is the birthday and the present waits");
  ck(!strcmp(statusMsg(), T(S_GIFT_TAP)), "the status line says there is a present");
  ck(flushes(), "the main screen with the present flushes");
  int cries = g_emuCries;
  onTap(PRESENT_CX, PRESENT_CY);
  ck(!party.slots[0].empty() && party.slots[0].shiny, "tapping the present puts a shiny in the party");
  ck(partyBannerUntil > 0 && !strcmp(partyBannerName, dexName(party.slots[0].dex)),
     "...announced by name");
  ck(g_emuCries == cries + 1, "...with its cry");
  ck(!pet.giftReady() && !strcmp(statusMsg(), T(S_HAPPY_BDAY)), "then it just says happy birthday");
  // the spot where the present was is not a caress: it is outside inPetZone
  uint8_t joy = pet.joy;
  onTap(PRESENT_CX, PRESENT_CY);
  ck(pet.joy == joy, "the present's spot never doubled as petting the creature");

  // ---- the sticker page: the last page of the player card ----
  playerOpen = true; playerPage = 0;
  int pages = 0;
  while (playerOpen && pages < 20) { onSwipe(-1); pages++; if (!playerOpen) break; }
  ck(pages >= 3, "the player card pages through to the end");
  playerOpen = true; playerPage = pages - 1;
  ck(flushes(), "the sticker page (the last one) draws and flushes");
  playerOpen = false;

  // ---- every event's decorations draw ----
  {
    int ok = 0;
    for (uint32_t d = D_2026_07_14; d < D_2026_07_14 + 366; d++) {
      pet.setClock(noonOf(d));
      if (pet.eventToday() == EV_NONE) continue;
      ok += flushes();
      ok -= 1;
    }
    ck(ok == 0, "the main screen flushes on every event day of a year");
  }

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
