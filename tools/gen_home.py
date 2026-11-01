#!/usr/bin/env python3
"""The main screen's scenes, drawn by code: tools/backs/HOME <BIOME> <PHASE>.png

    python3 tools/gen_home.py                  # writes only the PNGs that are missing
    python3 tools/gen_home.py --force          # redraws all of them
    python3 tools/gen_home.py --out DIR        # preview elsewhere

One scene per biome (dex.h's six), each in four lights: DAWN, DAY, DUSK and
NIGHT. Every PNG is 233x156: drawn at 2x it covers the panel from the top down
to y=312, where the status panel takes over.

The four lights of a biome share ONE set of pixels in the firmware, with four
palettes (gen_backs.py pairs them up pixel by pixel), so a whole day of light
costs one image. That is also the rule for a replacement: drop in four PNGs
under the same names, of the same picture in four lights. Pictures that differ
in shape still pack, just with fewer colours.

Kept clear on purpose, since the firmware draws over them: the middle of the
ground (x 70-160, y 70-156), where the creature stands; the sky under the name
(y 20-56); and the bottom-left corner, where the birthday present sits.

Same drawing kit as gen_scenes.py (ramps, Bayer dither, perspective floors), so
the home and the battle backdrops look like one game.
"""
import argparse
import math
import os
import random

import gen_scenes as g
from gen_scenes import ramp, pick, mix, fbm, vnoise, _h, Scene

W, H = 233, 156
g.W, g.H = W, H          # the kit reads its canvas size from these
HORIZON_Y = 96           # where the far land meets the ground

PHASES = ['DAWN', 'DAY', 'DUSK', 'NIGHT']
SKY = {   # top, bottom
    'DAWN':  ((92, 96, 170), (250, 186, 150)),
    'DAY':   ((92, 166, 236), (206, 236, 250)),
    'DUSK':  ((70, 52, 120), (246, 140, 92)),
    'NIGHT': ((10, 16, 40), (34, 44, 86)),
}
# light falling on everything that is not sky: a multiply, then a lift
LIGHT = {
    'DAWN': ((1.02, 0.86, 0.86), (16, 4, 18)),
    'DAY':  ((1.0, 1.0, 1.0), (0, 0, 0)),
    'DUSK': ((1.04, 0.78, 0.7), (18, 0, 6)),
}


def lit(s, phase):
    if phase == 'DAY':
        return s
    if phase == 'NIGHT':
        return g.to_night(s)
    (mr, mg, mb), (ar, ag, ab) = LIGHT[phase]
    for y in range(H):
        for x in range(W):
            if s.sky[y][x] or s.emit[y][x]:
                continue
            r, gg, b = s.px[y][x]
            s.px[y][x] = (min(255, int(r * mr + ar)), min(255, int(gg * mg + ag)), min(255, int(b * mb + ab)))
    return s


def sky_and_light(s, phase):
    top, bot = SKY[phase]
    g.sky(s, 0, HORIZON_Y + 4, top, bot)
    # the sun or the moon sits low on the right, clear of the name
    if phase == 'DAY':
        g.orb(s, 196, 22, 7, (255, 250, 220), (255, 246, 200))
    elif phase == 'DAWN':
        g.orb(s, 200, 70, 8, (255, 236, 196), (255, 200, 160))
    elif phase == 'DUSK':
        g.orb(s, 70, 84, 9, (255, 214, 150), (255, 160, 100))


def night_sky(s, seed):
    """After to_night: stars where there is sky, and a moon."""
    g.stars(s, seed, 70)
    g.orb(s, 196, 24, 6, (236, 240, 255), (150, 170, 230))
    for y in range(16, 32):         # a crescent: bite the disc with the sky colour
        for x in range(190, 206):
            if math.hypot(x - 199, y - 21) < 6 and math.hypot(x - 196, y - 24) <= 6:
                s.px[y][x] = s.px[y][186]


def clouds(s, seed, tones=((196, 214, 236), (230, 240, 252), (255, 255, 255))):
    g.cloud(s, 34, 66, 7, seed, tones)
    g.cloud(s, 214, 50, 5, seed + 1, tones)


