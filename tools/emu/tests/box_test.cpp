// The boxes (pcbox.h): a list in arrival order, in a file rather than NVS.
// This checks the list itself (order, duplicates, removal, the cap), that it
// survives a reboot -- including a power cut between writing the new file and
// swapping it in -- and that the two older stores (the 18-slot NVS box and the
// one-per-species Pokedex box file) are emptied INTO it exactly once.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "pcbox.h"
#include "dex.h"
#include <cstdio>
#include <unistd.h>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static PartyMon mk(int dex,int lvl){ PartyMon m; m.dex=dex; m.level=lvl;
  m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; return m; }
static int16_t dexAt(uint16_t i){ PartyMon m; return pcGet(i,m) ? m.dex : -1; }

int main(){
  // ---- the migration, from a save that has BOTH older stores ----
  // the 18-slot box in NVS, with experience under its own key
  { Preferences seed; seed.begin("tamapoke", false);
    PartyMon old[PARTY_SLOTS];
    for (int i=0;i<PARTY_SLOTS;i++) old[i]=mk(1+i*20, 30+i);
    seed.putBytes("party", old, sizeof(old));
    PartyMon box[BOX_SLOTS]; uint16_t bxp[BOX_SLOTS] = {0};
    box[0]=mk(150,70); bxp[0]=33; box[5]=mk(25,12);
    seed.putBytes("box", box, sizeof(box));
    seed.putBytes("bxp", bxp, sizeof(bxp));
    seed.end(); }
  // the Pokedex box file: a header, then one record per species in dex order
  { FILE *f = fopen("tamapoke-dexbox.bin","wb");
    struct { uint32_t magic; uint16_t version, recSize; } h = { 0x42445054u, 1, (uint16_t)sizeof(PartyMon) };
    fwrite(&h, sizeof(h), 1, f);
    for (int d=1; d<=DEX_COUNT; d++) {
      PartyMon m;                                   // an empty slot
      if (d==7 || d==131) m = mk(d, d==7 ? 9 : 40);
      fwrite(&m, sizeof(m), 1, f);
    }
    fclose(f); }
  party.begin();
  bool kept = true;
  for (int i=0;i<PARTY_SLOTS;i++) if (party.slots[i].dex != 1+i*20) kept=false;
  ck(kept, "the team is untouched by all of this");
  uint16_t moved = pcMigrate();
  ck(moved==4 && pcCount()==4, "both older stores move into the boxes (2 + 2)");
  ck(dexAt(0)==7 && dexAt(1)==131 && dexAt(2)==150 && dexAt(3)==25,
     "the Pokedex box first, in dex order, then the old box in slot order");
  { PartyMon m; uint16_t xp=0; pcGet(2,m,&xp);
    ck(m.level==70 && xp==33, "a creature keeps its level and its experience"); }
  ck(party.box[0].empty() && party.box[5].empty(), "the old box is emptied");
  { Preferences p; p.begin("tamapoke", true);
    ck(!p.isKey("box") && !p.isKey("bxp"), "and its NVS keys are gone, freeing the space"); p.end(); }
  ck(fopen("tamapoke-dexbox.bin","rb")==nullptr, "the Pokedex box file is deleted");
  ck(pcMigrate()==0 && pcCount()==4, "a second boot moves nothing: no duplicates");
  { Party again; again.begin();
    bool empty=true; for (auto &b: again.box) if (!b.empty()) empty=false;
    ck(empty, "the old box stays empty after a reload"); }

  // ---- the list ----
  pcAdd(mk(7, 20));
  ck(pcCount()==5 && dexAt(4)==7, "a second SQUIRTLE is welcome: duplicates are allowed");
  ck(pcHas(7) && pcHas(150) && !pcHas(1), "pcHas answers per species");
  pcTake(0);
  ck(pcCount()==4 && dexAt(0)==131 && dexAt(3)==7, "taking one moves the rest up, in order");
  ck(pcHas(7), "one SQUIRTLE left, so the species is still there");
  pcTake(3);
  ck(!pcHas(7), "and with the last one gone, it is not");
  pcSet(1, mk(9, 50), 12);
  { PartyMon m; uint16_t xp=0; pcGet(1,m,&xp);
    ck(m.dex==9 && m.level==50 && xp==12 && !pcHas(150) && pcHas(9), "a set replaces in place, index and all"); }
  PartyMon junk = mk(DEX_COUNT + 5, 3);
  ck(!pcAdd(junk) && pcCount()==3, "a record that is not a species is refused");

  // ---- a reboot ----
  pcReload();
  ck(pcCount()==3 && dexAt(0)==131 && dexAt(1)==9 && dexAt(2)==25, "the list survives a reboot");
  // the power went after the new file was written but before it was swapped in
  rename("tamapoke-pc.bin", "tamapoke-pc.tmp");
  pcReload();
  ck(pcCount()==3 && dexAt(1)==9, "a list found only as the temporary file is still read");
  pcAdd(mk(1, 5));
  pcReload();
  ck(pcCount()==4 && fopen("tamapoke-pc.tmp","rb")==nullptr, "and the next write puts it back in place");
  // the power went after the new file was written but BEFORE the old one was
  // removed: both exist, and the whole new one is the truth
  { FILE *a = fopen("tamapoke-pc.bin","rb"); FILE *b = fopen("tamapoke-pc.old","wb");
    char buf[4096]; size_t k; while ((k = fread(buf,1,sizeof(buf),a)) > 0) fwrite(buf,1,k,b);
    fclose(a); fclose(b); }
  pcAdd(mk(4, 5));                                // pc.bin now holds 5
  rename("tamapoke-pc.bin", "tamapoke-pc.tmp");   // the whole new 5 as the tmp...
  rename("tamapoke-pc.old", "tamapoke-pc.bin");   // ...and the OLD 4 where the real one goes
  pcReload();
  ck(pcCount()==5 && dexAt(4)==4, "a WHOLE temporary file beside the old one wins: it is the newer");
  // ...but one cut short while being written is not trusted
  { FILE *t = fopen("tamapoke-pc.tmp","r+b"); fseek(t, 0, SEEK_END); long sz = ftell(t); fclose(t);
    truncate("tamapoke-pc.tmp", sz - 10); }
  pcReload();
  ck(pcCount()==4, "a temporary file cut short is ignored, and the real one stands");
  pcAdd(mk(4, 5));                                // back to 5, cleanly
  pcReload();

  // ---- the cap ----
  while (pcCount() < PC_MAX) if (!pcAdd(mk(16, 2))) break;
  ck(pcCount()==PC_MAX, "the boxes fill to PC_MAX");
  ck(!pcAdd(mk(19, 2)) && pcCount()==PC_MAX, "and refuse past it rather than overflow");
  pcReload();
  ck(pcCount()==PC_MAX, "a full list reloads whole");

  pcWipe();
  pcReload();
  ck(pcCount()==0, "WIPE empties them for good");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
