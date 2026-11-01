#include "dexbox.h"
#include "dex.h"
#include "moves.h"
#include <stdio.h>
#include <string.h>

// One stdio path on both sides: ESP-IDF puts a mounted FAT partition behind
// the ordinary C file API, so the board and the emulator run the same code and
// differ only in where the file lives.
#if defined(ESP32)
#include <FFat.h>
static const char *PATH = "/ffat/dexbox.bin";
static bool mountFs() {
  static int8_t st = -1;
  // formatOnFail: the very first mount on a board formats the partition, which
  // takes a few seconds once. Only reached when the box is first used.
  if (st < 0) {
    st = FFat.begin(true) ? 1 : 0;
    Serial.printf("dexbox: FFat %s\n", st ? "mounted" : "MOUNT FAILED");
  }
  return st == 1;
}
#else
static const char *PATH = "tamapoke-dexbox.bin";
static bool mountFs() { return true; }
#endif

#define DB_MAGIC 0x42445054u   // "TPDB"
struct DbHead { uint32_t magic; uint16_t version; uint16_t recSize; };

static bool loaded = false;
static bool ok = false;
static uint16_t recSize = sizeof(PartyMon);
static uint8_t used[(DEX_COUNT + 7) / 8];

static long offOf(int16_t dex) { return (long)sizeof(DbHead) + (long)(dex - 1) * recSize; }

static void load() {
  if (loaded) return;
  loaded = true;
  memset(used, 0, sizeof(used));
  if (!mountFs()) return;
  ok = true;
  FILE *f = fopen(PATH, "rb");
  if (!f) return;                       // nothing caught yet: an empty box
  DbHead h;
  if (fread(&h, sizeof(h), 1, f) != 1 || h.magic != DB_MAGIC || h.recSize < sizeof(int16_t)) {
    fclose(f);
    return;
  }
  recSize = h.recSize;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    int16_t got = 0;
    if (fseek(f, offOf(d), SEEK_SET) != 0 || fread(&got, sizeof(got), 1, f) != 1) break;
    if (got == d) used[(d - 1) >> 3] |= 1 << ((d - 1) & 7);
  }
  fclose(f);
}

bool dexBoxReady() { load(); return ok; }

bool dexBoxHas(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  load();
  return used[(dex - 1) >> 3] & (1 << ((dex - 1) & 7));
}

bool dexBoxGet(int16_t dex, PartyMon &out) {
  if (!dexBoxHas(dex)) return false;
  FILE *f = fopen(PATH, "rb");
  if (!f) return false;
  out = PartyMon();
  size_t n = recSize < sizeof(PartyMon) ? recSize : sizeof(PartyMon);
  bool good = fseek(f, offOf(dex), SEEK_SET) == 0 && fread(&out, n, 1, f) == 1;
  fclose(f);
  if (!good || out.dex != dex) return false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (out.moves[i] >= MOVE_COUNT) out.moves[i] = 0;   // never index MOVE_TBL with junk
  out.nick[sizeof(out.nick) - 1] = 0;
  return true;
}

// Writes one record. The file is created whole the first time, header and all,
// so every later write is an in-place overwrite at a fixed offset.
static bool writeRec(int16_t dex, const PartyMon &m) {
  load();
  if (!ok || dex < 1 || dex > DEX_COUNT) return false;
  FILE *f = fopen(PATH, "r+b");
  if (!f) {
    f = fopen(PATH, "w+b");
    if (!f) return false;
    recSize = sizeof(PartyMon);
    DbHead h = { DB_MAGIC, 1, (uint16_t)recSize };
    fwrite(&h, sizeof(h), 1, f);
    PartyMon blank;
    for (int16_t d = 1; d <= DEX_COUNT; d++) fwrite(&blank, recSize, 1, f);
  }
  // a record of the CURRENT size, padded or cut to the file's own
  uint8_t buf[64];
  memset(buf, 0, sizeof(buf));
  size_t n = recSize < sizeof(buf) ? recSize : sizeof(buf);
  memcpy(buf, &m, n < sizeof(PartyMon) ? n : sizeof(PartyMon));
  bool good = fseek(f, offOf(dex), SEEK_SET) == 0 && fwrite(buf, n, 1, f) == 1;
  fclose(f);
  return good;
}

bool dexBoxPut(const PartyMon &m) {
  if (m.dex < 1 || m.dex > DEX_COUNT) return false;
  if (!writeRec(m.dex, m)) return false;
  used[(m.dex - 1) >> 3] |= 1 << ((m.dex - 1) & 7);
  return true;
}

void dexBoxTake(int16_t dex) {
  if (!dexBoxHas(dex)) return;
  if (writeRec(dex, PartyMon())) used[(dex - 1) >> 3] &= ~(1 << ((dex - 1) & 7));
}

uint16_t dexBoxCount() {
  load();
  uint16_t n = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) n += (used[(d - 1) >> 3] >> ((d - 1) & 7)) & 1;
  return n;
}

void dexBoxWipe() {
  load();
  remove(PATH);
  memset(used, 0, sizeof(used));
  recSize = sizeof(PartyMon);
}