def ridge(s, y_base, amp, freq, seed, rmp, haze, far, top_col=None):
    """A band of hills: noise ridge, lit from the left, hazed with distance."""
    for x in range(W):
        h = amp * (0.55 + 0.6 * fbm(x * freq, 0, seed, 3))
        top = int(y_base - h)
        hl = amp * (0.55 + 0.6 * fbm((x - 1) * freq, 0, seed, 3))
        hr = amp * (0.55 + 0.6 * fbm((x + 1) * freq, 0, seed, 3))
        slope = hl - hr
        for y in range(top, y_base + 1):
            t = 0.55 + max(-0.2, min(0.2, -slope * 0.5)) - (y - top) * 0.025
            t += (fbm(x * 0.06, y * 0.2, seed + 3) - 0.5) * 0.2
            s.put(x, y, mix(pick(rmp, t, x, y), haze, far))
            s.sky[y][x] = False
        if top_col:
            s.put(x, top, mix(top_col, haze, far))


def ground(s, rmp, seed, y0=HORIZON_Y, tex=None):
    """Perspective ground from y0 to the panel: rows widen toward the viewer."""
    for y in range(y0, H):
        z = (H - y0 + 6) / (y - y0 + 6)          # 1 at the bottom, larger far away
        for x in range(W):
            X = (x - W / 2) / (y - y0 + 6) * 6
            n = fbm(X * 0.9, z * 5.0, seed)
            t = 0.35 + (1 - min(1, z / 6)) * 0.35 + (n - 0.5) * 0.5
            c = tex(X, z, x, y, t) if tex else None
            s.put(x, y, c if c else pick(rmp, t, x, y))


