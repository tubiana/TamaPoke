// The egg waits for the player.
//
// What is being held: an egg never hatches by itself -- not after hours of live
// ticks, not after two weeks switched off -- and does not age while it waits,
// so whatever the player hatches with three taps starts at level 1. The
// explore bar still refills meanwhile, and three taps really do hatch it
// (a guard phrased as "never hatches" needs proof the mechanism still works).
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include <cstdio>
uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  Pet p; p.begin();
  p.dbgHatchAs(4, false);              // past the starter screen
  const uint32_t T0 = 20700u * 86400u + 13 * 3600u;
  p.setClock(T0);
  p.newEgg();
  ck(p.isEgg() && !p.awaitingStarter(), "an egg is waiting");

  // --- live: four hours of ticks (one outing comes back every 200 awake minutes)
  p.expSpend(); p.expSpend(); p.expSpend();
  for (int i = 0; i < 240; i++) { p.lastSeenEpoch = T0 + i * 60; p.dbgTick(); }
  ck(p.isEgg(), "four hours of live ticks and it is still an egg");
  ck(p.expCharges() > 0, "while the explore bar keeps refilling");

  // --- switched off for two weeks
  p.setClock(T0 + 3 * 3600);
  p.syncClock(T0 + 3 * 3600 + 14u * 86400u);
  ck(p.isEgg(), "two weeks switched off and it is still an egg");

  // --- the player hatches it
  p.eggTap(); p.eggTap();
  ck(p.isEgg(), "two taps are not enough");
  p.eggTap();
  ck(!p.isEgg(), "the third tap hatches it");
  ck(p.level() == 1, "at level 1, however long it waited");

  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
