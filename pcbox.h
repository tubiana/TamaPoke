#pragma once
#include <Arduino.h>
#include "party.h"

// The boxes: every creature that is not in the team of six, in the order it
// arrived, like the PC in the real games. A catch, a trade and a gift land
// here; the team screen and the boxes screen move creatures between the two.
//
// NOT IN NVS. The whole NVS partition is 20 KB and a full NVS fails EVERY
// write, the live creature's included. This is one file on the board's own FAT
// partition (app3M_fat9M has 9 MB of it), so a missing or swapped microSD card
// cannot take anybody away. In the emulator it is a file beside the save.
//
// The list is held in RAM (PSRAM on the board, ~30 KB at PC_MAX) and the file
// is rewritten whole on every change: written beside the old one, then swapped
// in, so a power cut leaves the old list or the new one, never half of each.
// The header names the record size, so a future, longer PartyMon reads an old
// file the way Party::begin() reads an old party: the front of each record,
// the rest zeroed.
#define PC_MAX 960          // 60 pages of 16; in practice, never full

bool pcReady();             // the storage mounted (false: nothing can be kept here)
uint16_t pcCount();
bool pcGet(uint16_t i, PartyMon &out, uint16_t *xp = nullptr);
bool pcAdd(const PartyMon &m, uint16_t xp = 0);        // at the end; false if full or unwritable
bool pcSet(uint16_t i, const PartyMon &m, uint16_t xp);  // replace in place (a team swap)
bool pcTake(uint16_t i);    // remove; the ones after it move up
bool pcHas(int16_t dex);    // any creature of this species in the boxes
void pcWipe();              // WIPE: the factory reset takes this too
void pcReload();            // forget what is in RAM and read the file again (a reboot, for tests)

// Once, at boot: the Pokedex box file (3.19-3.34, one per species) and the old
// 18-slot box in NVS are emptied INTO the boxes. Each source is cleared only
// after the boxes were written, so a failure leaves a creature in two places,
// never in none. Returns how many moved.
uint16_t pcMigrate();
