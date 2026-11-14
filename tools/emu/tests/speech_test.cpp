// The spoken UI (speech.cpp): the tokens a text is looked up by, the per-
// language packs under /voice/<lang>/, and the VOICE switch.
//
// The tokenizer has a twin in tools/gen_voice.py (tokens()); the two must agree
// or a pack's clips are never found. The cases below are the ones that differ
// from "split on spaces": abbreviations glued to numbers, leading zeros, and
// the two symbols turned into language-neutral words.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "i18n.h"
#include "speech.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
uint32_t millis(){return 0;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static std::string toks(const char *t){
  char out[160]; int16_t st[24];
  speechTokens(t, out, sizeof(out), st, 24);
  return out;
}

// A pack with one clip per phrase, as gen_voice.py writes it: VIX1, count,
// MAX_TOK, then (hash, chunk, pad, off, len) sorted by hash; clips in v00.bin.
struct E { uint32_t h; uint16_t ch, pad; uint32_t off, len; };
// a clip's length is its key's identity: 1000 + a hash of the key
static uint32_t lenOf(const char *k){ return 1000 + speechHash(k, (int)strlen(k)) % 997; }
static std::string plan(const char *text){
  uint32_t lens[32]; int n = speechPlan(text, lens, 32);
  std::string out;
  for (int i = 0; i < n; i++) out += std::to_string(lens[i]) + " ";
  return out;
}
static std::string clips(std::initializer_list<const char*> keys){
  std::string out;
  for (const char *k : keys) out += std::to_string(lenOf(k)) + " ";
  return out;
}
static void writePack(const char *dir, const char *lang, std::initializer_list<const char*> keys){
  std::string d = std::string(dir) + "/voice";
  mkdir(d.c_str(), 0755);
  d += std::string("/") + lang;
  mkdir(d.c_str(), 0755);
  std::vector<E> es;
  uint32_t off = 0;
  for (const char *k : keys) {
    uint32_t len = lenOf(k);
    es.push_back({ speechHash(k, (int)strlen(k)), 0, 0, off, len });
    off += len;
  }
  std::sort(es.begin(), es.end(), [](const E&a,const E&b){ return a.h < b.h; });
  FILE *f = fopen((d + "/index.bin").c_str(), "wb");
  uint32_t n = es.size(); uint8_t mt[4] = { 8, 0, 0, 0 };
  fwrite("VIX1", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(mt, 1, 4, f);
  fwrite(es.data(), sizeof(E), n, f);
  fclose(f);
  f = fopen((d + "/v00.bin").c_str(), "wb");
  std::vector<int8_t> pcm(off, 10);
  fwrite(pcm.data(), 1, pcm.size(), f);
  fclose(f);
}

int main(){
  // ---- the tokens (gen_voice.py's tokens() must make the same) ----
  ck(toks("Nv.12") == "nv 12", "an abbreviation glued to a number splits in two");
  ck(toks("POKEDEX 03/151") == "pokedex 3 slash 151", "a slash between numbers is the token 'slash', leading zeros go");
  ck(toks("FUERZA +5") == "fuerza plus 5", "a plus before a number is the token 'plus'");
  ck(toks("A/B") == "a b", "a slash between words is just a gap");
  ck(toks("  ...  ") == "", "punctuation alone says nothing");

  // ---- the packs: one folder per language, under $TAMAPOKE_VOICE ----
  char tmpl[] = "/tmp/voiceXXXXXX";
  const char *dir = mkdtemp(tmpl);
  setenv("TAMAPOKE_VOICE", dir, 1);
  writePack(dir, "fr", { "equipe", "boites" });
  writePack(dir, "nl", { "team", "0", "3", "4", "7", "47", "150", "300", "1000", "pokedex", "slash" });
  ck(!strcmp(speechLangDir(LANG_FR), "fr") && !strcmp(speechLangDir(LANG_NL), "nl"),
     "each UI language has its own folder");

  setLang(LANG_FR);
  speechSetEnabled(false);
  ck(speechHasPack(), "the French pack is found");
  ck(!speechReady() && !speechSay("EQUIPE"), "but with VOICE off nothing is said");
  speechSetEnabled(true);
  ck(speechReady() && speechSay("EQUIPE"), "VOICE on: the French pack says EQUIPE");
  ck(!speechSay("TEAM"), "and not a word only the Dutch pack has");

  setLang(LANG_NL);
  ck(speechReady() && speechSay("TEAM") && !speechSay("EQUIPE"),
     "switching to Dutch reads the Dutch pack, not the French one");

  // ---- numbers: 0..200 and the hundreds have clips, the rest is built ----
  ck(plan("150") == clips({ "150" }), "a number with its own clip is said whole");
  ck(plan("347") == clips({ "300", "47" }), "347 is said as 300 + 47");
  ck(plan("1047") == clips({ "1000", "47" }), "1047 is said as 1000 + 47");
  ck(plan("1150") == clips({ "1000", "150" }), "1150 is said as 1000 + 150, not 1000 + 100 + 50");
  ck(plan("0") == clips({ "0" }), "zero is said");
  ck(plan("374") == clips({ "3", "7", "4" }), "with a part missing (no 74, no 300.. here: 300 yes, 74 no) it falls back to digits");
  ck(plan("POKEDEX 47/150") == clips({ "pokedex", "47", "slash", "150" }), "and inside a sentence too");

  setLang(LANG_DE);
  ck(!speechHasPack() && !speechReady() && !speechSay("EQUIPE"),
     "a language with no pack on the card says nothing, rather than another language");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
