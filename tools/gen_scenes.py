#!/usr/bin/env python3
"""Original scenes, drawn by code: tools/backs/<NAME>.png

    python3 tools/gen_scenes.py                 # writes only the PNGs that are missing
    python3 tools/gen_scenes.py --force         # redraws all of them
    python3 tools/gen_scenes.py --out DIR NAME  # preview one scene elsewhere

Every PNG this writes is an ordinary 240x112 image in tools/backs/, the same
folder and format as the supplied battle backgrounds. That is the whole
replacement story: drop any other picture under the same name (hand-drawn, or
from an image generator) and gen_backs.py packs it instead. This script never
overwrites a file that exists unless asked to, so a replaced picture survives a
re-run.

The art is ours, which matters: CREDITS.md cannot establish where the original
twelve backgrounds came from, and these carry no such question.

How it is drawn, so a scene can be changed without reverse-engineering it:
- colours come from RAMPS, a few shades of one material, hue-shifted (shadows
  lean blue, highlights lean yellow) the way pixel artists shade by hand;
  continuous values are mapped onto a ramp through a 4x4 Bayer dither;
- floors are a real perspective projection: each pixel below the horizon is
  turned into a world (X, Z) and the floor is a texture function of those, so
  arena lines, tiles and the centre circle all foreshorten correctly;
- night is DERIVED from day (a moonlit colour shift), except for pixels a scene
  marks as emissive -- lava, torches, bulbs -- which keep glowing.
"""
import argparse
import colorsys
import math
import os
import random

from PIL import Image

W, H = 240, 112
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'backs')

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


# ---------- colour ----------

def ramp(hue, sat, n=5, lo=0.14, hi=0.82, shift=1.0):
    """n shades dark -> light of one material, hue-shifted like hand shading."""
    out = []
    for i in range(n):
        t = i / (n - 1)
        h = hue + shift * (-(1 - t) * 22 + t * 14) * (1 if hue < 200 else -1)
        if hue >= 200:  # blues and purples: shadows go deeper blue, lights go cyan
            h = hue + shift * ((1 - t) * 10 - t * 18)
        s = sat * (0.75 + 0.45 * math.sin(math.pi * t))
        l = lo + (hi - lo) * t
        r, g, b = colorsys.hls_to_rgb((h % 360) / 360.0, l, min(1.0, s))
        out.append((int(r * 255), int(g * 255), int(b * 255)))
    return out


def pick(rmp, t, x, y):
    """A continuous 0..1 value onto a ramp, dithered."""
    t = 0.0 if t < 0 else (1.0 if t > 1 else t)
    f = t * (len(rmp) - 1)
    i = int(f)
    if i < len(rmp) - 1 and (f - i) * 16 > BAYER[y & 3][x & 3] + 0.5:
        i += 1
    return rmp[i]


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


# ---------- noise ----------

def _h(ix, iy, seed):
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, y, seed=0):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = _h(ix, iy, seed), _h(ix + 1, iy, seed)
    c, d = _h(ix, iy + 1, seed), _h(ix + 1, iy + 1, seed)
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy


def fbm(x, y, seed=0, oct=4):
    s, amp, tot = 0.0, 1.0, 0.0
    for o in range(oct):
        s += vnoise(x, y, seed + o * 17) * amp
        tot += amp; x *= 2.03; y *= 2.03; amp *= 0.5
    return s / tot


def voronoi(x, y, seed=0):
    """(distance to nearest point, gap to the second nearest, cell id)."""
    ix, iy = math.floor(x), math.floor(y)
    best, second, cid = 9.0, 9.0, 0
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cx, cy = ix + dx, iy + dy
            px = cx + _h(cx, cy, seed)
            py = cy + _h(cx, cy, seed + 7)
            d = math.hypot(px - x, py - y)
            if d < best:
                second, best, cid = best, d, (cx * 73856093) ^ (cy * 19349663)
            elif d < second:
                second = d
    return best, second - best, cid


# ---------- canvas ----------

class Scene:
    def __init__(self):
        self.px = [[(0, 0, 0)] * W for _ in range(H)]
        self.emit = [[False] * W for _ in range(H)]   # keeps its colour at night
        self.sky = [[False] * W for _ in range(H)]    # where stars may go

    def put(self, x, y, c, emit=False):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = c
            self.emit[y][x] = emit

    def get(self, x, y):
        return self.px[max(0, min(H - 1, y))][max(0, min(W - 1, x))]

    def blend(self, x, y, c, a, emit=False):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = mix(self.px[y][x], c, a)
            if emit:
                self.emit[y][x] = True

    def image(self):
        im = Image.new('RGB', (W, H))
        im.putdata([c for row in self.px for c in row])
        return im


# ---------- shared pieces ----------

def sky(s, y0, y1, top, bottom, mark=True):
    rmp = [mix(top, bottom, i / 5) for i in range(6)]
    for y in range(y0, y1):
        for x in range(W):
            s.put(x, y, pick(rmp, (y - y0) / max(1, y1 - y0 - 1), x, y))
            if mark:
                s.sky[y][x] = True


