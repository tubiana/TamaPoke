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

// The synth against the CRIES. Everything above is the synth's balance with
// itself; this is the whole synth against a recorded cry, and it was wrong the
// other way: on the board the blips and the music were far louder than the
// cries at every volume, low included. A square wave is harsh and full of the
// harmonics the ear is most sensitive to, and a cry only touches its peak for
// an instant, so a cry with the higher peak still SOUNDED quieter. Every synth
// sample is scaled by SYNTH_LEVEL_PCT on its way to the codec, and a cry plays
// at CRY_GAIN per unit of volume (normalised to +-121 by pack_cries.py, so
// 121 * CRY_GAIN * 10 must stay under 32767 or volume 10 clips).
#define SYNTH_LEVEL_PCT 40
#define CRY_GAIN 20
#define CRY_PEAK 121
static_assert(CRY_PEAK * CRY_GAIN * 10 <= 32767, "a cry at volume 10 would clip");

static inline void synthLevel(int16_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) buf[i] = (int16_t)((int32_t)buf[i] * SYNTH_LEVEL_PCT / 100);
}

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
// A one-shot (the victory fanfare) plays its intro and stops. In the game the
// theme loops under the dialogue, and gen_music.py reads the whole channel
// text, so the data runs 90 s -- which played on long after a fight, on
// whatever screen came next. The intro is the first ~6.9 s.
#define FANFARE_MS 7000

// What the audio task does while no music is wanted: how many ms of synth to
// render (something is still ringing), or 0 when all is quiet and the task may
// sleep on its queue. `stopped` says music was playing until now.
//
// It must never say "nothing to do" while the synth is busy: the task then
// loops without rendering, the notes never run out, and it spins on core 0
// until the task watchdog reboots the board. That shipped in 3.21 -- music cut
// off mid-note (leaving exploring, the fanfare's 7 s cap) left its notes held.
static inline uint32_t quietStep(GbSynth &syn, bool stopped) {
  if (stopped) syn.allOff();   // cut mid-note: its notes would hold their full length
  return syn.busy() ? 10 : 0;
}

static inline uint32_t musicStep(GbSynth &syn, const MusicTrack &t, bool loop, MusicCursor &c) {
  if (!loop && c.clock >= FANFARE_MS) return 0;
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
