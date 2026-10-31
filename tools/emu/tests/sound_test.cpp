// One tap, one sound -- and the creature's own voice where the card has it.
//
// A caress used to play the heart blip AND, sometimes, the cry right after it:
// two sounds for one touch, heard on the board. The cry now stands in for the
// blip (cryOr()), and the blip comes back when the card has no cries pack. The
// Pokedex's detail page speaks a registered species' name; the quiz's day
// score pays out stars that train the live creature.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "i18n.h"
#include "audio.h"
#include <cstdio>
uint32_t g_seed=4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void onTap(int16_t x, int16_t y);
void openQuiz();
void emuSetTimeScale(uint32_t s);
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool galleryOpen, galleryPick, quizOpen;
extern uint8_t galleryRegion;
extern int galleryPage;
extern int16_t galleryDetail, quizAns, quizOpt[3];
extern int8_t quizPicked;
extern uint8_t quizGainShown;
extern int g_emuCries, g_emuSfx;
extern int16_t g_emuLastCry;
extern bool g_emuHasCries;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static bool flushes(){ gfx->frameReady=false; render(); return gfx->frameReady; }
static int sounds(){ return g_emuSfx + g_emuCries; }

// from the sketch: GAL_X / GAL_Y / GAL_CELL, QUIZ_ROW_*
#define CELL_X(c) (73 + (c) * 80 + 40)
#define CELL_Y(r) (84 + (r) * 80 + 40)
#define QROW_X 233
#define QROW_Y(i) (256 + (i) * 52 + 23)

static void answer(bool right) {
  for (int i = 0; i < 3; i++)
    if ((quizOpt[i] == quizAns) == right) { onTap(QROW_X, QROW_Y(i)); return; }
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.setClock(20757u * 86400u + 12 * 3600u);
  pet.sleeping = false;

  // ---- a caress: exactly one sound per touch, with and without cries ----
  // time runs fast so the once-a-minute cry gap passes between taps
  emuSetTimeScale(2000000);
  for (int pass = 0; pass < 2; pass++) {
    g_emuHasCries = (pass == 0);
    int one = 0, cried = 0, taps = 200;
    for (int i = 0; i < taps; i++) {
      int s0 = sounds(), c0 = g_emuCries;
      uint32_t t = millis();
      while (millis() - t < 61000) {}          // past the once-a-minute gap
      onTap(233, 200);
      one += (sounds() - s0) == 1;
      cried += g_emuCries > c0;
    }
    printf("    (%s: %d of %d caresses one sound, %d cried)\n", pass ? "no cries" : "cries", one, taps, cried);
    ck(one == taps, pass ? "no cries pack: every caress is just the heart" : "with cries: every caress is ONE sound, never blip + cry");
    if (!pass) ck(cried > 10 && cried < taps / 2, "...and some of them are the creature's voice, not most");
    else ck(cried == 0, "...and nothing claims a cry it could not play");
  }
  emuSetTimeScale(1);
  g_emuHasCries = true;

  // ---- the Pokedex: a registered species' page says its name ----
  galleryOpen = true; galleryPick = false; galleryRegion = 0; galleryPage = 0; galleryDetail = 0;
  int c0 = g_emuCries, s0 = sounds();
  onTap(CELL_X(3), CELL_Y(0));                 // dex 4, the live creature: registered
  ck(galleryDetail == 4 && g_emuCries == c0 + 1 && g_emuLastCry == 4, "opening CHARMANDER's page plays its cry");
  ck(sounds() == s0 + 1, "...and only that");
  onTap(CELL_X(3), CELL_Y(0));                 // back to the grid
  ck(!pet.isRegistered(2), "(IVYSAUR is not registered)");
  c0 = g_emuCries;
  onTap(CELL_X(1), CELL_Y(0));
  ck(galleryDetail == 2 && g_emuCries == c0, "an unmet species' page stays silent");
  galleryOpen = false; galleryDetail = 0;

  // ---- the quiz: one sound per tap, and the day's stars ----
  s0 = sounds(); c0 = g_emuCries;
  openQuiz();
  ck(quizOpen && sounds() == s0 + 1 && g_emuCries == c0 + 1, "opening the quiz is one sound: the question's cry");
  uint8_t atk = pet.trAtk, def = pet.trDef, spe = pet.trSpe;
  int single = 0;
  for (int q = 0; q < QUIZ_PER_DAY; q++) {
    answer(q < 8);                             // 8 right, 2 wrong
    if (q < QUIZ_PER_DAY - 1) {
      s0 = sounds();
      onTap(233, 120);                         // next question
      single += sounds() == s0 + 1;
    }
  }
  ck(single == QUIZ_PER_DAY - 1, "each next question is one sound, not a blip and a cry");
  ck(pet.quizRightToday() == 8 && pet.quizStars() == 2, "8 right of 10 is two stars");
  int trained = (pet.trAtk - atk) + (pet.trDef - def) + (pet.trSpe - spe);
  ck(trained == 2 * QUIZ_STAR_TRAIN && quizGainShown == trained, "the two stars trained the creature, and the screen knows by how much");
  onTap(233, 120);                             // to the day's score
  ck(quizPicked < 0 && !pet.quizLeft() && flushes(), "the score screen with its stars draws and flushes");
  pet.quizAnswered(true);
  ck((pet.trAtk - atk) + (pet.trDef - def) + (pet.trSpe - spe) == trained, "an answer past the day's ten earns nothing more");
  {
    Pet q; q.begin();
    q.lastSeenEpoch = pet.lastSeenEpoch;
    ck(q.quizRightToday() == 8, "today's score survives a reboot");
    q.lastSeenEpoch += 86400;
    ck(q.quizRightToday() == 0 && q.quizStars() == 0 && q.quizLeft() == QUIZ_PER_DAY, "and the next day starts from nothing");
  }
  {
    // the thresholds: 4 -> none, 5 -> one, 10 -> three
    Pet p; p.dbgHatchAs(7, false); p.lastSeenEpoch = 20000u * 86400u;
    for (int i = 0; i < 4; i++) p.quizAnswered(true);
    ck(p.quizStars() == 0, "four right is no star yet");
    p.quizAnswered(true);
    ck(p.quizStars() == 1, "five right is one");
    for (int i = 0; i < 5; i++) p.quizAnswered(true);
    ck(p.quizStars() == 3 && p.quizGain == 3 * QUIZ_STAR_TRAIN, "all ten is three, paid on the tenth");
  }
  quizOpen = false;

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
