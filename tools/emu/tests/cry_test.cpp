// When a creature cries -- and, as much, when it does NOT.
//
// The device is a child's toy, so a cry is rationed: once per event (hatch,
// evolution revealed, a farewell, stepping into a fight) and only sometimes on
// a caress, never twice inside a minute. The events are seen by cryWatch() in
// the sketch's loop, off the pet's state, so this drives the real loop and the
// real tap path and reads which cries were ASKED for (host_impl counts them;
// the playback itself is a buffer handed to I2S and has nothing to decide).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed=4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
void emuSetTimeScale(uint32_t);
bool cryOnCaress(uint32_t now);
void startTrainerBattle(uint8_t idx, bool hard);
extern Pet pet;
extern bool screenOff, battleOpen;
extern uint8_t dimStage;
extern int g_emuCries;
extern int16_t g_emuLastCry;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void pump(int n){ for(int i=0;i<n;i++){ std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void tap(int x,int y){
  g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt();
  pump(3);
  g_touchDown=false; emuFireInterrupt();
  pump(3);
}

int main(){
  setup();
  pump(5);
  ck(pet.isEgg() && g_emuCries == 0, "a fresh boot on an egg is silent");

  // hatch: the egg going away is the event, however it went
  pet.dbgHatchAs(4, false);
  pump(2);
  ck(g_emuCries == 1 && g_emuLastCry == 4, "hatching cries once, as the creature that hatched");
  pump(10);
  ck(g_emuCries == 1, "...and only once, not every frame after");

  // evolution: heard when the new form is REVEALED, not when the anim starts
  pet.ageMinutes = 60UL * 50; pet.careMistakes = 0;   // old enough to evolve
  pet.evolve();
  pump(2);
  ck(g_emuCries == 1, "no cry while the evolution is still animating");
  emuSetTimeScale(50);             // the anim is 5.2 s; let it run out
  pump(8);
  emuSetTimeScale(1);
  ck(!pet.evolving() && g_emuCries == 2 && g_emuLastCry == 5,
     "the evolution cries once at the end, as CHARMELEON");

  // a dark screen keeps it quiet: an evolution finishing in a pocket at night
  screenOff = true;
  pet.ageMinutes = 60UL * 50; pet.careMistakes = 0;   // old enough to evolve
  pet.evolve();
  emuSetTimeScale(50);
  pump(8);
  emuSetTimeScale(1);
  ck(!pet.evolving() && pet.speciesId == 6 && g_emuCries == 2,
     "nothing cries while the screen is off");
  screenOff = false;
  pump(3);
  ck(g_emuCries == 2, "...and the missed cry is not replayed when it comes back on");

  // stepping into a fight: both sides are heard
  int before = g_emuCries;
  startTrainerBattle(0, false);
  ck(battleOpen && g_emuCries == before + 2, "a fight opens with both creatures' cries");
  battleOpen = false;
  pump(3);

  // the caress rule itself, against a controlled clock
  {
    int n = 0;
    for (int i = 0; i < 500; i++) n += cryOnCaress(10000000u);
    ck(n <= 1, "never twice in a minute, however hard it is petted");
    n = 0;
    for (uint32_t i = 1; i <= 600; i++) n += cryOnCaress(20000000u + i * 61000u);
    // about one in six once the gap has passed
    ck(n >= 60 && n <= 150, "roughly one caress in six cries, given the time");
    printf("    (%d of 600 spaced caresses cried)\n", n);
  }

  // ...and the CALLER: tapping the creature on the main screen reaches it.
  // 30 taps in a few seconds: odds of none at 1-in-6 are under 1%, and with
  // the seed fixed the run is deterministic. More than one would break the gap.
  dimStage = 0;
  before = g_emuCries;
  for (int i = 0; i < 30; i++) tap(233, 250);
  ck(g_emuCries - before == 1 && g_emuLastCry == 6,
     "petting the creature cries, once, as itself");

  // a farewell is heard; the gap does not apply to events
  pet.startFarewell();
  pump(2);
  ck(g_emuCries - before == 2 && g_emuLastCry == 6, "a farewell cries");

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
