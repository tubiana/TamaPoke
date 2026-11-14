#include "pcbox.h"
#include "dex.h"
#include "moves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One stdio path on both sides: ESP-IDF puts a mounted FAT partition behind
// the ordinary C file API, so the board and the emulator run the same code and
// differ only in where the file lives.
#if defined(ESP32)
#include <FFat.h>
static const char *PATH = "/ffat/pc.bin";
static const char *TMP = "/ffat/pc.tmp";
static const char *OLD_DEXBOX = "/ffat/dexbox.bin";
static bool mountFs() {
  static int8_t st = -1;
  // formatOnFail: the very first mount on a board formats the partition, which
  // takes a few seconds once.
  if (st < 0) {
    st = FFat.begin(true) ? 1 : 0;
    Serial.printf("pc: FFat %s\n", st ? "mounted" : "MOUNT FAILED");
  }
  return st == 1;
}
static void *bigAlloc(size_t n) { return ps_malloc(n); }
#else
static const char *PATH = "tamapoke-pc.bin";
static const char *TMP = "tamapoke-pc.tmp";
static const char *OLD_DEXBOX = "tamapoke-dexbox.bin";
static bool mountFs() { return true; }
static void *bigAlloc(size_t n) { return malloc(n); }
#endif

#define PC_MAGIC 0x43505054u   // "TPPC"
struct PcHead { uint32_t magic; uint16_t version; uint16_t recSize; uint16_t count; uint16_t pad; };
struct PcRec { PartyMon mon; uint16_t xp; };

static bool loaded = false, ok = false;
static PcRec *recs = nullptr;
static uint16_t n = 0;
static uint8_t has[(DEX_COUNT + 7) / 8];

static void reindex() {
  memset(has, 0, sizeof(has));
  for (uint16_t i = 0; i < n; i++) {
    int16_t d = recs[i].mon.dex;
    if (d >= 1 && d <= DEX_COUNT) has[(d - 1) >> 3] |= 1 << ((d - 1) & 7);
  }
}

// a record off the wire or out of an older file: never index a table with junk
static bool sane(PartyMon &m) {
  if (m.dex < 1 || m.dex > DEX_COUNT) return false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (m.moves[i] >= MOVE_COUNT) m.moves[i] = 0;
  m.nick[sizeof(m.nick) - 1] = 0;
  return true;
}

// True only for a WHOLE file: every record its header promises. A file cut
// short by a power failure reads as false; the caller then reads the other one,
// which starts the list again from empty.
static bool readFile(const char *path) {
  n = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  PcHead h;
  bool good = fread(&h, sizeof(h), 1, f) == 1 && h.magic == PC_MAGIC &&
              h.recSize >= sizeof(int16_t) && h.recSize <= 96;
  for (uint16_t i = 0; good && i < h.count; i++) {
    uint8_t buf[96];
    uint16_t xp = 0;
    good = fread(buf, h.recSize, 1, f) == 1 && fread(&xp, sizeof(xp), 1, f) == 1;
    if (!good || n >= PC_MAX) continue;
    PcRec r;
    r.mon = PartyMon();
    memcpy(&r.mon, buf, h.recSize < sizeof(PartyMon) ? h.recSize : sizeof(PartyMon));
    r.xp = xp;
    if (sane(r.mon)) recs[n++] = r;
  }
  fclose(f);
  return good;
}

static void load() {
  if (loaded) return;
  loaded = true;
  if (!mountFs()) return;
  if (!recs) recs = (PcRec *)bigAlloc(sizeof(PcRec) * PC_MAX);
  if (!recs) return;
  ok = true;
  // The tmp file is only ever the NEWER copy: it is renamed over the real one
  // as the last step. Found WHOLE, the power went before that step, so it wins;
  // found cut short, it went while writing, and the real one still stands.
  if (!readFile(TMP)) readFile(PATH);
  reindex();
}

