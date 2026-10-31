// The balance between a fight and everything else, MEASURED.
//
// The battle music is two voices plus percussion and never pauses, so at the
// track's own volumes a fight came out far louder than the rest of the device
// -- reported from the board. This renders the real music stepper and the real
// cue levels (audio_mix.h, exactly what the audio task calls) through the real
// synth and compares their energy, so the balance cannot drift back unnoticed.
//
// Whether it SOUNDS balanced is still for ears; what is measured here is that
// a fight is no louder than a UI cue and still clearly audible.
#include "audio_mix.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

#define MASTER 5
static std::vector<int16_t> pump(GbSynth &s, uint32_t ms) {
  std::vector<int16_t> out((size_t)ms * GB_RATE / 1000);
  s.render(out.data(), out.size(), MASTER);
  return out;
}
static double rms(const std::vector<int16_t> &v) {
  double a = 0; for (int16_t x : v) a += (double)x * x;
  return v.empty() ? 0 : sqrt(a / v.size());
}
static int peak(const std::vector<int16_t> &v) { int p = 0; for (int16_t x : v) p = std::max(p, abs((int)x)); return p; }

// one effect note the way the audio task plays it
static std::vector<int16_t> cue(uint8_t id, uint16_t hz, uint16_t ms) {
  GbSynth s; cueNote(s, id, 0, hzToGb(hz), ms); return pump(s, ms);
}

int main(){
  // a UI cue: a heart, as a caress plays it
  std::vector<int16_t> ui = cue(SFX_HEART, 880, 150);
  double uiRms = rms(ui); int uiPeak = peak(ui);

  // twenty seconds of the gym battle theme, through the audio task's own stepper
  GbSynth s; MusicCursor c;
  std::vector<int16_t> music;
  while (c.clock < 20000) {
    uint32_t st = musicStep(s, MUSIC_TBL[0], true, c);
    std::vector<int16_t> b = pump(s, st);
    music.insert(music.end(), b.begin(), b.end());
    c.clock += st;
  }
  double musRms = rms(music);

  // a battle cue landing on top of the music: voice 0 is taken by the cue,
  // voice 1 keeps playing the bass under it
  cueNote(s, SFX_BEAM, 0, hzToGb(1180), 150);
  std::vector<int16_t> over = pump(s, 150);
  // and the impact percussion on its own
  std::vector<int16_t> hit = cue(SFX_HIT, 180, 150);

  printf("    ui cue rms %.0f peak %d | music rms %.0f peak %d | cue over music peak %d | hit rms %.0f\n",
         uiRms, uiPeak, musRms, peak(music), peak(over), rms(hit));
  // A cue is a blip; the music never stops. So the music has to sit well under
  // a cue's energy and never peak past one -- at the track's own volumes it was
  // 0.68 of a cue's energy and peaked at 1.64x, which is what "a fight is too
  // loud" was.
  ck(peak(music) <= uiPeak, "the battle music never peaks past a UI cue");
  ck(musRms <= uiRms * 0.5, "...and carries at most half a cue's energy, all fight long");
  ck(musRms >= uiRms * 0.3, "...and is still clearly audible");
  // the battle cues land ON the music, so they are quieter than a cue in silence
  ck(peak(over) <= uiPeak * 0.8, "a move's cue on top of the music stays under a UI cue");
  ck(rms(hit) <= uiRms * 0.6, "the impact noise sits under a UI cue");
  ck(musicLevel(15) >= 1 && musicLevel(11) < 11, "the music is scaled, and a loud note never rounds to silence");

  printf("%s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
