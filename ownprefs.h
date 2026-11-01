#pragma once
#include <Preferences.h>

// A Preferences that a COPY does not share.
//
// Arduino's Preferences holds a raw NVS handle and its destructor calls
// end() -> nvs_close(handle). Copying an object that owns one therefore copies
// the handle, and when the copy dies it closes the ORIGINAL's handle: from then
// on every write the original makes fails silently, until a reboot reopens it.
//
// That shipped. The battle code built the player's side from a copy,
//
//     Pet tmp = pet;      // buildSquad(), every fight with the live pet in it
//
// so after the first fight NOTHING the live creature did was saved -- not
// catches in the Pokedex, not its stats, not its level -- and a reboot rolled
// the game back to before that fight. The Pokedex box, being a file, kept its
// catches, which is how it surfaced: a Pokeball on a species still in black.
//
// A copy here gets a fresh, unopened Preferences: it cannot write (an unopened
// Preferences refuses every put) and cannot close anything. Assigning one keeps
// the target's own handle. foesave_test pins both halves.
struct OwnedPrefs : Preferences {
  OwnedPrefs() {}
  OwnedPrefs(const OwnedPrefs &) : Preferences() {}
  OwnedPrefs &operator=(const OwnedPrefs &) { return *this; }
};