static bool store() {
  FILE *f = fopen(TMP, "wb");
  if (!f) return false;
  PcHead h = { PC_MAGIC, 1, (uint16_t)sizeof(PartyMon), n, 0 };
  bool good = fwrite(&h, sizeof(h), 1, f) == 1;
  for (uint16_t i = 0; good && i < n; i++)
    good = fwrite(&recs[i].mon, sizeof(PartyMon), 1, f) == 1 &&
           fwrite(&recs[i].xp, sizeof(uint16_t), 1, f) == 1;
  good = (fclose(f) == 0) && good;
  if (!good) { remove(TMP); return false; }
  remove(PATH);                     // FAT will not rename over an existing file
  return rename(TMP, PATH) == 0;
}

void pcReload() { loaded = false; ok = false; n = 0; }

bool pcReady() { load(); return ok; }
uint16_t pcCount() { load(); return n; }

bool pcGet(uint16_t i, PartyMon &out, uint16_t *xp) {
  load();
  if (i >= n) return false;
  out = recs[i].mon;
  if (xp) *xp = recs[i].xp;
  return true;
}

bool pcAdd(const PartyMon &m, uint16_t xp) {
  load();
  if (!ok || n >= PC_MAX) return false;
  PcRec r = { m, xp };
  if (!sane(r.mon)) return false;
  recs[n++] = r;
  if (!store()) { n--; return false; }
  reindex();
  return true;
}

bool pcSet(uint16_t i, const PartyMon &m, uint16_t xp) {
  load();
  if (i >= n) return false;
  PcRec r = { m, xp }, was = recs[i];
  if (!sane(r.mon)) return false;
  recs[i] = r;
  if (!store()) { recs[i] = was; return false; }
  reindex();
  return true;
}

bool pcTake(uint16_t i) {
  load();
  if (i >= n) return false;
  PcRec was = recs[i];
  memmove(&recs[i], &recs[i + 1], (n - i - 1) * sizeof(PcRec));
  n--;
  if (!store()) {                   // put it back exactly where it was
    memmove(&recs[i + 1], &recs[i], (n - i) * sizeof(PcRec));
    recs[i] = was;
    n++;
    return false;
  }
  reindex();
  return true;
}

bool pcHas(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  load();
  return has[(dex - 1) >> 3] & (1 << ((dex - 1) & 7));
}

void pcWipe() {
  load();
  n = 0;
  remove(PATH);
  remove(TMP);
  remove(OLD_DEXBOX);
  reindex();
}

// The old Pokedex box: a header, then one record per species in dex order,
// an empty slot being a record whose dex is not its own.
struct DbHead { uint32_t magic; uint16_t version; uint16_t recSize; };
#define DB_MAGIC 0x42445054u   // "TPDB"

uint16_t pcMigrate() {
  load();
  if (!ok) return 0;
  uint16_t was = n;
  FILE *f = fopen(OLD_DEXBOX, "rb");
  bool fromFile = false;
  if (f) {
    DbHead h;
    if (fread(&h, sizeof(h), 1, f) == 1 && h.magic == DB_MAGIC && h.recSize >= sizeof(int16_t) &&
        h.recSize <= 96) {
      uint8_t buf[96];
      size_t keep = h.recSize < sizeof(PartyMon) ? h.recSize : sizeof(PartyMon);
      for (int16_t d = 1; d <= DEX_COUNT && n < PC_MAX; d++) {
        if (fread(buf, h.recSize, 1, f) != 1) break;
        PcRec r = { PartyMon(), 0 };
        memcpy(&r.mon, buf, keep);
        if (r.mon.dex == d && sane(r.mon)) recs[n++] = r;
      }
    }
    fclose(f);
    fromFile = true;
  }
  bool fromBox = false;
  for (int i = 0; i < BOX_SLOTS; i++) {
    if (party.box[i].empty()) continue;
    fromBox = true;
    PcRec r = { party.box[i], party.boxXp[i] };
    if (n < PC_MAX && sane(r.mon)) recs[n++] = r;
  }
  if (!fromFile && !fromBox) return 0;
  if (n != was && !store()) { n = was; return 0; }   // nothing cleared: try again next boot
  reindex();
  if (fromFile) remove(OLD_DEXBOX);
  if (fromBox) party.boxClear();
  return n - was;
}
