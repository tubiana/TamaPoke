#pragma once
#include <Arduino.h>
#include <Preferences.h>

// The party: pets that finished their life and were kept, rather than being
// dissolved into a single Pokedex bit like every previous ending did.
//
// Only the two endings the player CHOOSES bank a pet -- farewell and release.
// A runaway does not: it is the game's one punishing outcome, and letting a
// neglected pet come back as a team member would take the sting out of it.
#define PARTY_SLOTS 6
// The box: storage beyond the six that fight. Deliberately a SEPARATE NVS key
// rather than a bigger party blob -- growing that blob would change its stride
// and the length-based migration in begin() cannot tell a stride change from a
// slot-count change, so an existing party would be read back misaligned. A new
// key is purely additive and cannot corrupt anything.
#define BOX_SLOTS 18
#define MOVE_SLOTS 4    // the same four every trainer gets in the real games

// A retired pet. It does not keep ageing and cannot be trained any further,
// but it grows by FIGHTING while it is in the party (see Party::gainXp).
struct PartyMon {
  int16_t dex = 0;      // Pokedex number, 0 = empty slot
  uint16_t level = 1;   // its age when banked, then raised by experience
  uint16_t medals = 0;  // what it earned in life
  uint8_t ivAtk = 0, ivDef = 0, ivSpe = 0, ivHp = 0;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  uint8_t shiny = 0;
  char nick[12] = "";
  // Frozen with everything else: the moves you chose while it was alive are
  // what it fights with forever. 0 = empty slot (MOVE_TBL[0] is the "-" filler).
  // Appended at the END of the struct on purpose -- Party::begin() migrates
  // older, shorter blobs by length, and that only works if nothing moved.
  uint8_t moves[MOVE_SLOTS] = { 0, 0, 0, 0 };

  bool empty() const { return dex < 1; }
};

// Experience. A banked creature's level used to be frozen forever; now the
// party's members grow by fighting -- all six, like the Exp. Share: whoever
// fought takes the full share, the rest XP_BENCH_PCT of it. Never past the
// highest level the player has RAISED a creature to (Pet::xpCap), so raising
// stays the heart of the game. The box is storage and takes none.
#define XP_BENCH_PCT 50
// Progress toward the next level lives OUTSIDE PartyMon, under keys of its own
// ("pxp", "bxp"): PartyMon is stored raw, so a field inside it would shift
// every saved party -- CLAUDE.md trap 2. Every method below that moves a
// creature moves its experience with it.
struct XpResult { uint8_t levels = 0; int16_t evolvedFrom = 0; };

// What `dex` evolves into on reaching `level`, or 0: its species' evolution
// level, a target that can be drawn and whose pack is installed (evolution is
// one-way), and one of Eevee's branches for Eevee. Lives in pet.cpp beside the
// live creature's own evolution rules.
int16_t evolutionAt(int16_t dex, uint16_t level);

class Party {
public:
  PartyMon slots[PARTY_SLOTS];
  PartyMon box[BOX_SLOTS];
  uint16_t xp[PARTY_SLOTS] = { 0 };
  uint16_t boxXp[BOX_SLOTS] = { 0 };

  // the experience a level needs, and what a knocked-out (or caught) foe gives
  static uint16_t xpToNext(uint16_t level) { return 20 + level * 6; }
  static uint16_t xpForFoe(uint16_t foeLevel, bool trainer) {
    return (uint16_t)(foeLevel * (trainer ? 6 : 4));
  }
  // Gives `amount` to party slot i: levels it up while it can, never past
  // `cap`, and evolves it on reaching its species' evolution level.
  XpResult gainXp(uint8_t i, uint16_t amount, uint16_t cap);

  void begin();                 // load from NVS
  uint8_t count() const;
  bool isFull() const { return count() >= PARTY_SLOTS; }
  int firstFree() const;        // index of the first empty slot, -1 if full
  bool add(const PartyMon &m);  // into the first free slot; false if full
  void replaceAt(uint8_t i, const PartyMon &m);
  void releaseAt(uint8_t i);    // free a slot again
  void save();
  uint8_t boxCount() const;
  int boxFirstFree() const;
  bool boxAdd(const PartyMon &m);     // into the first free box slot
  void boxReleaseAt(uint8_t i);
  void boxSave();
  // Swaps a party slot with a box slot. Either may be empty, so this doubles as
  // deposit and withdraw rather than needing three separate operations.
  void swapPartyBox(uint8_t partyIdx, uint8_t boxIdx);

  // combat stats of a party member, same formula as the live pet's
  uint16_t atkOf(const PartyMon &m) const;
  uint16_t defOf(const PartyMon &m) const;
  uint16_t speOf(const PartyMon &m) const;
  uint16_t vitOf(const PartyMon &m) const;
  uint16_t spaOf(const PartyMon &m) const;
  uint16_t spdOf(const PartyMon &m) const;

private:
  Preferences prefs;
};

extern Party party;