def cloud(s, cx, cy, size, seed, tones):
    """Cumulus: a union of puffs, lit from above. tones = (shadow, mid, light)."""
    rnd = random.Random(seed)
    puffs = [(cx, cy, size)]
    for _ in range(6):
        a = rnd.uniform(-math.pi, 0)
        r = size * rnd.uniform(0.5, 0.9)
        puffs.append((cx + math.cos(a) * size * 1.3 * rnd.uniform(0.6, 1.4),
                      cy + math.sin(a) * size * 0.5, r))
    x0 = int(min(p[0] - p[2] for p in puffs)) - 1
    x1 = int(max(p[0] + p[2] for p in puffs)) + 1
    y0 = int(min(p[1] - p[2] for p in puffs)) - 1
    base = cy + size * 0.55
    for y in range(y0, int(base) + 1):
        for x in range(x0, x1 + 1):
            best = -1.0
            for px, py, r in puffs:
                d = ((x - px) ** 2 + ((y - py) * 1.15) ** 2) / (r * r)
                if d < 1:
                    best = max(best, 1 - d + (py - y) / (r * 3))
            if best < 0:
                continue
            light = 0.55 + 0.5 * best - 0.65 * max(0, (y - (base - size * 0.6)) / (size * 0.6))
            s.put(x, y, pick(list(tones), light, x, y))


