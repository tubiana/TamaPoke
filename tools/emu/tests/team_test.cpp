// The team screen in a fight, and the two endings that got a scene.
//
// What is held here:
// - POKEMON shows the WHOLE squad: the 5th and 6th can be sent in. The old
//   switch list was the move grid's four cells, and they could not.
// - When yours faints with others standing, the player CHOOSES who is next,
//   and cannot back out of choosing. That holds when the one that fainted is
//   the LAST of the squad -- which used to lose the fight outright with the
//   others still standing.
// - Sending in after a faint costs no turn: the foe does not act.
// - A LAN fight that really ended shows its win or lose scene, which a stray
//   tap cannot skip and a later tap leaves; a peer that went quiet does not.
// - A trade plays its swap when theirs arrives, a tap skips it, and an
//   evolution on arrival makes it longer.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void startBattle(int16_t dex, uint8_t lvl);
void battleTap(int16_t x, int16_t y);
int btlTeamHit(int16_t x, int16_t y);
void tradePoll(); void tradeTap(int16_t x, int16_t y);
bool tradeAnimating(); uint32_t tradeAnimLen();
extern Pet pet;
extern Link lan;
extern Combatant btlYou, btlFoe, btlSquad[];
extern uint8_t btlSquadN, btlSquadAt, btlMenu, btlMsgCount;
extern bool battleOpen, btlOver, btlWon, btlLink, btlLinkHost, btlMustPick, btlLinkLost, lanOpen, tradeOpen;
extern uint32_t btlLanEndT0, tradeAnimT0;
extern int8_t btlSwapWho;
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static PartyMon mk(int16_t d, uint16_t lv){ PartyMon m; makeMon(d, lv, false, m); return m; }

// the centre of the cell the firmware says holds squad member i
static bool cellOf(int i, int16_t &cx, int16_t &cy) {
  long sx = 0, sy = 0, n = 0;
  for (int y = 0; y < 466; y += 2) for (int x = 0; x < 466; x += 2)
    if (btlTeamHit(x, y) == i) { sx += x; sy += y; n++; }
  if (!n) return false;
  cx = (int16_t)(sx / n); cy = (int16_t)(sy / n);
  return true;
}
static void clearWords() { for (int i = 0; i < 20 && btlMsgCount; i++) battleTap(233, 233); }

int main(){
  setup();
  for (int i=0;i<3;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4,false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 60UL * 20;
  pet.sleeping = false;
  for (int i=0;i<PARTY_SLOTS;i++) party.replaceAt(i, mk(10 + i * 3, 20));

  // --- the whole squad on the team screen
  startBattle(19, 5);
  clearWords();
  ck(btlSquadN == 6, "a squad of six");
  btlMenu = 2;
  render();
  int16_t x, y;
  bool all = true;
  for (int i = 0; i < btlSquadN; i++) if (!cellOf(i, x, y)) all = false;
  ck(all, "the team screen has a cell for every one of the six");
  ck(cellOf(5, x, y), "the sixth has a cell");
  battleTap(x, y);
  clearWords();
  ck(btlSquadAt == 5, "and the sixth can be sent in");

  // --- the last of the squad faints: the player chooses, the fight goes on
  int16_t foeDex = 150;                     // a level 100 MEWTWO knocks anything out
  bool picked = false, lost = false;
  for (int round = 0; round < 12 && !picked; round++) {
    startBattle(foeDex, 100);
    clearWords();
    btlMenu = 2; cellOf(5, x, y); battleTap(x, y); clearWords();   // the last one out
    for (int turn = 0; turn < 10 && !btlMustPick && !btlOver; turn++) {
      btlYou.hp = 1;
      btlFoe.hp = btlFoe.maxHp;
      btlMenu = 1;
      battleTap(69 + 40, 286 + 22);         // move cell 0
      clearWords();
    }
    if (btlOver) { lost = true; continue; }
    picked = btlMustPick;
  }
  ck(picked, "when the last of the squad faints, the team screen asks who is next");
  ck(!lost, "instead of losing with five still standing");
  ck(btlMenu == 2 && battleOpen && !btlOver, "and the fight is still on");
  battleTap(233, 400);                       // where BACK would be
  ck(btlMustPick && btlMenu == 2, "there is no backing out of the choice");
  cellOf(5, x, y); battleTap(x, y);
  ck(btlMustPick && btlSquadAt == 5, "the fainted one cannot be chosen");
  uint16_t foeHp = btlFoe.hp;
  cellOf(2, x, y); battleTap(x, y);
  ck(!btlMustPick && btlSquadAt == 2, "a standing one can: it goes out");
  clearWords();
  ck(btlFoe.hp == foeHp && !btlYou.fainted(), "and the replacement costs no turn");
  btlMenu = 2;                               // POKEMON, by choice this time
  cellOf(5, x, y); battleTap(x, y);
  ck(btlSquadAt == 2 && btlMenu == 2, "a fainted one that is not out cannot be sent in either");
  btlMenu = 0;

  // --- a LAN fight's ending
  startBattle(19, 5); clearWords();
  btlLink = true; btlLinkHost = true; btlOver = true; btlWon = true; btlLinkLost = false;
  btlMsgCount = 1;                           // its last words
  battleTap(233, 233);
  ck(btlLanEndT0 != 0 && battleOpen, "a won LAN fight shows its scene");
  render();
  battleTap(233, 233);
  ck(btlLanEndT0 != 0, "the very next tap does not skip it");
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  battleTap(233, 233);
  ck(btlLanEndT0 == 0 && !battleOpen && lanOpen && !btlLink, "a later tap goes back to the LAN screen");

  startBattle(19, 5); clearWords();
  btlLink = true; btlOver = true; btlWon = false; btlLinkLost = false;
  btlMsgCount = 1;
  battleTap(233, 233);
  ck(btlLanEndT0 != 0, "a lost LAN fight has its scene too");
  render();
  btlLanEndT0 = 0; battleOpen = false; btlLink = false; lanOpen = false;

  startBattle(19, 5); clearWords();
  btlLink = true; btlLinkHost = true; btlLinkLost = false;
  lan.state = LINK_LOST;                     // the radio went quiet mid-fight
  render();                                  // render() polls the link
  ck(btlOver && btlMsgCount > 0, "a silent peer ends the fight");
  clearWords();
  ck(btlLanEndT0 == 0 && !battleOpen, "a peer that went quiet ends with no scene");
  btlLink = false;

  // --- the trade swap
  tradeOpen = true; lanOpen = true;
  lan.tMine = mk(25, 20);
  lan.tTheirs = mk(1, 10);
  lan.tArrived = true;
  tradePoll();
  ck(tradeAnimating(), "theirs arriving starts the swap");
  uint32_t plain = tradeAnimLen();
  render();
  tradeTap(233, 233);
  ck(!tradeAnimating(), "a tap skips it");
  lan.tTheirs = mk(64, 20);                   // KADABRA evolves on arrival
  lan.tArrived = true;
  tradePoll();
  ck(tradeAnimating() && tradeAnimLen() > plain, "an evolution on arrival makes it longer");
  tradeAnimT0 = 0;

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
