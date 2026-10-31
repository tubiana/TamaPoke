// Kid mode, the needs speed and the quiz allowance -- the rules, on Pet itself.
//
// Each guard is paired with a run WITHOUT it, so the check cannot pass because
// nothing happened: kid mode is shown to stop what a normal game does reach
// (zero needs, care mistakes, a runaway), and the slower speed is measured
// against the 100% run beside it.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=99; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// a fresh awake creature, full needs, at a daytime hour so it never auto-sleeps
static void fresh(Pet &p){
  p.dbgHatchAs(4,false);
  p.sleeping=false; p.screenIsOff=false;
  p.fullness=p.joy=p.energy=p.hygiene=100;
  p.careMistakes=0;
  p.lastSeenEpoch = 20000*86400UL + 12*3600;   // noon
}

int main(){
  // ---- the needs speed ----
  {
    Pet a, b;
    fresh(a); fresh(b);
    a.drainPct = 100; b.drainPct = 50;
    for (int i=0;i<30;i++){ a.dbgTick(); b.dbgTick(); }
    ck(a.fullness == 40, "100%: food drops 2 a minute, exactly as it always has");
    ck(b.fullness == 70, "50%: half as fast (30 minutes cost 30, not 60)");
    Pet c; fresh(c); c.drainPct = 25;
    for (int i=0;i<40;i++) c.dbgTick();
    ck(c.fullness == 80, "25%: a quarter as fast");
    printf("    (after 30 min: 100%%=%u 50%%=%u)\n", a.fullness, b.fullness);
  }
  // the offline catch-up honours it too: the board switched off is the commonest case
  {
    // syncClock() reads "seen" back from NVS, so these are opened one at a time
    uint8_t lost[2];
    for (int k=0;k<2;k++){
      Pet p; p.begin(); fresh(p); p.drainPct = k ? 50 : 100;
      p.dbgSetSeen(p.lastSeenEpoch);
      p.syncClock(p.lastSeenEpoch + 20*60);
      lost[k] = 100 - p.fullness;
    }
    ck(lost[0] > 0 && lost[1] * 2 == lost[0], "offline: 50% loses half what 100% loses");
    printf("    (20 min off: 100%%=-%u 50%%=-%u)\n", lost[0], lost[1]);
  }
  {
    Pet p; fresh(p);
    p.setDrainPct(10);  ck(p.drainPct == 25,  "the speed cannot go below 25%");
    p.setDrainPct(140); ck(p.drainPct == 100, "...or above 100%");
    p.setDrainPct(60);  ck(p.drainPct == 50,  "...and snaps to a 25% step");
  }

  // ---- kid mode ----
  // Without it, ten neglected hours reach zero, pile up mistakes and arm the runaway.
  {
    Pet p; fresh(p);
    for (int i=0;i<600;i++) p.dbgTick();
    ck(p.lowestStat()==0 && p.careMistakes>0 && p.canRunawayNow(),
       "normal mode: ten neglected hours end at zero, with mistakes, ready to run");
  }
  {
    Pet p; fresh(p); p.kidMode = true;
    for (int i=0;i<600;i++) p.dbgTick();
    ck(p.lowestStat() >= KID_FLOOR, "kid mode: no need ever drops below KID_FLOOR");
    ck(p.careMistakes == 0, "kid mode: neglect costs no care mistakes");
    ck(!p.canRunawayNow(), "kid mode: it never becomes ready to run away");
    printf("    (kid mode after 10 h: food=%u joy=%u ene=%u hyg=%u)\n", p.fullness, p.joy, p.energy, p.hygiene);
    p.dbgRunawayReady();   // forced to the brink by hand
    ck(!p.canRunawayNow(), "kid mode: not even when forced to zero");
  }
  {
    Pet p; fresh(p); p.kidMode = true;
    uint32_t t0 = p.lastSeenEpoch;
    p.syncClock(t0 + 12*3600);
    ck(p.lowestStat() >= KID_FLOOR, "kid mode: switched off for 12 h, the floor holds too");
  }
  {
    Pet p; fresh(p); p.kidMode = true;
    p.ageMinutes = 60UL*20;          // CHARMANDER evolves at 16
    p.fullness = 30;
    ck(!p.canEvolveNow(), "kid mode still asks for care: a hungry creature does not evolve");
  }

  // ---- the quiz allowance ----
  {
    Pet p; fresh(p);
    ck(p.quizLeft() == QUIZ_PER_DAY, "a fresh day has the full allowance");
    p.joy = 50;
    p.quizAnswered(true);
    ck(p.joy == 50 + QUIZ_JOY && p.quizLeft() == QUIZ_PER_DAY - 1,
       "a right answer spends one and cheers the creature");
    p.quizAnswered(false);
    ck(p.joy == 50 + QUIZ_JOY, "a wrong one does not change its mood");
    for (int i=0;i<20;i++) p.quizAnswered(false);
    ck(p.quizLeft() == 0, "after QUIZ_PER_DAY there are none left, and it does not wrap");
    p.lastSeenEpoch += 86400;
    ck(p.quizLeft() == QUIZ_PER_DAY, "the next day it is full again");
  }

  // ---- all three are the PLAYER's: they persist and outlive the creature ----
  {
    { Pet p; p.begin(); fresh(p);
      p.setKidMode(true); p.setDrainPct(50);
      p.quizAnswered(true); p.quizAnswered(true); p.saveNow(); }
    Pet q; q.begin();
    ck(q.kidMode && q.drainPct == 50, "kid mode and the speed survive a reboot");
    q.setClock(20000*86400UL + 13*3600);   // the RTC, as setup() does: same day
    ck(q.quizLeft() == QUIZ_PER_DAY - 2, "today's quiz count survives a reboot");
    q.startFarewell();               // the creature leaves; the settings must not
    q.dbgHatchAs(7,false);
    ck(q.kidMode && q.drainPct == 50, "...and a new creature");
  }
  {
    // a corrupt stored speed falls back to normal instead of freezing the needs
    Preferences pr; pr.begin("tamapoke", false); pr.putUChar("drn", 0); pr.end();
    Pet q; q.begin();
    ck(q.drainPct == 100, "a corrupt speed (0) loads as 100%, never as frozen needs");
  }

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