def rock(s, cx, cy, rx, ry, seed, rmp, outline=None, light=(-0.6, -0.8)):
    """A shaded boulder: noisy outline, lit from the upper left, dark rim."""
    rnd = random.Random(seed)
    k = [rnd.uniform(0.8, 1.15) for _ in range(9)]
    outline = outline or tuple(max(0, c - 30) for c in rmp[0])

    def radius(a):
        f = (a / (2 * math.pi)) * 9
        i = int(f) % 9
        t = f - int(f)
        return k[i] + (k[(i + 1) % 9] - k[i]) * t

    def inside(x, y):
        dx, dy = (x - cx) / rx, (y - cy) / ry
        if dy > 0.55:   # flat bottom, sits on the ground
            return None
        d = math.hypot(dx, dy) / radius(math.atan2(dy, dx) % (2 * math.pi))
        return (dx, dy, d) if d <= 1 else None

    for y in range(int(cy - ry) - 2, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 2, int(cx + rx) + 2):
            r = inside(x, y)
            if r is None:
                continue
            dx, dy, d = r
            if not all(inside(x + ox, y + oy) for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                s.put(x, y, outline)
                continue
            # facets: each chip of the stone takes one tilt, so it reads as
            # carved planes rather than noise
            _, gap, cid = voronoi(x / (rx * 0.45 + 2), y / (ry * 0.45 + 2), seed)
            tilt = (_h(cid & 0xFFFF, 5, seed) - 0.5) * 0.35
            nz = math.sqrt(max(0, 1 - d * d))
            lit = (-dx * light[0] - dy * light[1]) * 0.45 + nz * 0.5 + tilt
            if gap < 0.05:
                lit -= 0.25
            s.put(x, y, pick(rmp, 0.1 + lit * 0.85, x, y))


def ground_shadow(s, cx, cy, rx, ry, a=0.35):
    for y in range(int(cy - ry), int(cy + ry) + 1):
        for x in range(int(cx - rx), int(cx + rx) + 1):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1:
                s.blend(x, y, (20, 20, 40), a)


def stars(s, seed, count, moon=None):
    rnd = random.Random(seed)
    for _ in range(count):
        x, y = rnd.randrange(W), rnd.randrange(H)
        if s.sky[y][x]:
            c = rnd.choice([(255, 255, 255), (220, 230, 255), (255, 240, 200)])
            s.put(x, y, c)
            if rnd.random() < 0.15:   # a few twinkle crosses
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if s.sky[min(H - 1, max(0, y + dy))][min(W - 1, max(0, x + dx))]:
                        s.blend(x + dx, y + dy, c, 0.5)


def orb(s, cx, cy, r, body, halo, emit=True):
    """Sun or moon: a disc with a soft banded halo."""
    for y in range(int(cy - r * 3), int(cy + r * 3) + 1):
        for x in range(int(cx - r * 3), int(cx + r * 3) + 1):
            d = math.hypot(x - cx, y - cy)
            if d <= r:
                s.put(x, y, body, emit)
            elif d <= r * 3 and 0 <= y < H and 0 <= x < W and s.sky[y][x]:
                band = 0.45 if d < r * 1.6 else (0.25 if d < r * 2.3 else 0.1)
                s.blend(x, y, halo, band)


def to_night(s, tint=(0.42, 0.52, 0.86), lift=(8, 12, 40)):
    out = Scene()
    for y in range(H):
        for x in range(W):
            out.sky[y][x] = s.sky[y][x]
            out.emit[y][x] = s.emit[y][x]
            r, g, b = s.px[y][x]
            if s.emit[y][x]:
                out.px[y][x] = (r, g, b)
                continue
            l = (r * 0.3 + g * 0.59 + b * 0.11) / 255.0
            k = 0.75 if s.sky[y][x] else 1.0
            out.px[y][x] = (int(min(255, (r * tint[0] * 0.5 + l * 34 + lift[0]) * k)),
                            int(min(255, (g * tint[1] * 0.55 + l * 48 + lift[1]) * k)),
                            int(min(255, (b * tint[2] * 0.62 + l * 70 + lift[2]) * k)))
    return out


def glow(s, cx, cy, r, c, a=0.5):
    """Light falling on its surroundings from an emissive source."""
    for y in range(int(cy - r), int(cy + r) + 1):
        for x in range(int(cx - r), int(cx + r) + 1):
            d = math.hypot(x - cx, (y - cy) * 1.3) / r
            if d < 1 and 0 <= x < W and 0 <= y < H and not s.emit[y][x]:
                # banded, not smooth: three rings read as pixel art
                band = 1.0 if d < 0.4 else (0.6 if d < 0.7 else 0.3)
                s.blend(x, y, c, a * band)


# ---------- perspective ----------

HORIZON = 18     # the true vanishing line, above the wall's foot
FOOT = 48        # where the back wall meets the floor


def floor_xz(x, y):
    """Screen pixel -> world (X across, Z depth). Z is 1.0 at the bottom edge."""
    k = y - HORIZON + 0.5
    z = (H - HORIZON) / k
    return (x - W / 2) / k * 1.6, z


def floor(s, y0, tex):
    for y in range(y0, H):
        for x in range(W):
            X, Z = floor_xz(x, y)
            r = tex(X, Z, x, y)
            if r is not None:
                s.put(x, y, *r) if isinstance(r[0], tuple) else s.put(x, y, r)


def field_lines(X, Z, w=0.035):
    """The battle field painted on an arena floor: a rectangle, a halfway line
    and a centre ring with a dot, in world units so they foreshorten."""
    hx, z0, z1, zc = 1.55, 1.12, 2.55, 1.8
    on = False
    if z0 - w < Z < z1 + w and abs(abs(X) - hx) < w:
        on = True
    if -hx < X < hx and (abs(Z - z0) < w or abs(Z - z1) < w or abs(Z - zc) < w * 0.8):
        on = True
    d = math.hypot(X, (Z - zc) * 1.0)
    if abs(d - 0.38) < w or d < 0.09:
        on = True
    return on


def spot(x, y, sources, base=0.0):
    """Light from overhead spotlights, as an extra 0..1 brightness."""
    v = base
    for sx, sy, r, a in sources:
        d = math.hypot((x - sx) / r, (y - sy) / (r * 0.55))
        if d < 1:
            v += a * (1 - d)
    return v


# =====================================================================
# biomes
# =====================================================================

def ocean():
    s = Scene()
    sky(s, 0, 46, (86, 160, 236), (198, 232, 252))
    orb(s, 186, 14, 6, (255, 252, 228), (255, 250, 210))
    cloud(s, 34, 16, 9, 1, ((178, 206, 236), (222, 238, 252), (255, 255, 255)))
    cloud(s, 112, 9, 6, 2, ((178, 206, 236), (222, 238, 252), (255, 255, 255)))
    cloud(s, 222, 30, 5, 3, ((178, 206, 236), (222, 238, 252), (255, 255, 255)))
    # far islands: a ridge of noise, lit from the left
    green = ramp(135, 0.45, 5, 0.16, 0.6)
    haze = (150, 200, 230)
    for lo, hi, peak, sd, far in ((90, 140, 6, 6, 0.55), (-14, 70, 16, 4, 0.0), (128, 206, 11, 5, 0.0)):
        def hgt(x):
            t = (x - lo) / (hi - lo)
            if not 0 <= t <= 1:
                return 0.0
            return peak * math.sin(math.pi * t) ** 0.7 * (0.55 + 0.75 * fbm(x * 0.07, 0, sd, 3))
        for x in range(max(0, lo), min(W, hi + 1)):
            h = hgt(x)
            top = int(45 - h)
            slope = hgt(x - 1) - hgt(x + 1)       # rising to the right = facing the light
            for y in range(top, 45):
                lit = 0.5 + max(-0.35, min(0.35, slope * 0.6)) - (y - top) * 0.03
                lit += (fbm(x * 0.4, y * 0.6, sd + 9) - 0.5) * 0.35   # tree canopy
                s.put(x, y, mix(pick(green, lit, x, y), haze, far))
                s.sky[y][x] = False
            if top < 45:
                s.put(x, top, mix(green[-1] if slope > 0 else green[2], haze, far))
            if far == 0 and h > 0.5:
                s.put(x, 44, (236, 222, 172))     # a sliver of beach
    # sea: depth gradient, then streaks that lengthen toward the viewer
    sea = [(18, 60, 140), (26, 84, 170), (36, 112, 196), (52, 142, 214), (86, 176, 228), (140, 210, 240)]
    rnd = random.Random(11)
    for y in range(46, H):
        t = (y - 46) / (H - 46)
        for x in range(W):
            n = fbm(x * 0.05, y * 0.3, 21) - 0.5
            s.put(x, y, pick(sea, 0.92 - t * 0.85 + n * 0.25, x, y))
    s.put(0, 46, sea[-1])
    for x in range(W):
        s.put(x, 46, (190, 232, 250))
    for y in range(48, H, 3):
        t = (y - 46) / (H - 46)
        for _ in range(int(14 - 6 * t)):
            x = rnd.randrange(W)
            L = int(2 + 12 * t * rnd.uniform(0.5, 1.2))
            for i in range(L):
                s.put(x + i, y, (200, 238, 252) if i in (0, L - 1) or t < 0.3 else (150, 214, 244))
                s.put(x + i + 1, y + 1, sea[1])
    # glitter under the sun
    for y in range(47, 80):
        for x in range(176, 197):
            if rnd.random() < 0.18 * (1 - (y - 47) / 33):
                s.put(x, y, (255, 252, 230))
    # framing rocks with foam, so the open water reads as "in front of us"
    stone = ramp(215, 0.18, 5, 0.16, 0.66)
    for cx, cy, rx, ry, sd in ((8, 98, 22, 16, 31), (34, 106, 14, 9, 32), (232, 92, 16, 20, 33), (212, 108, 10, 6, 34)):
        for a in range(0, 360, 3):
            fx = int(cx + math.cos(math.radians(a)) * (rx + 3))
            fy = int(cy + ry * 0.5 + math.sin(math.radians(a)) * 3)
            s.put(fx, fy, (236, 248, 255))
        rock(s, cx, cy, rx, ry, sd, stone)
    return s, None


def ocean_night():
    s, _ = ocean()
    n = to_night(s)
    for y in range(H):        # the sun has to go before the moon comes up
        for x in range(160, 212):
            if n.sky[y][x] and math.hypot(x - 186, y - 14) < 20:
                n.px[y][x] = n.px[min(H - 1, y)][150]
    stars(n, 5, 90)
    orb(n, 186, 14, 5, (240, 244, 255), (170, 190, 240))
    rnd = random.Random(12)
    for y in range(48, 84):
        for x in range(178, 195):
            if rnd.random() < 0.14 * (1 - (y - 48) / 36):
                n.put(x, y, (220, 232, 255))
    return n


def volcano():
    s = Scene()
    sky(s, 0, 50, (46, 22, 44), (214, 98, 58))
    # ash plume from the crater, drifting right
    ash = [(58, 40, 56), (84, 60, 74), (112, 86, 96)]
    for i in range(14):
        cx, cy, r = 74 + i * 6, 8 - i * 0.2 + math.sin(i) * 2, 5 + i * 0.7
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r * 1.3), int(cx + r * 1.3) + 1):
                d = math.hypot((x - cx) / 1.3, y - cy) / r
                if d < 1 and 0 <= y < H and 0 <= x < W:
                    s.put(x, y, pick(ash, 1 - d * 0.6 - (y - cy + r) / (4 * r) + fbm(x * .2, y * .2, i) * .3, x, y))
    # the cone: lit on the left, a crater rim, lava runs that glow
    rockr = ramp(10, 0.28, 5, 0.10, 0.42)
    peak, base, cx = 10, 50, 70
    for y in range(peak, base):
        k = (y - peak) / (base - peak)
        half = 8 + 70 * k ** 1.6
        for x in range(int(cx - half), int(cx + half) + 1):
            t = (x - cx) / half
            gully = math.sin(t * 9 + fbm(x * 0.1, y * 0.1, 42) * 4)
            n = fbm(x * 0.15, y * 0.15, 41)
            s.put(x, y, pick(rockr, 0.5 - t * 0.35 + gully * 0.15 + (n - 0.5) * 0.3 - k * 0.15, x, y))
            s.sky[y][x] = False
    for x in range(cx - 8, cx + 9):
        s.put(x, peak, (255, 170, 60), True)
        s.put(x, peak + 1, (220, 90, 40), True)
    lava = [(150, 30, 20), (220, 80, 30), (255, 150, 40), (255, 220, 120)]
    for k, (dx0, wob) in enumerate(((-3, 1.2), (2, -0.9), (5, 1.6))):
        x = cx + dx0
        for y in range(peak + 1, base):
            x += math.sin(y * 0.35 + k) * 0.5 + wob * 0.15
            for w in (-1, 0, 1):
                s.put(int(x) + w, y, pick(lava, 0.9 - abs(w) * 0.4 - (y - peak) / 80, int(x) + w, y), True)
    # a jagged dark ridge closer in
    far = ramp(350, 0.2, 4, 0.08, 0.26)
    for x in range(W):
        top = int(40 + 6 * fbm(x * 0.06, 3, 43) + 3 * math.sin(x * 0.11))
        for y in range(top, 52):
            s.put(x, y, pick(far, 0.8 - (y - top) * 0.06, x, y))
            s.sky[y][x] = False
        s.put(x, top, (150, 70, 60))
    # basalt floor in perspective with glowing cracks
    basalt = ramp(15, 0.14, 5, 0.12, 0.44)
    def tex(X, Z, x, y):
        n = fbm(X * 1.4, Z * 1.4, 51)
        d, gap, cid = voronoi(X * 0.8 + n * 0.6, Z * 1.3 + n * 0.4, 52)
        if gap < 0.035 + 0.025 * n:
            if _h(cid & 0xFFFF, 1, 54) < 0.45:      # most seams are cold, a few glow
                return basalt[0]
            return (pick(lava, 0.95 - gap * 12, x, y), True)
        lit = 0.25 + (1 - Z / 4) * 0.45 + (n - 0.5) * 0.6 + (_h(cid, 0, 53) - 0.5) * 0.25
        return pick(basalt, lit, x, y)
    floor(s, 52, tex)
    # lava pools at the sides, crusted at the rim
    for pcx, pcy, rx, ry in ((22, 92, 30, 9), (214, 78, 26, 6)):
        for y in range(pcy - ry - 1, pcy + ry + 2):
            for x in range(pcx - rx - 2, pcx + rx + 3):
                d = math.hypot((x - pcx) / rx, (y - pcy) / ry)
                if d < 1:
                    n = fbm(x * 0.3, y * 0.6, 61)
                    s.put(x, y, pick(lava, 0.45 + n * 0.7 - d * 0.25, x, y), True)
                elif d < 1.18:
                    s.put(x, y, (40, 22, 22))
        glow(s, pcx, pcy - 2, rx * 1.5, (255, 120, 40), 0.35)
    for _, gx in enumerate((cx,)):
        glow(s, gx, 46, 40, (255, 110, 40), 0.18)
    # embers
    rnd = random.Random(71)
    for _ in range(40):
        x, y = rnd.randrange(W), rnd.randrange(4, 70)
        s.put(x, y, rnd.choice([(255, 200, 80), (255, 140, 50)]), True)
    return s, None


