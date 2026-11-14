#pragma once
#include <stdint.h>

// Spoken UI: touching text on the screen reads it out, in the UI language,
// from pre-recorded clips on the SD card under /voice/<lang>/ (es en fr de it
// pt nl), made by tools/gen_voice.py and loaded from the web installer. Off
// unless the VOICE switch on the OPTIONS page is on (Pet::voiceOn).
//
// How it finds "what is under the finger" without touching the ~250 places
// that draw text: every glyph drawn goes through speechGlyph() (from the
// canvas' write()), rounded rectangles through speechBox(), and flush() calls
// speechFrame(). So after each frame we know which strings are on screen, where,
// and which button each one sits on.

void speechGlyph(int16_t x, int16_t y, uint8_t sx, uint8_t sy, char c);
void speechBox(int16_t x, int16_t y, int16_t w, int16_t h);
// A spoken label for something without text, e.g. an icon button.
void speechRegion(int16_t x, int16_t y, int16_t w, int16_t h, const char *text);
void speechFrame();

// Speak the text under (x, y). `fresh` = a new touch; otherwise (finger sliding)
// it only speaks when it lands on a different text. True if it spoke.
bool speechAt(int16_t x, int16_t y, bool fresh);
// Speak any text: phrases, Pokemon names and numbers are looked up in the index.
bool speechSay(const char *text);
// For the tests: the clips `text` would be said with (their lengths), not said.
int speechPlan(const char *text, uint32_t *lens, int max);
bool speechReady();        // switched on, and the UI language's pack is on the card
void speechSetEnabled(bool on);
bool speechHasPack();      // the UI language's pack is on the card (the switch aside)
const char *speechLangDir(uint8_t lang);   // "nl" -> /voice/nl/
void speechDump();         // serial: what is on screen right now (testing)

// Shared with tools/gen_voice.py: the two must tokenize identically.
// Lowercase letters and digits; a letter/digit change also splits; leading zeros
// dropped; "/" between numbers becomes the token "slash", "+" before a number
// "plus" -- language-neutral tokens, which each language's pack says its own way.
int speechTokens(const char *text, char *out, int outLen, int16_t *starts, int maxTok);
uint32_t speechHash(const char *s, int n);
