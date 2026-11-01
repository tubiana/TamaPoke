// The Pokedex can be finished.
//
// What is being held: the totals on screen count only species a player can
// own (dexObtainableIn), and a player who has registered everything reads
// n/n -- overall and in every region. Before this, 19 species with no art
// anywhere were in every total and could never be registered, so no Pokedex
// could ever reach 809/809 and Kalos could never reach its own total.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "noart.h"
#include <cstdio>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  ck(NO_ART_COUNT > 0, "there ARE species with no art (else this proves nothing)");
  ck(dexObtainable() == DEX_COUNT - NO_ART_COUNT, "the total leaves out exactly the species with no art");

  Pet p; p.begin();
  for (int16_t d = 1; d <= DEX_COUNT; d++)        // everything, the art-less ones too
    p.registerCaught(d, false);
  char w[96];
  snprintf(w, sizeof(w), "a full Pokedex reads %u/%u", p.registeredCount(), dexObtainable());
  ck(p.registeredCount() == dexObtainable(), w);
  bool each = true;
  for (int r = 0; r < REGION_COUNT; r++) {
    if (!REGIONS[r].lo) continue;
    uint16_t got = p.registeredCountIn(REGIONS[r].lo, REGIONS[r].hi);
    uint16_t all = dexObtainableIn(REGIONS[r].lo, REGIONS[r].hi);
    if (got != all) { printf("      %s: %u/%u\n", REGIONS[r].name, got, all); each = false; }
  }
  ck(each, "and so does every region");
  printf(bad ? "\n%d FAILED\n" : "\nALL PASS\n", bad);
  return bad ? 1 : 0;
}