def volcano_night():
    s, _ = volcano()
    n = to_night(s, tint=(0.55, 0.45, 0.8), lift=(14, 6, 30))
    stars(n, 9, 50)
    return n


# =====================================================================
# arenas
# =====================================================================

def arena_floor(s, base_tex, line_col, lights, line_w=0.035):
    def tex(X, Z, x, y):
        r = base_tex(X, Z, x, y)
        c, e = (r if isinstance(r[0], tuple) else (r, False))
        if field_lines(X, Z, line_w):
            c = line_col
        lv = spot(x, y, lights)
        if lv > 0:
            c = mix(c, (255, 250, 220), min(0.35, lv * 0.35) if (x + y) & 1 or lv > 0.5 else min(0.2, lv * 0.2))
        return (c, e)
    floor(s, FOOT, tex)


def wall_trim(s, y, dark, light):
    for x in range(W):
        s.put(x, y, light)
        s.put(x, y + 1, dark)


def arena_rock():
    s = Scene()
    # back wall: piled boulders, each a voronoi cell shaded as a rounded stone
    stone = ramp(30, 0.22, 5, 0.18, 0.62)
    for y in range(FOOT):
        for x in range(W):
            d, gap, cid = voronoi(x / 13.0, y / 10.0, 81)
            if gap < 0.08:
                s.put(x, y, (52, 40, 34))
                continue
            lit = 0.75 - d * 0.7 + (_h(cid, 1, 82) - 0.5) * 0.35 + (fbm(x * .2, y * .2, 83) - .5) * .3
            s.put(x, y, pick(stone, lit - y * 0.004, x, y))
    wall_trim(s, FOOT - 2, (60, 44, 34), (150, 120, 90))
    sand = ramp(38, 0.38, 5, 0.32, 0.78)
    def tex(X, Z, x, y):
        n = fbm(X * 3, Z * 3, 84)
        peb = _h(int(X * 14), int(Z * 14), 85) > 0.965
        return pick(sand, (0.62 + (n - 0.5) * 0.6 - (0.12 if peb else 0) - (Z - 1) * 0.08), x, y)
    arena_floor(s, tex, (250, 244, 226), [(60, 60, 60, 0.6), (180, 60, 60, 0.6)])
    for cx, cy, rx, ry, sd in ((10, 100, 20, 16, 86), (230, 96, 18, 18, 87), (34, 108, 12, 7, 88)):
        ground_shadow(s, cx + 4, cy + ry * 0.5, rx * 1.1, 3)
        rock(s, cx, cy, rx, ry, sd, stone)
    return s


