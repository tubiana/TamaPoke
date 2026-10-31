// The DATE page of SETTINGS, through the real screen: a board whose RTC was
// never set starts on 1 January 2026, and the date is the only way to make
// the calendar (Halloween, the birthday) line up with the real one.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "i18n.h"
#include <cstdio>
uint32_t g_seed=1031; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void onTap(int16_t x, int16_t y); void onSwipe(int dir);
void openClock();
uint32_t rtcEpoch();
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool clockOpen;
extern uint8_t settingsPage, dateM, dateD;
extern int clockH;
extern uint16_t dateY;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static bool flushes(){ gfx->frameReady=false; render(); return gfx->frameReady; }

// from the sketch's DATE_COL_X / DATE_PLUS_Y / DATE_MINUS_Y / CLOCK_OK_*
static const int COL[3] = { 113, 233, 353 };
#define PLUS_Y (110 + 29)
#define MINUS_Y (232 + 29)
#define OK_X 233
#define OK_Y (340 + 24)
#define DAY 0
#define MON 1
#define YEAR 2
static void plus(int c, int n = 1){ for (int i = 0; i < n; i++) onTap(COL[c], PLUS_Y); }
static void minus(int c, int n = 1){ for (int i = 0; i < n; i++) onTap(COL[c], MINUS_Y); }

#define D_2026_01_05 20458u          // days since 1970, from Python's datetime
#define D_2026_10_31 20757u

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.setClock(D_2026_01_05 * 86400u + 12 * 3600u);   // a seeded RTC, a few days in

  // ---- reaching it ----
  openClock();
  ck(dateY == 2026 && dateM == 1 && dateD == 5, "the page starts on the clock's own date");
  onSwipe(-1);
  ck(clockOpen && settingsPage == 1, "one swipe left from the time is the DATE page");
  ck(flushes(), "the DATE page draws and flushes");

  // ---- the columns ----
  minus(DAY, 5);
  ck(dateD == 31 && dateM == 1, "the day wraps below 1 to the month's last day");
  plus(DAY);
  ck(dateD == 1, "...and past it back to 1");
  plus(DAY, 30);
  minus(MON, 2);                      // Jan -> Dec -> Nov, never through February
  ck(dateM == 11 && dateD == 30, "a 31st carried into November becomes the 30th");
  minus(MON, 9);
  ck(dateM == 2 && dateD == 28, "...and into February 2026 the 28th");
  plus(DAY);
  ck(dateD == 1, "February 2026 has no 29th");
  minus(DAY);
  plus(YEAR, 2);
  plus(DAY);
  ck(dateY == 2028 && dateD == 29, "February 2028 does");
  minus(YEAR, 20);
  ck(dateY == 2024, "the year stops at 2024 rather than wrapping");
  plus(YEAR, 2);
  minus(MON, 2);
  ck(dateM == 12, "the month wraps below 1 to 12");
  plus(MON, 2);
  ck(dateM == 2, "...and past 12 back to 1");

  // ---- set it to 31 October, change the hour too, and press OK on the DATE page ----
  while (dateM != 10) plus(MON);
  while (dateD != 31) plus(DAY);
  ck(dateY == 2026 && dateM == 10 && dateD == 31, "the taps reach 31/10/2026");
  settingsPage = 0;
  int h = clockH;
  onTap(170 + 29, 190 + 29);                  // hour +, on the TIME page
  ck(clockH == (h + 1) % 24, "the hour moved on the TIME page");
  settingsPage = 1;
  uint8_t food = pet.fullness, joy = pet.joy;
  onTap(OK_X, OK_Y);
  ck(!clockOpen, "OK closes SETTINGS");
  ck(pet.lastSeenEpoch / 86400 == D_2026_10_31, "the game's clock is on 31 October");
  ck(pet.lastSeenEpoch % 86400 / 3600 == (uint32_t)((h + 1) % 24), "...at the hour set on the other page: one edit, not two");
  ck(rtcEpoch() / 86400 == D_2026_10_31, "the RTC was set too, so it survives a reboot");
  ck(pet.fullness == food && pet.joy == joy, "jumping ten months ahead is not ten months away: nothing drained");

  // ---- the calendar now lines up ----
  ck(pet.eventToday() == EV_HALLOWEEN, "it is Halloween");
  pet.setBirthday(10, 31);
  ck(pet.eventToday() == EV_BIRTHDAY && pet.giftReady(), "with the birthday set to today, the present waits");

  // ---- the TIME page's own OK keeps the date that is shown ----
  openClock();
  ck(dateM == 10 && dateD == 31, "reopening shows the date just set");
  onTap(OK_X, OK_Y);
  ck(pet.lastSeenEpoch / 86400 == D_2026_10_31, "OK on the TIME page keeps it");

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
