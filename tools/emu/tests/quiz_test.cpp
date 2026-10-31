// The quiz and the OPTIONS page, driven through the real taps and swipes.
//
// The rules themselves are in kid_test, against Pet. This is the CALLER side:
// that the QUIZ button on the Pokedex chooser opens it, that the three names
// always contain the answer and never repeat, that answering spends exactly one
// of today's and stops at QUIZ_PER_DAY, and that the OPTIONS controls reach the
// settings they are drawn for -- including the kid-mode volume cap, which lives
// in the sketch beside the only buttons that change the volume.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed=31337; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
void onTap(int16_t x, int16_t y); void onSwipe(int dir); void onSwipeV(int dir);
void openClock();
uint8_t audioVolume(); void audioSetVolume(uint8_t v);
extern Pet pet;
extern bool galleryOpen, galleryPick, clockOpen, quizOpen;
extern uint8_t settingsPage;
extern int16_t quizAns, quizOpt[3];
extern int8_t quizPicked;
extern int g_emuCries;
extern int16_t g_emuLastCry;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// geometry from the sketch's own constants (LANBTN_*, QUIZ_ROW_*, OPT_*), not
// copied numbers: these are the centres of what renderQuiz()/renderOptions() draw
#define QUIZ_BTN_X 233
#define QUIZ_BTN_Y (336 + 44/2)
#define QUIZ_ROW_CY(i) (256 + (i) * 52 + 46/2)
#define KID_PILL_X (296 + 92/2)
#define KID_PILL_Y (104 + 44/2)
#define SPD_MINUS_X (112 + 29)
#define SPD_PLUS_X (296 + 29)
#define SPD_Y (226 + 29)

static bool optionsOk() {
  bool hasAns = false, distinct = true;
  for (int i=0;i<3;i++){
    hasAns |= quizOpt[i]==quizAns;
    for (int j=0;j<i;j++) distinct &= quizOpt[i]!=quizOpt[j];
  }
  return hasAns && distinct && quizAns >= 1 && quizAns <= DEX_COUNT;
}
static int rightRow(){ for (int i=0;i<3;i++) if (quizOpt[i]==quizAns) return i; return -1; }

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.setClock(20000*86400UL + 12*3600);
  // A pending "wants to learn" prompt takes every tap, even under a screen
  // drawn over it, so answer it first -- as flush_test and swipe_test do.
  while (pet.hasLearnOffer()) pet.declineLearn();

  // ---- OPTIONS, the second page of SETTINGS ----
  openClock();
  ck(clockOpen && settingsPage == 0, "SETTINGS opens on the clock page");
  onSwipe(-1);
  ck(clockOpen && settingsPage == 1, "a swipe left turns to OPTIONS and stays in SETTINGS");
  onSwipe(+1);
  ck(clockOpen && settingsPage == 0, "...and right turns back");
  onTap(233, 22);
  ck(settingsPage == 1, "tapping the page dots turns the page too");

  audioSetVolume(9);
  bool was = pet.kidMode;
  onTap(KID_PILL_X, KID_PILL_Y);
  ck(!was && pet.kidMode, "the switch turns kid mode on");
  ck(audioVolume() == 5, "...and pulls a loud volume down to the kid cap");
  int before = pet.drainPct;
  onTap(SPD_MINUS_X, SPD_Y);
  ck(pet.drainPct == before - DRAIN_STEP, "- slows the needs by a step");
  onTap(SPD_PLUS_X, SPD_Y);
  ck(pet.drainPct == before, "+ speeds them back up");

  // the volume + on the CLOCK page stops at the cap while kid mode is on
  settingsPage = 0;
  for (int i=0;i<6;i++) onTap(276 + 24, 296 + 15);    // VOL_PLUS_X + half, LANG_PILL_Y + half
  ck(audioVolume() == 5, "kid mode: + stops at 5");
  settingsPage = 1;
  onTap(KID_PILL_X, KID_PILL_Y);
  ck(!pet.kidMode, "the switch turns it off again");
  settingsPage = 0;
  onTap(276 + 24, 296 + 15);
  ck(audioVolume() == 6, "...and + goes past 5 once it is off");
  clockOpen = false;

  // ---- the quiz, from the Pokedex chooser ----
  galleryOpen = true; galleryPick = true;
  int cries = g_emuCries;
  onTap(QUIZ_BTN_X, QUIZ_BTN_Y);
  ck(quizOpen && quizPicked < 0, "the QUIZ button on the Pokedex chooser opens a question");
  ck(g_emuCries == cries + 1 && g_emuLastCry == quizAns, "the question plays the answer's cry");
  ck(optionsOk(), "three different names, one of them right");

  pet.joy = 50;
  onTap(233, QUIZ_ROW_CY(rightRow()));
  ck(quizPicked == rightRow() && pet.quizLeft() == QUIZ_PER_DAY - 1,
     "answering spends one of today's");
  ck(pet.joy == 50 + QUIZ_JOY, "a right answer cheers the creature up");
  onTap(233, 120);
  ck(quizOpen && quizPicked < 0 && optionsOk(), "a tap after the answer brings the next question");

  // every question, all day: always well-formed, and the allowance holds
  int asked = 1, bogus = 0;
  while (pet.quizLeft() > 0 && asked < 40) {
    if (!optionsOk()) bogus++;
    onTap(233, QUIZ_ROW_CY((rightRow() + 1) % 3));     // a wrong one this time
    onTap(233, 120);
    asked++;
  }
  ck(bogus == 0, "no malformed question all day");
  ck(pet.quizLeft() == 0 && asked == QUIZ_PER_DAY, "it stops after exactly QUIZ_PER_DAY");
  ck(quizOpen && quizPicked < 0, "...on the come-back-tomorrow screen");
  int spent = pet.quizLeft();
  onTap(233, QUIZ_ROW_CY(0));
  ck(!quizOpen && pet.quizLeft() == spent, "a tap there just closes it; nothing more is spent");

  onTap(QUIZ_BTN_X, QUIZ_BTN_Y);
  ck(quizOpen && pet.quizLeft() == 0, "reopening the same day still says tomorrow");
  onSwipe(-1);
  ck(!quizOpen && galleryOpen, "a swipe leaves the quiz, back to the Pokedex");

  pet.setClock(20001*86400UL + 9*3600);              // the next morning
  onTap(QUIZ_BTN_X, QUIZ_BTN_Y);
  ck(quizOpen && quizPicked < 0 && optionsOk() && pet.quizLeft() == QUIZ_PER_DAY,
     "the next day there is a full allowance again");

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