def arena_water():
    s = Scene()
    tile = [(150, 196, 226), (182, 220, 240), (214, 238, 250), (236, 248, 255)]
    for y in range(FOOT):
        for x in range(W):
            g = (x % 12 == 0) or (y % 12 == 0)
            c = (120, 168, 206) if g else pick(tile, 0.6 + (fbm(x * .1, y * .1, 91) - .5) * .5 - y * 0.006, x, y)
            s.put(x, y, c)
    # a wave mural across the wall
    for x in range(W):
        yy = 26 + int(4 * math.sin(x * 0.16)) + int(2 * math.sin(x * 0.05))
        for y in range(yy, 40):
            s.put(x, y, (40, 120, 200) if y > yy + 1 else (230, 248, 255))
        if x % 24 < 3:
            s.put(x, yy - 2, (230, 248, 255))
    wall_trim(s, FOOT - 2, (40, 90, 140), (240, 250, 255))
    water = [(20, 90, 170), (30, 120, 200), (60, 160, 224), (120, 200, 240), (210, 244, 255)]
    deck = [(170, 180, 190), (205, 214, 222), (232, 238, 242)]
    def tex(X, Z, x, y):
        in_pool = abs(X) < 1.45 and 1.18 < Z < 2.5
        if not in_pool:
            g = (abs(X * 4 - round(X * 4)) < 0.04) or (abs(Z * 4 - round(Z * 4)) < 0.04)
            return deck[0] if g else pick(deck, 0.75 - (Z - 1) * 0.2 + (fbm(X * 3, Z * 3, 94) - .5) * .4, x, y)
        if abs(abs(X) - 1.45) < 0.05 or abs(Z - 1.18) < 0.04 or abs(Z - 2.5) < 0.05:
            return (60, 90, 120)
        # caustics: the bright net that light makes through moving water
        _, gap, _ = voronoi(X * 2.6 + fbm(X, Z, 93), Z * 4.0, 92)
        c = 0.3 + (2.5 - Z) * 0.18 + (0.45 if gap < 0.035 else (0.2 if gap < 0.07 else 0))
        if abs(Z - 1.82) < 0.025:               # a floating lane rope, red and white
            return (230, 60, 60) if int(X * 10) % 2 else (250, 250, 250)
        return pick(water, c, x, y)
    floor(s, FOOT, tex)
    # life rings on the wall, ladders near the viewer
    for rx_ in (24, 216):
        for y in range(4, 23):
            for x in range(rx_ - 10, rx_ + 11):
                d = math.hypot(x - rx_, y - 13)
                if 4.5 < d < 8.5:
                    a = math.degrees(math.atan2(y - 13, x - rx_)) % 90
                    s.put(x, y, (230, 60, 50) if a < 45 else (250, 250, 250))
                elif 8.5 <= d < 9.5 or 3.5 < d <= 4.5:
                    s.put(x, y, (110, 60, 60))
    for lx in (30, 206):
        for y in range(70, 104):
            s.put(lx, y, (210, 220, 230)); s.put(lx + 8, y, (210, 220, 230))
            if y % 7 == 0:
                for x in range(lx, lx + 9):
                    s.put(x, y, (230, 236, 240))
    return s