def tuft(s, x, y, rmp, h=4):
    for dx, hh in ((-1, h - 1), (0, h), (1, h - 2), (2, h - 1)):
        for k in range(hh):
            s.put(x + dx, y - k, rmp[min(len(rmp) - 1, 1 + k * 3 // max(1, hh))])


def flower(s, x, y, col, centre=(255, 230, 90)):
    s.put(x, y + 1, (60, 120, 50))
    s.put(x, y + 2, (60, 120, 50))
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        s.put(x + dx, y + dy, col)
    s.put(x, y, centre)


def pine(s, cx, base, h, rmp, snow=False, trunk=(80, 54, 40)):
    for y in range(base - 3, base + 1):
        s.put(cx, y, trunk)
        s.put(cx + 1, y, trunk)
    tiers = 3
    for k in range(tiers):
        top = base - 3 - h + k * h // (tiers + 1)
        bot = base - 3 - (tiers - 1 - k) * h // (tiers + 2)
        for y in range(top, bot + 1):
            half = (y - top) * (h // 3 + 2) / max(1, bot - top) + 0.5
            for x in range(int(cx - half), int(cx + half) + 2):
                side = (x - cx) / max(1.0, half)
                t = 0.62 - side * 0.3 - (y - top) / max(1, bot - top) * 0.25
                c = pick(rmp, t, x, y)
                if snow and y - top < 2 + (x % 3 == 0):
                    c = (236, 244, 252)
                s.put(x, y, c)
                if 0 <= y < H and 0 <= x < W:
                    s.sky[y][x] = False


def bush(s, cx, cy, r, rmp, seed, berries=None):
    rnd = random.Random(seed)
    for y in range(int(cy - r), int(cy + 1)):
        for x in range(int(cx - r * 1.4), int(cx + r * 1.4) + 1):
            d = math.hypot((x - cx) / 1.4, y - cy) / r
            d += (fbm(x * 0.5, y * 0.5, seed) - 0.5) * 0.4
            if d < 1:
                s.put(x, y, pick(rmp, 0.85 - d * 0.4 - (y - cy + r) / (2 * r) * -0.2 + (cx - x) / (r * 4), x, y))
    if berries:
        for _ in range(int(r)):
            a = rnd.uniform(math.pi, 2 * math.pi)
            s.put(int(cx + math.cos(a) * r * 0.9), int(cy + math.sin(a) * r * 0.6), berries)


# =====================================================================
# the six homes
# =====================================================================

def meadow(phase):
    s = Scene()
    sky_and_light(s, phase)
    clouds(s, 3)
    haze = SKY[phase][1] if phase != 'NIGHT' else SKY['DAY'][1]
    green = ramp(110, 0.45, 5, 0.22, 0.62)
    ridge(s, 84, 14, 0.035, 7, ramp(150, 0.3, 5, 0.25, 0.6), haze, 0.55)
    ridge(s, HORIZON_Y, 10, 0.05, 8, green, haze, 0.15, top_col=green[-1])
    # a few round trees on the near hill
    tree = ramp(120, 0.5, 5, 0.18, 0.55)
    for tx, ty, r in ((12, 92, 9), (24, 95, 6), (204, 90, 8), (222, 94, 7)):
        for y in range(ty - 1, ty + 5):
            s.put(tx, y, (92, 64, 44)); s.put(tx + 1, y, (92, 64, 44))
        bush(s, tx, ty, r, tree, tx)
    grass = ramp(100, 0.5, 5, 0.3, 0.66)
    ground(s, grass, 21)
    rnd = random.Random(5)
    for _ in range(70):                       # tufts thicken toward the viewer
        y = rnd.randrange(HORIZON_Y + 4, H)
        x = rnd.randrange(W)
        if 66 < x < 166 and y > HORIZON_Y + 10:
            continue
        tuft(s, x, y, grass, 2 + (y - HORIZON_Y) // 14)
    cols = [(240, 96, 110), (255, 220, 90), (250, 250, 255), (180, 140, 240)]
    for _ in range(26):
        y = rnd.randrange(HORIZON_Y + 6, H - 3)
        x = rnd.choice((rnd.randrange(3, 64), rnd.randrange(170, W - 3)))
        flower(s, x, y, rnd.choice(cols))
    # a little wooden fence on the right
    wood = ramp(28, 0.4, 4, 0.25, 0.6)
    for i, px in enumerate(range(176, 232, 11)):
        base = 120 + i
        for y in range(base - 13, base + 1):
            s.put(px, y, pick(wood, 0.7, px, y)); s.put(px + 1, y, pick(wood, 0.4, px + 1, y))
        s.put(px, base - 14, wood[-1]); s.put(px + 1, base - 14, wood[-1])
    for k in (0, 1):
        for x in range(176, 233):
            y = int(120 - 10 + k * 6 + (x - 176) / 11)
            s.put(x, y, wood[2]); s.put(x, y + 1, wood[1])
    return s


def beach(phase):
    s = Scene()
    sky_and_light(s, phase)
    clouds(s, 11)
    # sea to the horizon, with streaks and a lighter band at the shore
    sea = [(18, 70, 150), (30, 100, 180), (44, 130, 204), (70, 164, 222), (130, 204, 238)]
    sea_top = 78
    for y in range(sea_top, 106):
        t = (y - sea_top) / (106 - sea_top)
        for x in range(W):
            n = fbm(x * 0.06, y * 0.4, 31) - 0.5
            s.put(x, y, pick(sea, 0.25 + t * 0.6 + n * 0.3, x, y))
            s.sky[y][x] = False
    for x in range(W):
        s.put(x, sea_top, (186, 226, 246))
    rnd = random.Random(9)
    for y in range(sea_top + 3, 104, 3):
        for _ in range(6):
            x = rnd.randrange(W); L = 2 + (y - sea_top) // 5
            for i in range(L):
                s.put(x + i, y, (190, 232, 250))
    # a far island
    green = ramp(135, 0.45, 5, 0.2, 0.55)
    for x in range(150, 196):
        h = 7 * math.sin(math.pi * (x - 150) / 46) ** 0.8 * (0.7 + 0.6 * fbm(x * 0.2, 0, 3))
        for y in range(int(sea_top - h), sea_top):
            s.put(x, y, mix(pick(green, 0.5 - (y - sea_top + h) * 0.03, x, y), SKY['DAY'][1], 0.45))
            s.sky[y][x] = False
    # sand, wet at the waterline, foam on top
    sand = ramp(40, 0.5, 5, 0.5, 0.85)
    for y in range(104, H):
        for x in range(W):
            shore = 106 + 3 * math.sin(x * 0.07 + 1) + 2 * fbm(x * 0.1, 0, 4)
            if y < shore:
                s.put(x, y, (226, 246, 252) if y > shore - 2 else pick(sea, 0.9, x, y))
                continue
            wet = max(0, 1 - (y - shore) / 6)
            n = fbm(x * 0.2, y * 0.3, 12) - 0.5
            c = pick(sand, 0.62 + n * 0.4 - (y - 104) * 0.004, x, y)
            s.put(x, y, mix(c, (170, 140, 100), wet * 0.6))
    # shells and pebbles
    for _ in range(22):
        x, y = rnd.randrange(W), rnd.randrange(114, H)
        if 64 < x < 168:
            continue
        s.put(x, y, rnd.choice([(250, 220, 210), (255, 255, 240), (160, 140, 120)]))
    # a palm on the left, leaning in
    trunk = ramp(30, 0.35, 4, 0.3, 0.6)
    for i in range(52):
        x = int(18 + i * 0.32 + 3 * math.sin(i / 20)); y = 146 - i
        for w in range(3):
            s.put(x + w, y, pick(trunk, 0.7 - w * 0.25 - (i % 4 == 0) * 0.3, x + w, y))
            if 0 <= y < H and 0 <= x + w < W:
                s.sky[y][x + w] = False
    leaf = ramp(115, 0.55, 5, 0.18, 0.55)
    cx, cy = 35, 94
    for a0 in (-2.7, -2.2, -1.6, -1.0, -0.5, 0.1, 2.9):
        for k in range(26):
            t = k / 26
            x = cx + math.cos(a0) * k * 1.1
            y = cy + math.sin(a0) * k * 0.7 + (t ** 2) * 9
            for w in range(-2, 3):
                if abs(w) <= 2 * (1 - t) + 0.5:
                    s.put(int(x), int(y + w), pick(leaf, 0.75 - abs(w) * 0.18 - t * 0.2, int(x), int(y + w)))
    for dx in (-2, 1):
        s.put(cx + dx, cy + 2, (110, 70, 40)); s.put(cx + dx + 1, cy + 3, (90, 56, 32))
    # a sandcastle on the right
    for (bx, by, bw, bh) in ((182, 140, 26, 8), (186, 132, 8, 8), (198, 132, 8, 8), (191, 126, 9, 10)):
        for y in range(by, by + bh):
            for x in range(bx, bx + bw):
                s.put(x, y, pick(sand, 0.85 - (x - bx) / bw * 0.4 - (y == by) * -0.1, x, y))
        for x in range(bx, bx + bw, 3):
            s.put(x, by - 1, sand[3])
    s.put(195, 120, (230, 70, 70)); s.put(196, 120, (230, 70, 70)); s.put(195, 121, (230, 70, 70))
    for y in range(120, 126):
        s.put(194, y, (110, 80, 60))
    return s


def forest(phase):
    s = Scene()
    sky_and_light(s, phase)
    clouds(s, 17)
    haze = SKY['DAY'][1]
    far = ramp(150, 0.35, 5, 0.18, 0.5)
    near = ramp(130, 0.5, 5, 0.12, 0.48)
    # rows of pines, far to near
    rnd = random.Random(4)
    for x in range(-6, W + 6, 9):
        pine(s, x + rnd.randrange(-2, 3), 99 + rnd.randrange(-2, 2), 26 + rnd.randrange(0, 8),
             [mix(c, haze, 0.45) for c in far])
    for x in list(range(-4, 66, 15)) + list(range(172, W + 8, 15)):
        pine(s, x + rnd.randrange(-3, 4), 103 + rnd.randrange(-2, 3), 38 + rnd.randrange(0, 10), near)
    # a mossy clearing
    moss = ramp(95, 0.45, 5, 0.22, 0.56)
    def tex(X, z, x, y, t):
        # a dirt path winding to the back
        p = abs(X - 0.6 * math.sin(z * 0.9))
        if p < 0.9 / max(1, z ** 0.25):
            return pick(ramp(32, 0.35, 4, 0.3, 0.55), t, x, y)
        return None
    ground(s, moss, 41, y0=HORIZON_Y + 2, tex=tex)
    # big trunks framing the clearing
    bark = ramp(25, 0.35, 5, 0.12, 0.45)
    for bx, bw in ((0, 12), (222, 11)):
        for y in range(0, H):
            for x in range(bx, bx + bw):
                t = 0.3 + (x - bx) / bw * 0.4 + (fbm(x * 0.3, y * 0.08, bx) - 0.5) * 0.5
                if bx > 100:
                    t = 0.7 - (x - bx) / bw * 0.4 + (fbm(x * 0.3, y * 0.08, bx) - 0.5) * 0.5
                s.put(x, y, pick(bark, t, x, y))
                s.sky[y][x] = False
    # mushrooms and a stump
    for mx, my, cap in ((28, 140, (220, 60, 60)), (36, 146, (230, 80, 70)), (196, 138, (240, 190, 90))):
        for y in range(my - 3, my + 1):
            s.put(mx, y, (240, 230, 210))
        for x in range(mx - 3, mx + 4):
            for y in range(my - 6, my - 2):
                if (x - mx) ** 2 / 12 + (y - my + 3) ** 2 / 5 <= 1:
                    s.put(x, y, mix(cap, (255, 255, 255), 0.0 if (x + y) % 5 else 0.7))
    for y in range(126, 138):
        for x in range(176, 194):
            s.put(x, y, pick(bark, 0.55 - (x - 176) / 18 * 0.3, x, y))
    for x in range(176, 194):
        s.put(x, 125, (200, 160, 110)); s.put(x, 126, (170, 130, 90))
    for x in range(178, 192, 4):
        s.put(x, 125, (150, 110, 70))
    # light shafts through the canopy (daylight only: they would glow at night)
    if phase != 'NIGHT':
        for x0 in (70, 120, 160):
            for y in range(0, HORIZON_Y + 30):
                for w in range(5):
                    x = x0 + w + y // 3
                    if 0 <= x < W and (x + y) % 2 == 0:
                        s.blend(x, y, (255, 250, 210), 0.12)
    return s


def volcano(phase):
    s = Scene()
    top, bot = SKY[phase]
    if phase in ('DAY', 'DAWN'):
        top, bot = mix(top, (120, 70, 80), 0.4), mix(bot, (240, 150, 100), 0.5)
    g.sky(s, 0, HORIZON_Y + 4, top, bot)
    if phase == 'DAY':
        g.orb(s, 196, 22, 7, (255, 236, 200), (255, 200, 160))
    # a distant jagged ridge on the right
    far = ramp(350, 0.2, 4, 0.1, 0.3)
    for x in range(W):
        t0 = int(78 + 6 * fbm(x * 0.06, 3, 43) + 3 * math.sin(x * 0.13))
        for y in range(t0, HORIZON_Y + 1):
            s.put(x, y, pick(far, 0.8 - (y - t0) * 0.05, x, y))
            s.sky[y][x] = False
        s.put(x, t0, (150, 80, 70))
    # the cone, far left of centre, with a glowing crater and a smoke plume
    rockr = ramp(10, 0.28, 5, 0.1, 0.42)
    lava = [(150, 30, 20), (220, 80, 30), (255, 150, 40), (255, 220, 120)]
    cx, peak, base = 74, 36, HORIZON_Y
    ash = [(70, 52, 64), (96, 74, 86), (128, 104, 112)]
    for i in range(10):
        pcx, pcy, r = cx + 6 + i * 7, peak - 4 - i * 2.2, 4 + i * 0.8
        for y in range(int(pcy - r), int(pcy + r) + 1):
            for x in range(int(pcx - r * 1.3), int(pcx + r * 1.3) + 1):
                d = math.hypot((x - pcx) / 1.3, y - pcy) / r
                if d < 1 and 0 <= y < H and 0 <= x < W:
                    s.put(x, y, pick(ash, 1 - d * 0.6 + fbm(x * .2, y * .2, i) * .3 - 0.2, x, y))
    for y in range(peak, base):
        k = (y - peak) / (base - peak)
        half = 7 + 66 * k ** 1.5
        for x in range(int(cx - half), int(cx + half) + 1):
            t = (x - cx) / half
            gully = math.sin(t * 9 + fbm(x * 0.1, y * 0.1, 42) * 4)
            n = fbm(x * 0.15, y * 0.15, 41)
            s.put(x, y, pick(rockr, 0.5 - t * 0.35 + gully * 0.15 + (n - 0.5) * 0.3 - k * 0.15, x, y))
            s.sky[y][x] = False
    for x in range(cx - 7, cx + 8):
        s.put(x, peak, (255, 170, 60), True); s.put(x, peak + 1, (220, 90, 40), True)
    xx = cx + 2.0
    for y in range(peak + 1, base):
        xx += math.sin(y * 0.3) * 0.5 + 0.2
        for w in (-1, 0, 1):
            s.put(int(xx) + w, y, pick(lava, 0.9 - abs(w) * 0.4 - (y - peak) / 70, int(xx) + w, y), True)
    g.glow(s, cx, peak + 4, 22, (255, 120, 40), 0.25)
    # basalt ground with a few glowing seams, kept away from the middle
    basalt = ramp(15, 0.14, 5, 0.14, 0.46)
    def tex(X, z, x, y, t):
        d, gap, cid = g.voronoi(X * 0.3, z * 0.9, 52)
        if gap < 0.03 and z < 2.2 and not (60 < x < 172):
            if _h(cid & 0xFFFF, 1, 54) < 0.5:
                return basalt[0]
            return (pick(lava, 0.95 - gap * 10, x, y), True)
        return pick(basalt, t - 0.05 + (_h(cid, 0, 53) - 0.5) * 0.25, x, y)
    for y in range(HORIZON_Y, H):
        z = (H - HORIZON_Y + 6) / (y - HORIZON_Y + 6)
        for x in range(W):
            X = (x - W / 2) / (y - HORIZON_Y + 6) * 6
            n = fbm(X * 0.8, z * 2.2, 51)
            r = tex(X, z, x, y, 0.35 + (1 - min(1, z / 6)) * 0.35 + (n - 0.5) * 0.5)
            s.put(x, y, *r) if isinstance(r[0], tuple) else s.put(x, y, r)
    # a lava pool front left, obsidian rocks front right
    pcx, pcy, rx, ry = 26, 136, 24, 7
    for y in range(pcy - ry - 1, pcy + ry + 2):
        for x in range(pcx - rx - 2, pcx + rx + 3):
            d = math.hypot((x - pcx) / rx, (y - pcy) / ry)
            if d < 1:
                s.put(x, y, pick(lava, 0.45 + fbm(x * 0.3, y * 0.6, 61) * 0.7 - d * 0.25, x, y), True)
            elif d < 1.2:
                s.put(x, y, (40, 22, 22))
    g.glow(s, pcx, pcy - 2, rx * 1.4, (255, 120, 40), 0.3)
    obs = ramp(260, 0.2, 5, 0.08, 0.4)
    g.rock(s, 206, 134, 14, 12, 3, obs)
    g.rock(s, 186, 144, 8, 6, 4, obs)
    return s


def mountain(phase):
    s = Scene()
    sky_and_light(s, phase)
    clouds(s, 23)
    haze = SKY['DAY'][1]
    # snowy peaks far away, a closer rocky ridge
    for cx, peak, half, sd, far in ((60, 20, 70, 1, 0.5), (170, 12, 80, 2, 0.45), (120, 40, 60, 3, 0.2)):
        rockr = ramp(220, 0.12, 5, 0.25, 0.7)
        for y in range(peak, HORIZON_Y):
            k = (y - peak) / (HORIZON_Y - peak)
            hw = half * k * (0.9 + 0.2 * fbm(y * 0.1, sd, sd))
            for x in range(int(cx - hw), int(cx + hw) + 1):
                if not (0 <= x < W):
                    continue
                t = (x - cx) / max(1, hw)
                snowline = peak + (HORIZON_Y - peak) * (0.3 + 0.12 * math.sin(x * 0.3 + sd))
                c = pick(rockr, 0.6 - t * 0.35 + (fbm(x * 0.2, y * 0.2, sd) - 0.5) * 0.4, x, y)
                if y < snowline:
                    c = (230, 238, 250) if t < 0.1 else (196, 210, 232)
                s.put(x, y, mix(c, haze, far))
                s.sky[y][x] = False
    pines = ramp(150, 0.4, 5, 0.12, 0.42)
    rnd = random.Random(8)
    for x in range(-4, W + 4, 7):
        pine(s, x + rnd.randrange(-2, 3), 98 + rnd.randrange(-2, 3), 12 + rnd.randrange(0, 6), pines)
    # an alpine meadow of rock and short grass
    grass = ramp(90, 0.35, 5, 0.28, 0.6)
    ground(s, grass, 61, y0=HORIZON_Y + 2)
    stone = ramp(215, 0.15, 5, 0.2, 0.66)
    g.rock(s, 16, 132, 20, 16, 5, stone)
    g.rock(s, 212, 128, 18, 20, 6, stone)
    g.rock(s, 194, 146, 8, 5, 7, stone)
    for _ in range(40):
        y = rnd.randrange(HORIZON_Y + 6, H)
        x = rnd.randrange(W)
        if 60 < x < 172:
            continue
        tuft(s, x, y, grass, 2 + (y - HORIZON_Y) // 18)
    for _ in range(10):   # edelweiss
        y = rnd.randrange(110, H - 3)
        x = rnd.choice((rnd.randrange(30, 62), rnd.randrange(172, 196)))
        flower(s, x, y, (250, 250, 250), (240, 220, 120))
    return s


def snow(phase):
    s = Scene()
    sky_and_light(s, phase)
    clouds(s, 29, ((190, 204, 226), (226, 234, 246), (250, 252, 255)))
    haze = SKY['DAY'][1]
    hills = ramp(215, 0.25, 5, 0.6, 0.94)
    ridge(s, HORIZON_Y, 22, 0.03, 12, hills, haze, 0.35, top_col=(250, 252, 255))
    pines = ramp(170, 0.3, 5, 0.12, 0.4)
    rnd = random.Random(14)
    for x in list(range(0, 70, 11)) + list(range(166, W + 6, 11)):
        pine(s, x + rnd.randrange(-2, 3), 99 + rnd.randrange(-2, 3), 20 + rnd.randrange(0, 10), pines, snow=True)
    snowr = ramp(212, 0.3, 5, 0.66, 0.96)
    ground(s, snowr, 71)
    # sparkle and footprints
    for _ in range(30):
        x, y = rnd.randrange(W), rnd.randrange(HORIZON_Y + 4, H)
        s.put(x, y, (255, 255, 255))
    for i in range(7):
        x, y = 190 - i * 9, 150 - i * 4
        s.put(x, y, snowr[1]); s.put(x + 1, y, snowr[1]); s.put(x + 3, y + 2, snowr[1])
    # a snowman on the left, an igloo on the right
    for cy, r in ((142, 9), (128, 7), (117, 5)):
        for y in range(cy - r, cy + r + 1):
            for x in range(28 - r, 28 + r + 1):
                d = math.hypot(x - 28, y - cy) / r
                if d <= 1:
                    s.put(x, y, pick(snowr, 0.95 - (x - 28 + r) / (2 * r) * 0.5 - max(0, d - 0.8), x, y))
    s.put(26, 115, (30, 30, 40)); s.put(30, 115, (30, 30, 40))
    s.put(28, 117, (250, 130, 40)); s.put(29, 117, (250, 130, 40)); s.put(30, 118, (250, 130, 40))
    for y in (126, 130, 134):
        s.put(28, y, (40, 40, 50))
    for y in range(110, 113):
        for x in range(23, 34):
            s.put(x, y, (200, 60, 60) if y > 110 else (220, 80, 80))
    icx, icy, ir = 204, 140, 18
    for y in range(icy - ir, icy + 1):
        for x in range(icx - ir, icx + ir + 1):
            d = math.hypot(x - icx, (y - icy) * 1.15) / ir
            if d <= 1:
                row = (icy - y) // 4
                brick = (y % 4 == 0) or ((x + row * 3) % 8 == 0)
                c = pick(snowr, 0.95 - (x - icx + ir) / (2 * ir) * 0.7, x, y)
                if d > 0.92:
                    c = (120, 140, 180)
                s.put(x, y, mix(c, (130, 150, 190), 0.45) if brick else c)
    for y in range(icy - 8, icy + 1):
        for x in range(icx - 12, icx - 3):
            if math.hypot(x - icx + 7.5, (y - icy) * 1.0) < 6:
                s.put(x, y, (40, 50, 80))
    return s


BIOMES = [('MEADOW', meadow), ('BEACH', beach), ('FOREST', forest),
          ('VOLCANO', volcano), ('MOUNTAIN', mountain), ('SNOW', snow)]


def build(fn, phase):
    if phase == 'NIGHT':
        s = fn('DAY')
        n = lit(s, 'NIGHT')
        for y in range(H):              # the sun and its halo go, the night sky comes
            for x in range(W):
                if n.sky[y][x]:
                    n.px[y][x] = pick([mix(SKY['NIGHT'][0], SKY['NIGHT'][1], i / 5) for i in range(6)],
                                      y / (HORIZON_Y + 3), x, y)
        night_sky(n, 3)
        return n
    return lit(fn(phase), phase)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--force', action='store_true', help='overwrite existing PNGs')
    ap.add_argument('--out', default=g.OUT, help='output folder (default tools/backs)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name, fn in BIOMES:
        for phase in PHASES:
            path = os.path.join(a.out, 'HOME %s %s.png' % (name, phase))
            if os.path.exists(path) and not a.force:
                print('keep   %s' % os.path.basename(path))
                continue
            im = build(fn, phase).image()
            n = len(im.getcolors(1 << 16))
            if n > 255:
                im = im.quantize(255, dither=0).convert('RGB')
            im.save(path)
            print('draw   %s (%d colours)' % (os.path.basename(path), min(n, 255)))


if __name__ == '__main__':
    main()
