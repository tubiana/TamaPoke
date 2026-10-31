#pragma once
#include <stdint.h>
#include "audio.h"     // Sfx
#include "gbsynth.h"
#include "music.h"

// How loud each kind of sound is against the others, and the music stepper,
// kept apart from audio.cpp's I2S so loud_test can render them exactly as the
// board does and MEASURE the balance instead of trusting a guess.
//
// The balance is the point. A UI cue is one pulse voice at CUE_LEVEL, but the
// battle music is TWO voices at the track's own 11-12 plus noise for every hit,
// and it never pauses -- a fight came out far louder than everything else on
// the device (its peaks were 1.6x a cue's, all fight long). The music is scaled
// so its two voices together peak no higher than one cue, and the battle cues
// are lower again because they land on top of the music rather than in silence.
// loud_test pins all of it.
#define CUE_LEVEL 14           // UI cues: eat, heart, medal... one voice
#define BATTLE_CUE_LEVEL 10    // a move landing, over the music
#define NOISE_LEVEL 9          // the impact / faint percussion
#define MUSIC_LEVEL_PCT 60     // the music voices, as a share of the track's own volume

// The SFX tables are written in Hz; the synth speaks the Game Boy's frequency
// register. Converting here means the cues did not have to be rewritten.
static inline uint16_t hzToGb(uint16_t hz) {
  if (!hz) return 0;
  int32_t f = 2048 - (int32_t)(131072u / hz);
  return (f < 0 || f > 2047) ? 0 : (uint16_t)f;
}

static inline uint8_t musicLevel(uint8_t v) { return (uint8_t)((v * MUSIC_LEVEL_PCT + 50) / 100); }

// Starts the effect's note `i` on the synth: noise for the impacts, a pulse for
// everything else. `gbFreq` 0 is a rest.
static inline void cueNote(GbSynth &syn, uint8_t id, uint8_t i, uint16_t gbFreq, uint16_t ms) {
  if (id == SFX_HIT || id == SFX_FAINT)
    syn.noise(NOISE_LEVEL, -1, 2, ms, (uint16_t)(4 + i * 4));
  else if (gbFreq)
    syn.note(0, gbFreq, 1, (id >= SFX_HIT && id <= SFX_FAINT) ? BATTLE_CUE_LEVEL : CUE_LEVEL, -1, 4, ms);
  else
    syn.silence(0);
}

// Where a track has got to. One per playing track; zeroed to restart it.
struct MusicCursor { uint16_t i1 = 0, i2 = 0; uint32_t at1 = 0, at2 = 0, clock = 0; };

// Starts whichever channel is due and returns how many ms to render before the
// next call (at most 60, to stay responsive to an effect), or 0 once a one-shot
// track has finished.
static inline uint32_t musicStep(GbSynth &syn, const MusicTrack &t, bool loop, MusicCursor &c) {
  if (c.clock >= c.at1) {
    if (c.i1 >= t.n1) {
      if (!loop) return 0;
      c.i1 = 0; c.at1 = c.clock;
    }
    const MusicNote &n = t.ch1[c.i1++];
    if (n.freq) syn.note(0, n.freq, n.duty, musicLevel(n.vol), n.envDir, n.envPeriod, n.ms);
    else syn.silence(0);
    c.at1 = c.clock + n.ms;
  }
  if (c.clock >= c.at2) {
    if (c.i2 >= t.n2) { c.i2 = 0; c.at2 = c.clock; }
    const MusicNote &n = t.ch2[c.i2++];
    if (n.freq) syn.note(1, n.freq, n.duty, musicLevel(n.vol), n.envDir, n.envPeriod, n.ms);
    else syn.silence(1);
    c.at2 = c.clock + n.ms;
  }
  uint32_t next = (c.at1 < c.at2) ? c.at1 : c.at2;
  if (next <= c.clock) next = c.clock + 5;
  uint32_t step = next - c.clock;
  return step > 60 ? 60 : step;
}