def arena_electric():
    s = Scene()
    steel = ramp(220, 0.12, 5, 0.12, 0.5)
    for y in range(FOOT):
        for x in range(W):
            px, py = x % 30, y % 24
            if px == 0 or py == 0:
                c = steel[0]
            elif px == 1 or py == 1:
                c = steel[3]
            elif (px in (4, 26)) and (py in (4, 20)):
                c = steel[4]                     # rivets
            else:
                c = pick(steel, 0.45 - py * 0.01 + (fbm(x * .3, y * .05, 101) - .5) * .25, x, y)
            s.put(x, y, c)
    for y in range(FOOT - 8, FOOT - 2):          # hazard stripe
        for x in range(W):
            s.put(x, y, (250, 210, 40) if ((x + y) // 5) % 2 else (40, 36, 30))
    # a row of bulbs that light the wall around them
    for bx in range(15, W, 30):
        glow(s, bx, 10, 14, (255, 236, 120), 0.4)
        for y in range(7, 14):
            for x in range(bx - 3, bx + 4):
                if math.hypot(x - bx, y - 10) < 3.6:
                    s.put(x, y, (255, 250, 200) if y < 10 else (255, 220, 80), True)
        s.put(bx, 6, (90, 90, 100)); s.put(bx, 5, (90, 90, 100))
    grate = ramp(225, 0.14, 5, 0.18, 0.62)
    def tex(X, Z, x, y):
        u, v = X * 2.2, Z * 3.0
        fu, fv = u - math.floor(u), v - math.floor(v)
        if fu < 0.04 or fv < 0.05:
            return grate[0]
        if fu > 0.95 or fv > 0.94:
            return grate[3]
        if (abs(fu - 0.1) < 0.03 or abs(fu - 0.88) < 0.03) and (abs(fv - 0.14) < 0.04 or abs(fv - 0.84) < 0.04):
            return grate[4]
        return pick(grate, 0.5 + (fbm(X * 2, Z * 2, 102) - 0.5) * 0.4, x, y)
    arena_floor(s, tex, (250, 220, 60), [(120, 70, 90, 0.7)])
    # the bolt in the centre ring, upright: a symbol lying flat stops reading
    bolt = [(1, -10), (8, -10), (2, -2), (7, -2), (-5, 10), (-1, 1), (-6, 1)]
    cy = HORIZON - 0.5 + (H - HORIZON) / 1.8          # screen row of the ring's centre
    for y in range(FOOT, H):
        for x in range(W):
            px, pz = x - W / 2 + 0.5, y - cy
            inside = False
            j = len(bolt) - 1
            for i in range(len(bolt)):
                xi, zi = bolt[i]; xj, zj = bolt[j]
                if (zi > pz) != (zj > pz) and px < (xj - xi) * (pz - zi) / (zj - zi) + xi:
                    inside = not inside
                j = i
            if inside:
                s.put(x, y, (255, 226, 60) if (x + y) % 5 else (255, 250, 180), True)
    return s


def arena_grass():
    s = Scene()
    # greenhouse glass: panes of sky-tinted green, white frames, plants behind
    for y in range(FOOT):
        for x in range(W):
            fx, fy = x % 26, y % 22
            if fx < 2 or fy < 2:
                s.put(x, y, (236, 244, 236) if fx == 0 or fy == 0 else (170, 190, 176))
                continue
            leaf = fbm(x * 0.09, y * 0.12, 111)
            if leaf > 0.55 - y * 0.004:
                s.put(x, y, pick(ramp(115, 0.4, 4, 0.22, 0.52), leaf, x, y))
            else:
                s.put(x, y, pick([(170, 220, 210), (196, 236, 226), (220, 246, 238)], 0.5 + (fx - fy) * 0.02, x, y))
            if (fx + fy) % 23 == 0:
                s.put(x, y, (250, 255, 250))       # a glint on the glass
    # vines hanging from the roof
    rnd = random.Random(113)
    leafr = ramp(120, 0.5, 4, 0.2, 0.55)
    for _ in range(14):
        vx = rnd.randrange(W); ln = rnd.randrange(6, 20)
        for y in range(ln):
            x = vx + int(math.sin(y * 0.5 + vx) * 1.5)
            s.put(x, y, leafr[1])
            if y % 3 == 0:
                s.put(x - 1, y, leafr[3]); s.put(x + 1, y + 1, leafr[2])
    wall_trim(s, FOOT - 2, (70, 90, 60), (200, 220, 190))
    lawn = ramp(105, 0.48, 5, 0.22, 0.62)
    def tex(X, Z, x, y):
        stripe = 0.12 if int(Z * 5) % 2 else -0.05       # mowing stripes
        n = fbm(X * 6, Z * 6, 114)
        if _h(int(X * 18), int(Z * 18), 115) > 0.985:
            return rnd_flower(int(X * 18), int(Z * 18))
        return pick(lawn, 0.5 + stripe + (n - 0.5) * 0.5, x, y)
    arena_floor(s, tex, (245, 250, 235), [(120, 50, 110, 0.5)])
    # potted bushes in the near corners
    for cx, cy in ((18, 92), (222, 92)):
        for y in range(cy, cy + 16):
            for x in range(cx - 9 + (y - cy) // 3, cx + 10 - (y - cy) // 3):
                s.put(x, y, pick([(130, 60, 40), (170, 90, 60), (200, 120, 80)], 0.8 - (x - cx + 9) / 22, x, y))
        for k in range(26):
            a = k * 2.4
            r = 4 + (k % 4) * 2.2
            rock(s, cx + math.cos(a) * r, cy - 6 + math.sin(a) * r * 0.7, 5, 5, 120 + k, lawn, (30, 60, 30))
    return s


def rnd_flower(i, j):
    return [(250, 240, 120), (250, 250, 250), (250, 150, 180)][(i * 7 + j * 3) % 3]


def arena_fire():
    s = Scene()
    brick = ramp(8, 0.45, 5, 0.14, 0.5)
    for y in range(FOOT):
        for x in range(W):
            row = y // 7
            off = 8 if row % 2 else 0
            bx, by = (x + off) % 16, y % 7
            if bx == 0 or by == 0:
                c = (40, 18, 18)
            else:
                cid = (x + off) // 16 * 31 + row * 17
                c = pick(brick, 0.4 + (_h(cid, 2, 121) - 0.5) * 0.4 - by * 0.03 + (0.12 if by == 1 else 0), x, y)
            s.put(x, y, c)
    wall_trim(s, FOOT - 2, (40, 18, 18), (200, 120, 60))
    # torches: bracket, flame, and the warm pool of light they throw
    flame = [(180, 40, 20), (240, 100, 30), (255, 180, 60), (255, 240, 170)]
    for tx in (40, 120, 200):
        glow(s, tx, 18, 26, (255, 150, 60), 0.35)
        for y in range(24, 34):
            s.put(tx, y, (60, 40, 30)); s.put(tx + 1, y, (90, 60, 40))
        for y in range(10, 24):
            for x in range(tx - 5, tx + 7):
                d = math.hypot((x - tx - 0.5) / (4.5 - (24 - y) * 0.22 + 0.01), (y - 20) / 7)
                if d < 1 and (24 - y) * 0.22 < 4.4:
                    s.put(x, y, pick(flame, 1.0 - d + (fbm(x * .5, y * .4, tx) - .5) * .4, x, y), True)
    stone = ramp(12, 0.22, 5, 0.12, 0.46)
    lava = [(150, 30, 20), (220, 80, 30), (255, 150, 40), (255, 220, 120)]
    def tex(X, Z, x, y):
        if abs(abs(X) - 1.9) < 0.12:          # lava channels flanking the field
            return (pick(lava, 0.6 + fbm(X * 4, Z * 8, 122) * 0.5, x, y), True)
        u, v = X * 2.0, Z * 2.6
        fu, fv = u - math.floor(u), v - math.floor(v)
        if fu < 0.05 or fv < 0.06:
            return stone[0]
        return pick(stone, 0.55 + (_h(math.floor(u), math.floor(v), 123) - .5) * .35 + (fbm(X * 3, Z * 3, 124) - .5) * .3, x, y)
    arena_floor(s, tex, (250, 200, 120), [(40, 50, 80, 0.4), (200, 50, 80, 0.4)])
    return s


def arena_ghost():
    s = Scene()
    for y in range(FOOT):
        for x in range(W):
            n = fbm(x * 0.05, y * 0.08, 131)
            s.put(x, y, pick([(24, 14, 40), (40, 24, 66), (62, 38, 96), (90, 60, 130)], n * 0.9 - y * 0.004 + 0.1, x, y))
    # crescent moons and stars painted on the wall
    for mx, my in ((40, 18), (200, 18), (120, 12)):
        for y in range(my - 7, my + 8):
            for x in range(mx - 7, mx + 8):
                if math.hypot(x - mx, y - my) < 7 and math.hypot(x - mx - 3, y - my - 2) > 6:
                    s.put(x, y, (240, 220, 140))
    rnd = random.Random(132)
    for _ in range(50):
        x, y = rnd.randrange(W), rnd.randrange(FOOT - 8)
        s.put(x, y, (200, 180, 240))
    # heavy curtains at both sides
    cur = ramp(285, 0.5, 5, 0.12, 0.45)
    for x in list(range(0, 26)) + list(range(214, W)):
        for y in range(FOOT):
            fold = math.sin(x * 0.9) * 0.4
            s.put(x, y, pick(cur, 0.45 + fold - (0.2 if y < 4 else 0), x, y))
    wall_trim(s, FOOT - 2, (20, 10, 30), (160, 120, 200))
    a, b = (70, 44, 100), (30, 20, 44)
    def tex(X, Z, x, y):
        chk = (math.floor(X * 2.4) + math.floor(Z * 3.2)) % 2
        c = a if chk else b
        # a glossy floor: the wall's glow reflected near the back
        refl = max(0.0, 0.32 - (Z - 2.0) * -0.12) if Z > 2.0 else 0
        if refl and (x + y) % 2 == 0:
            c = mix(c, (150, 110, 200), min(0.4, refl))
        return c
    arena_floor(s, tex, (200, 170, 255), [(120, 40, 120, 0.25)])
    # candles on stands, flames that keep glowing at night
    for cx, cy in ((34, 70), (206, 70), (18, 100), (222, 100)):
        glow(s, cx, cy - 12, 12, (255, 200, 120), 0.18)
        for y in range(cy - 10, cy + 4):
            s.put(cx, y, (230, 220, 210)); s.put(cx + 1, y, (190, 180, 170))
        for y in range(cy - 15, cy - 10):
            s.put(cx, y, (255, 220, 120), True)
        s.put(cx + 1, cy - 13, (255, 250, 220), True)
    return s


def arena_ice():
    s = Scene()
    ice = ramp(196, 0.45, 5, 0.42, 0.92, shift=0.6)
    for y in range(FOOT):
        for x in range(W):
            d, gap, cid = voronoi(x / 18.0, y / 12.0, 141)
            if gap < 0.05:
                s.put(x, y, (130, 180, 214))
                continue
            lit = 0.7 - d * 0.5 + (_h(cid, 3, 142) - 0.5) * 0.3
            c = pick(ice, lit, x, y)
            if gap < 0.12 and d < 0.3:
                c = (255, 255, 255)                 # a glint near each block's crest
            s.put(x, y, c)
    # icicles hanging off the top
    rnd = random.Random(143)
    for x in range(0, W, 3):
        ln = rnd.randrange(2, 10)
        for y in range(ln):
            for w in range(max(1, 2 - y // 4)):
                s.put(x + w, y, (240, 252, 255) if w == 0 else (170, 214, 236))
    wall_trim(s, FOOT - 2, (110, 160, 200), (250, 255, 255))
    def tex(X, Z, x, y):
        n = fbm(X * 2, Z * 4, 144)
        c = pick(ice, 0.55 + (n - 0.5) * 0.4 + (Z - 1) * 0.1, x, y)
        # reflection streaks: long skids across the ice
        if abs(math.sin(X * 3.0 + Z * 9.0)) < 0.05 and _h(int(X * 3), int(Z * 2), 145) > 0.4:
            c = (255, 255, 255)
        drift = abs(X) - 1.7 - (Z - 1.0) * 1.6 + (n - 0.5) * 0.4
        if drift > 0 and Z < 1.9:                   # snow drifted into the near corners
            c = pick([(200, 220, 240), (232, 242, 252), (255, 255, 255)], 0.4 + drift * 1.5, x, y)
        return c
    arena_floor(s, tex, (70, 140, 210), [(120, 60, 120, 0.4)])
    return s


def arena_dragon():
    s = Scene()
    # a grand hall: dark back, marble pillars, gold trim, red banners
    for y in range(FOOT):
        for x in range(W):
            s.put(x, y, pick([(20, 16, 30), (34, 26, 46), (50, 38, 64)], 0.6 - y * 0.008, x, y))
    marble = ramp(40, 0.08, 5, 0.42, 0.9)
    for px in (20, 76, 164, 220):
        for y in range(FOOT):
            for x in range(px - 9, px + 10):
                t = (x - px + 9) / 18
                vein = abs(fbm(x * 0.1, y * 0.25, px) - 0.5) < 0.02
                c = (150, 140, 120) if vein else pick(marble, 0.9 - abs(t - 0.35) * 1.4, x, y)
                if y < 4 or y > FOOT - 7:
                    c = (230, 190, 80) if y in (3, FOOT - 6) else c
                s.put(x, y, c)
    for bx in (48, 120, 192):
        for y in range(2, 34):
            for x in range(bx - 10, bx + 11):
                if y > 28 and abs(x - bx) > (34 - y) * 2:
                    continue
                edge = abs(x - bx) > 8
                s.put(x, y, (230, 190, 80) if edge else pick(ramp(355, 0.6, 4, 0.2, 0.5), 0.7 - abs(x - bx) * 0.04, x, y))
        # the emblem: a stylised dragon eye
        for y in range(10, 22):
            for x in range(bx - 6, bx + 7):
                if abs(x - bx) / 6 + abs(y - 16) / 6 < 1:
                    s.put(x, y, (240, 200, 90))
                if abs(x - bx) < 2 and 12 < y < 20:
                    s.put(x, y, (40, 20, 20))
    wall_trim(s, FOOT - 2, (90, 70, 30), (240, 210, 110))
    marb = ramp(40, 0.06, 5, 0.5, 0.9)
    carpet = ramp(355, 0.62, 4, 0.22, 0.5)
    def tex(X, Z, x, y):
        if abs(X) < 0.5:                             # the carpet runs to the throne
            if abs(abs(X) - 0.45) < 0.05:
                return (230, 190, 80)
            return pick(carpet, 0.55 + (fbm(X * 6, Z * 6, 151) - .5) * .3, x, y)
        u, v = X * 1.5, Z * 2.0
        fu, fv = u - math.floor(u), v - math.floor(v)
        if fu < 0.03 or fv < 0.04:
            return (150, 140, 128)
        vein = abs(fbm(X * 3, Z * 3, 152) - 0.5) < 0.015
        return (170, 160, 150) if vein else pick(marb, 0.7 + (fv - 0.5) * 0.2, x, y)
    arena_floor(s, tex, (240, 210, 110), [(60, 40, 90, 0.35), (180, 40, 90, 0.35)])
    return s


# name -> builder. Day/night pairs for the biomes; arenas are indoors, one each.
SCENES = {
    'OCEAN': lambda: ocean()[0],
    'OCEAN NIGHT': ocean_night,
    'VOLCANO': lambda: volcano()[0],
    'VOLCANO NIGHT': volcano_night,
    'ARENA ROCK': arena_rock,
    'ARENA WATER': arena_water,
    'ARENA ELECTRIC': arena_electric,
    'ARENA GRASS': arena_grass,
    'ARENA FIRE': arena_fire,
    'ARENA GHOST': arena_ghost,
    'ARENA ICE': arena_ice,
    'ARENA DRAGON': arena_dragon,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('names', nargs='*', help='scenes to draw (default: all)')
    ap.add_argument('--force', action='store_true', help='overwrite existing PNGs')
    ap.add_argument('--out', default=OUT, help='output folder (default tools/backs)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name in a.names or SCENES:
        if name not in SCENES:
            raise SystemExit('unknown scene %r; known: %s' % (name, ', '.join(SCENES)))
        path = os.path.join(a.out, name + '.png')
        if os.path.exists(path) and not a.force:
            print('keep   %s (exists; --force to redraw)' % name)
            continue
        im = SCENES[name]().image()
        n = len(im.getcolors(1 << 16))
        if n > 256:   # gen_backs.py packs 8bpp; never hand it an image it refuses
            im = im.quantize(255, dither=Image.Dither.NONE).convert('RGB')
        im.save(path)
        print('draw   %s (%d colours)' % (name, min(n, 255)))


if __name__ == '__main__':
    main()
