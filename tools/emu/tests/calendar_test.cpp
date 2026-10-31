// Calendar surprises, the birthday present and stickers -- on Pet itself.
//
// Dates are checked against values computed independently (Python's datetime),
// not against the algorithm's own output. Each effect is paired with the same
// roll on an ordinary day, so a lean that never engages cannot pass.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=1031; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// days since 1970-01-01, from Python's datetime
#define D_2026_10_31 20757u
#define D_2026_10_24 20750u
#define D_2026_12_23 20810u
#define D_2026_12_24 20811u
#define D_2027_01_01 20819u
#define D_2024_02_29 19782u
#define D_2025_02_28 20147u
#define D_2026_02_27 20511u
#define D_2026_06_21 20625u
#define D_2027_10_31 21122u
#define D_2026_07_14 20648u
static uint32_t noon(uint32_t day){ return day * 86400u + 12 * 3600u; }
static bool is(CalDate c, int y, int m, int d){ return c.year==y && c.month==m && c.day==d; }

static bool hasType(int16_t d, uint8_t t){ return DEX_TBL[d].type1==t || DEX_TBL[d].type2==t; }

int main(){
  // ---- dates ----
  ck(is(calFromDays(0),1970,1,1), "day 0 is 1 January 1970");
  ck(is(calFromDays(D_2026_10_31),2026,10,31) && is(calFromDays(D_2024_02_29),2024,2,29)
     && is(calFromDays(D_2027_01_01),2027,1,1), "known dates convert exactly");
  {
    // walk forty years a day at a time: every step is the next day of a real calendar
    int broken = 0;
    CalDate prev = calFromDays(0);
    for (uint32_t d = 1; d < 40u * 366; d++) {
      CalDate c = calFromDays(d);
      bool next = (c.year==prev.year && c.month==prev.month && c.day==prev.day+1) ||
                  (c.day==1 && prev.day==calMonthDays(prev.month, prev.year) &&
                   ((c.month==prev.month+1 && c.year==prev.year) || (c.month==1 && prev.month==12 && c.year==prev.year+1)));
      if (!next && broken++ < 3) printf("    day %u: %u-%u-%u after %u-%u-%u\n", d, c.year,c.month,c.day, prev.year,prev.month,prev.day);
      prev = c;
    }
    ck(broken == 0, "forty years of days follow one another, leap years included");
  }

  // ---- which event, when ----
  ck(calEventOn(calFromDays(D_2026_10_31),0,0) == EV_HALLOWEEN, "31 Oct is Halloween");
  ck(calEventOn(calFromDays(D_2026_10_24),0,0) == EV_NONE, "...and 24 Oct is not yet");
  ck(calEventOn(calFromDays(D_2026_12_23),0,0) == EV_WINTER && calEventOn(calFromDays(D_2026_12_24),0,0) == EV_CHRISTMAS,
     "winter hands over to Christmas on the 24th");
  ck(calEventOn(calFromDays(D_2027_01_01),0,0) == EV_NEWYEAR, "1 Jan is New Year");
  ck(calEventOn(calFromDays(D_2026_02_27),0,0) == EV_POKEDAY, "27 Feb is Pokemon Day");
  ck(calEventOn(calFromDays(D_2026_06_21),0,0) == EV_SUMMER, "21 Jun starts summer");
  ck(calEventOn(calFromDays(D_2026_07_14),0,0) == EV_NONE, "an ordinary day has nothing");
  ck(calEventOn(calFromDays(D_2026_10_31),10,31) == EV_BIRTHDAY, "a birthday on Halloween is a birthday");
  ck(calEventOn(calFromDays(D_2025_02_28),2,29) == EV_BIRTHDAY, "a 29 Feb birthday falls on the 28th in other years");
  ck(calEventOn(calFromDays(D_2024_02_29 - 1),2,29) == EV_NONE, "...but not in a leap year, which has its own 29th");
  {
    int days = 0;
    for (uint32_t d = D_2027_01_01 - 365; d < D_2027_01_01; d++) days += calEventOn(calFromDays(d),0,0) != EV_NONE;
    ck(days == 1+1+1+7+7+7+7+3+3, "a year has exactly the event days the table lists");
  }

  // ---- the egg lean ----
  {
    auto rollGhosts = [](uint32_t day, int *rare) {
      Pet p; p.dbgHatchAs(4,false);    // registered, so not the first-egg starter roll
      p.lastSeenEpoch = noon(day);
      p.region = 0;                    // Kanto, which has Gastly's line among its eggs
      int g = 0; *rare = 0;
      for (int i = 0; i < 2000; i++) {
        int16_t d = p.pickEggSpecies();
        g += hasType(d, T_GHOST);
        *rare += DEX_TBL[d].rarity >= R_RARO;
      }
      return g;
    };
    int rareH, rareN;
    int gh = rollGhosts(D_2026_10_31, &rareH);
    int gn = rollGhosts(D_2026_07_14, &rareN);
    printf("    (2000 eggs: Ghost %d at Halloween, %d on an ordinary day; rare+ %d vs %d)\n", gh, gn, rareH, rareN);
    ck(gn < 200, "an ordinary day's eggs are rarely Ghost");
    // half the rolls lean, but only within the tier already rolled -- a rare or
    // legendary tier with no Ghost stays ordinary -- so well over a third, not half
    ck(gh > 500, "at Halloween a big share of the eggs lean Ghost");
    ck(rareH > rareN - 150, "the lean never trades a rare egg for a common one");
  }

  // ---- stickers ----
  {
    Pet p; p.dbgHatchAs(4,false);
    p.lastSeenEpoch = noon(D_2026_07_14);
    p.dbgTick();
    ck(!p.hasSticker(STK_HALLOWEEN) && p.stickers == 0, "nothing on an ordinary day with one species");
    p.lastSeenEpoch = noon(D_2026_10_31);
    p.dbgTick();
    ck(p.hasSticker(STK_HALLOWEEN) && p.newSticker == STK_HALLOWEEN, "being there at Halloween earns its sticker, announced");
    ck(!p.hasSticker(STK_BIRTHDAY), "...and not the birthday's, which comes with the present");
    for (int i = 0; i < 10; i++) p.quizAnswered(true);
    ck(p.hasSticker(STK_QUIZ10) && !p.hasSticker(STK_QUIZ50), "ten right answers earn QUIZ 10");
    for (int d = 1; d <= 10; d++) { p.dbgHatchAs(d, false); }
    p.dbgTick();
    ck(p.hasSticker(STK_DEX10), "ten species earn DEX 10");
    p.dbgHatchAs(25, true);
    p.dbgTick();
    ck(p.hasSticker(STK_SHINY), "a shiny in the Pokedex earns SHINY");
  }

  // ---- the birthday present ----
  {
    Pet p; p.begin();
    for (int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i);
    for (int i=0;i<BOX_SLOTS;i++) party.boxReleaseAt(i);
    p.dbgHatchAs(4,false);
    p.ageMinutes = 60UL * 29;          // level 30
    p.lastSeenEpoch = noon(D_2026_10_31);
    ck(!p.giftReady(), "no birthday set, no present");
    p.setBirthday(10, 31);
    ck(p.eventToday() == EV_BIRTHDAY && p.giftReady(), "on the birthday a present waits");
    int16_t g = p.openGift();
    const PartyMon &m = party.slots[0];
    ck(g > 0 && m.dex == g && m.shiny && m.level == p.level(), "opening it puts a shiny of the creature's level in the party");
    ck(DEX_TBL[g].rarity == R_COMUN || DEX_TBL[g].rarity == R_RARO, "a base form, never a legendary");
    ck(p.isRegistered(g) && p.isShinyRegistered(g), "it is in the Pokedex, shiny");
    ck(m.moves[0] != 0, "it arrives with moves, not as a blank");
    ck(p.hasSticker(STK_BIRTHDAY) && !p.giftReady(), "the birthday sticker, and no second present today");
    p.lastSeenEpoch += 3600;
    ck(!p.giftReady() && p.openGift() == 0, "not later the same day either");
    p.lastSeenEpoch = noon(D_2027_10_31);
    ck(p.giftReady(), "a year later there is a new one");
    // full party and box: the present waits instead of vanishing
    for (int i=0;i<PARTY_SLOTS;i++){ PartyMon f; f.dex=7; f.level=5; party.replaceAt(i,f); }
    for (int i=0;i<BOX_SLOTS;i++){ PartyMon f; f.dex=7; f.level=5; party.box[i]=f; }
    ck(p.openGift() == 0 && p.giftReady(), "with no room it stays wrapped for later");
    party.releaseAt(2);
    ck(p.openGift() > 0 && !party.slots[2].empty(), "...and opens once a slot is free");
  }
  {
    // the choice prefers what the Pokedex is missing
    Pet p; p.begin();
    for (int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i);
    p.dbgHatchAs(4,false);
    p.setBirthday(7, 14);
    int fresh = 0;
    for (int y = 0; y < 20; y++) {
      // that year's 14 July, found by date rather than by adding 365s
      uint32_t day = D_2026_07_14 + (uint32_t)y * 365u - 5;
      while (!(calFromDays(day).month == 7 && calFromDays(day).day == 14)) day++;
      p.lastSeenEpoch = noon(day);
      uint16_t before = p.registeredCount();
      int16_t g = p.openGift();
      if (g && p.registeredCount() == before + 1) fresh++;
      for (int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i);
    }
    ck(fresh >= 18, "year after year the present is a species not seen before");
  }

  // ---- it is the player's: it persists ----
  {
    { Pet p; p.begin(); p.setBirthday(3, 9); p.stickers |= 1u << STK_SPRING; p.saveNow(); }
    Pet q; q.begin();
    ck(q.bdayMonth == 3 && q.bdayDay == 9 && q.hasSticker(STK_SPRING), "birthday and stickers survive a reboot");
    q.setBirthday(2, 31);
    ck(q.bdayMonth == 2 && q.bdayDay == 29, "a day past the month's end is clamped (29 Feb allowed)");
    q.setBirthday(0, 0);
    ck(q.bdayMonth == 0 && q.eventToday() != EV_BIRTHDAY, "and it can be cleared");
  }

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
