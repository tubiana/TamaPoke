#pragma once
#include <Arduino.h>
#include "party.h"

// The Pokedex box: room for ONE creature of every species, kept in the
// Pokedex itself rather than in the party's 6 or the box's 18.
//
// It is the answer to "the box fills up": a wild catch lands here, a species
// caught again asks which of the two to keep, and the Pokedex page of a species
// moves its creature to the team and back. One per species means there is
// never a list to manage, and 809 slots never fill.
//
// NOT IN NVS. 809 records of a PartyMon are ~24 KB and the whole NVS partition
// is 20 KB -- and a full NVS fails EVERY write, the live creature's included.
// This is one file on the board's own FAT partition (app3M_fat9M has 9 MB of
// it, unused until now), so a missing or swapped microSD card cannot take
// anybody away. In the emulator it is a file beside the save.
//
// The file is the records in dex order behind a small header that names the
// record size, so a future, longer PartyMon reads an old file the way
// Party::begin() reads an old party: the front of each record, rest zeroed.
// A new species is simply a slot past the old end.

bool dexBoxHas(int16_t dex);
bool dexBoxGet(int16_t dex, PartyMon &out);
bool dexBoxPut(const PartyMon &m);   // into m.dex's slot, replacing whoever is there
void dexBoxTake(int16_t dex);        // empty that species' slot
uint16_t dexBoxCount();
void dexBoxWipe();                   // WIPE: the factory reset takes this too
bool dexBoxReady();                  // the storage mounted (false: catches cannot be kept)
