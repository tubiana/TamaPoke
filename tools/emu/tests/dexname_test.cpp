// Species names follow the UI language.
//
// They used to be English everywhere: DEX_TBL[].name was printed directly by
// every screen, so a French player read CHARMANDER -- and only noticed SOME of
// the time, because PIKACHU, MEWTWO and ONIX are spelt the same in both.
// dexName() is now the one answer, and the battle builds its combatants
// through it, so the test drives that CALLER and not only the table.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static const char *LANG_NAME[] = { "ES", "EN", "FR", "DE", "IT", "PT" };

// the font has A-Z, 0-9 and a little punctuation; anything else is garbage
static bool fontSafe(const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
          *p == ' ' || *p == '.' || *p == '-')) return false;
  return true;
}

int main(){
  // every species, every language: present, drawable, and fits a battle name
  int unsafe = 0, tooLong = 0, translated[LANG_COUNT] = { 0 };
  for (int l = 0; l < LANG_COUNT; l++) {
    gLang = (Lang)l;
    for (int d = 1; d <= DEX_COUNT; d++) {
      const char *n = dexName(d);
      if (!n || !*n || !fontSafe(n)) {
        if (unsafe++ < 5) printf("  bad %s #%d \"%s\"\n", LANG_NAME[l], d, n ? n : "(null)");
        continue;
      }
      if (strlen(n) >= sizeof(((Combatant *)0)->name)) {
        if (tooLong++ < 5) printf("  long %s #%d \"%s\"\n", LANG_NAME[l], d, n);
      }
      if (strcmp(n, DEX_TBL[d].name)) translated[l]++;
    }
  }
  ck(unsafe == 0, "every name in every language uses only the font's characters");
  ck(tooLong == 0, "every name fits Combatant::name without truncating");

  // ...and the table actually engaged: a check above that passes on an empty
  // table proves nothing
  ck(translated[LANG_EN] == 0, "English is the canonical table, untouched");
  ck(translated[LANG_PT] == 0, "Portuguese uses the English names, as its games do");
  ck(translated[LANG_FR] > 500, "French translates most of the dex");
  ck(translated[LANG_DE] > 500, "German translates most of the dex");

  gLang = LANG_FR;
  ck(!strcmp(dexName(4), "SALAMECHE"), "FR: CHARMANDER is SALAMECHE");
  ck(!strcmp(dexName(133), "EVOLI"), "FR: EEVEE is EVOLI (accent folded)");
  ck(!strcmp(dexName(29), "NIDORAN F"), "FR: the female symbol becomes F");
  ck(!strcmp(dexName(25), "PIKACHU"), "FR: PIKACHU falls back to the shared name");
  ck(!strcmp(dexName(0), "?") && !strcmp(dexName(DEX_COUNT + 1), "?"),
     "out of range is ? rather than a read off the end");
  gLang = LANG_DE;
  ck(!strcmp(dexName(4), "GLUMANDA"), "DE: CHARMANDER is GLUMANDA");
  gLang = (Lang)200;
  ck(!strcmp(dexName(4), "CHARMANDER"), "a corrupt language falls back to the default");

  // the CALLER: what a battle narrates is built through dexName()
  {
    Pet p; p.dbgHatchAs(4, false);      // never begin()'d, so it cannot save
    Combatant c;
    gLang = LANG_FR; combatantFromPet(c, p);
    ck(!strcmp(c.name, "SALAMECHE"), "battle: the live pet is named in French");
    gLang = LANG_EN; combatantFromPet(c, p);
    ck(!strcmp(c.name, "CHARMANDER"), "battle: and in English when the UI is");
    PartyMon m{}; m.dex = 6; m.level = 50;
    gLang = LANG_FR; combatantFromParty(c, m);
    ck(!strcmp(c.name, "DRACAUFEU"), "battle: a banked member is named in French");
    p.rename("BOB");
    combatantFromPet(c, p);
    ck(!strcmp(c.name, "BOB"), "battle: a nickname still wins over any language");
  }
  // a 12-character name arrives whole (it was clipped to 11 before)
  {
    PartyMon m{}; m.dex = 740; m.level = 50;     // CRABOMINABLE
    Combatant c; gLang = LANG_EN; combatantFromParty(c, m);
    ck(!strcmp(c.name, DEX_TBL[740].name) && strlen(c.name) == 12,
       "battle: a 12-letter name is not clipped");
  }

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
