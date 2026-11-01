#!/usr/bin/env python3
"""Battle backgrounds: tools/backs/*.png -> backs.h

    python3 tools/gen_backs.py

Twelve 240x112 scenes, one day and one night per biome, packed 8bpp indexed
with a per-image RGB565 palette. 8bpp rather than 4bpp because the worst image
uses 65 colours and quantising a background to 16 would band the gradients;
315 KB in flash is affordable and it means a battle still looks like somewhere
even on a board with no SD card, which the streamed creature sprites cannot.

The rows are emitted as-is. The drawing side turns them into runs of identical
indices at draw time -- this art is flat pixel art with long horizontal runs, so
that is far cheaper than a fillRect per pixel.

Biome mapping is by closest available scene: dex.h has six biomes and the art
has ten kinds, so forest borrows PATH (a wooded route) and volcano borrows CAVE
(the only rocky interior). Nothing is volcanic in the set.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'backs')
OUT = os.path.join(HERE, '..', 'backs.h')

# biome index in dex.h -> (day file, night file)
BIOMES = [
    ('TALL GRASS', 'pradera / meadow'),
    ('BEACH',      'playa / beach'),
    ('PATH',       'bosque / forest -- a wooded route is the closest scene'),
    ('CAVE',       'volcan / volcano -- no volcanic scene exists; rock is closest'),
    ('MOUNTAIN',   'montana / mountain'),
    ('SNOW',       'nieve / snow'),
]

# gym arenas, indoors so one scene each. The firmware picks one by the leader's
# type (ARENA_OF_TYPE in TamaPoke.ino); the enum below is emitted in this order.
ARENAS = [
    ('ARENA ROCK',     'rock, ground, fighting, normal'),
    ('ARENA WATER',    'water'),
    ('ARENA ELECTRIC', 'electric, steel'),
    ('ARENA GRASS',    'grass, bug, flying'),
    ('ARENA FIRE',     'fire'),
    ('ARENA GHOST',    'ghost, psychic, poison, dark, fairy'),
    ('ARENA ICE',      'ice'),
    ('ARENA DRAGON',   'dragon, and every Champion'),
]

# places to explore that have no biome backdrop of their own, day and night.
# The other places reuse BACKS: the explore code maps place -> scene.
XBACKS = [
    ('OCEAN',   'the open sea'),
    ('VOLCANO', 'the volcano -- the volcano BIOME still battles in CAVE'),
]

# the explore map, full panel at 2x, with its pins from MAP.json
MAP = 'MAP'

# the main screen, one per biome in dex.h order (gen_home.py draws them), each
# in four lights that share ONE set of pixels: an index image plus a palette
# per light, so a whole day costs one picture.
HOMES = ['MEADOW', 'BEACH', 'FOREST', 'VOLCANO', 'MOUNTAIN', 'SNOW']
HOME_PHASES = ['DAWN', 'DAY', 'DUSK', 'NIGHT']
HOME_SIZE = (233, 156)     # to y=312 at 2x; the status panel covers the rest


def read_png(path):
    d = open(path, 'rb').read()
    pos, idat, w, h, bd, ct = 8, b'', 0, 0, 0, 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', data[:10])
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    if bd != 8:
        raise SystemExit('%s: only 8-bit channels supported' % path)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(idat)
    rows, prev, i = [], bytearray(w * ch), 0
    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + w * ch]); i += w * ch
        for x in range(len(line)):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1:   line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line)); prev = line
    return w, h, ch, rows


def load(path, size=(240, 112)):
    """Any picture -> (w, h, ch, rows) at the backdrop size, <= 255 colours.

    A file that is already 240x112 with few colours goes through read_png()
    untouched, so the supplied art packs byte for byte as before. Anything else
    -- a photo-like image from a generator, a different size -- is cropped to
    the backdrop's shape, scaled, and reduced to 255 colours. That needs
    Pillow, and only then."""
    w, h, ch, rows = read_png(path)
    if (w, h) == size and len({line[x:x + 3] for line in rows
                               for x in range(0, len(line), ch)}) <= 256:
        return w, h, ch, rows
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit('%s is %dx%d or has many colours: install Pillow to fit it' % (path, w, h))
    im = Image.open(path).convert('RGB')
    tw, th = size
    scale = max(tw / im.width, th / im.height)          # cover, then centre-crop
    im = im.resize((max(tw, round(im.width * scale)), max(th, round(im.height * scale))), Image.LANCZOS)
    l, t = (im.width - tw) // 2, (im.height - th) // 2
    im = im.crop((l, t, l + tw, t + th)).quantize(255, dither=Image.Dither.NONE).convert('RGB')
    raw = im.tobytes()
    return tw, th, 3, [raw[y * tw * 3:(y + 1) * tw * 3] for y in range(th)]


def to_indexed(w, h, ch, rows):
    pal, idx = [], []
    lut = {}
    for line in rows:
        for x in range(0, len(line), ch):
            r, g, b = line[x], line[x + 1], line[x + 2]
            c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            if c not in lut:
                if len(pal) >= 256:
                    raise SystemExit('more than 256 colours')
                lut[c] = len(pal); pal.append(c)
            idx.append(lut[c])
    return pal, idx


def to_indexed_multi(images):
    """Several pictures of one scene -> one index image + a palette each.

    A pixel's index stands for its colour in EVERY picture at once, so the
    four lights of a scene drawn from the same shapes need no more indices than
    one of them. Pictures that disagree in shape can exceed 256 combinations;
    the rarest are then folded into the nearest common one."""
    w, h, _, _ = images[0]
    keys = []
    for y in range(h):
        for x in range(w):
            k = []
            for (_, _, ch, rows) in images:
                r, g, b = rows[y][x * ch:x * ch + 3]
                k.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            keys.append(tuple(k))
    count = {}
    for k in keys:
        count[k] = count.get(k, 0) + 1
    common = sorted(count, key=lambda k: -count[k])[:256]
    slot = {k: i for i, k in enumerate(common)}

    def rgb(c):
        return ((c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3)

    def dist(a, b):
        return sum(sum((p - q) ** 2 for p, q in zip(rgb(u), rgb(v))) for u, v in zip(a, b))

    for k in count:
        if k not in slot:
            slot[k] = slot[min(common, key=lambda c: dist(c, k))]
    pals = [[k[i] for k in common] for i in range(len(images))]
    return pals, [slot[k] for k in keys]


def main():
    out = ['// GENERADO por tools/gen_backs.py desde tools/backs/*.png - no editar',
           '#pragma once', '#include <stdint.h>', '',
           '// Battle backgrounds, 8bpp indexed with a per-image RGB565 palette.',
           '// Indexed by biome (see dex.h) then 0 = day, 1 = night.', '']
    names = []
    total = 0

    def emit(file, nm, note, size=(240, 112)):
        nonlocal total
        w, h, ch, rows = load(os.path.join(SRC, file + '.png'), size)
        pal, idx = to_indexed(w, h, ch, rows)
        names.append((nm, w, h, len(pal)))
        total += len(idx) + len(pal) * 2
        out.append('// %s  (%s)' % (file, note))
        out.append('static const uint16_t %s_PAL[%d] = {' % (nm, len(pal)))
        out.append('  ' + ', '.join('0x%04X' % c for c in pal))
        out.append('};')
        out.append('static const uint8_t %s_IDX[%d] = {' % (nm, len(idx)))
        for r in range(0, len(idx), 40):
            out.append('  ' + ','.join(str(v) for v in idx[r:r + 40]) + ',')
        out.append('};')
        out.append('')

    for bi, (base, note) in enumerate(BIOMES):
        for ni, suffix in enumerate(('', ' NIGHT')):
            emit(base + suffix, 'BACK_%d_%d' % (bi, ni), note)
    for ai, (file, note) in enumerate(ARENAS):
        emit(file, 'ARENA_%d' % ai, note)
    for xi, (base, note) in enumerate(XBACKS):
        for ni, suffix in enumerate(('', ' NIGHT')):
            emit(base + suffix, 'XBACK_%d_%d' % (xi, ni), note)
    emit(MAP, 'MAPART', 'the explore map', size=(233, 233))
    homes = []
    for hi, nm in enumerate(HOMES):
        imgs = [load(os.path.join(SRC, 'HOME %s %s.png' % (nm, ph)), HOME_SIZE) for ph in HOME_PHASES]
        pals, idx = to_indexed_multi(imgs)
        n = len(pals[0])
        total += len(idx) + n * 2 * len(pals)
        homes.append(n)
        out.append('// HOME %s -- %s' % (nm, ', '.join(HOME_PHASES)))
        out.append('static const uint16_t HOME_%d_PAL[%d] = {' % (hi, n * len(pals)))
        for p in pals:
            out.append('  ' + ', '.join('0x%04X' % c for c in p) + ',')
        out.append('};')
        out.append('static const uint8_t HOME_%d_IDX[%d] = {' % (hi, len(idx)))
        for r in range(0, len(idx), 40):
            out.append('  ' + ','.join(str(v) for v in idx[r:r + 40]) + ',')
        out.append('};')
        out.append('')
    out.append('struct BackScene {')
    out.append('  const uint16_t *pal;')
    out.append('  const uint8_t *idx;')
    out.append('  uint16_t w, h, palCount;')
    out.append('};')
    out.append('#define BACK_BIOMES %d' % len(BIOMES))
    out.append('static const BackScene BACKS[BACK_BIOMES][2] = {')
    for bi in range(len(BIOMES)):
        row = []
        for ni in range(2):
            nm, w, h, pc = names[bi * 2 + ni]
            row.append('{ %s_PAL, %s_IDX, %d, %d, %d }' % (nm, nm, w, h, pc))
        out.append('  { %s },' % ', '.join(row))
    out.append('};')
    out.append('')
    out.append('enum : uint8_t { %s, ARENA_COUNT };'
               % ', '.join(f.replace(' ', '_') for f, _ in ARENAS))
    out.append('static const BackScene ARENAS[ARENA_COUNT] = {')
    for ai in range(len(ARENAS)):
        nm, w, h, pc = names[len(BIOMES) * 2 + ai]
        out.append('  { %s_PAL, %s_IDX, %d, %d, %d },' % (nm, nm, w, h, pc))
    out.append('};')
    out.append('')
    base = len(BIOMES) * 2 + len(ARENAS)
    out.append('enum : uint8_t { %s, XBACK_COUNT };'
               % ', '.join('XB_' + b for b, _ in XBACKS))
    out.append('static const BackScene XBACKS[XBACK_COUNT][2] = {')
    for xi in range(len(XBACKS)):
        row = []
        for ni in range(2):
            nm, w, h, pc = names[base + xi * 2 + ni]
            row.append('{ %s_PAL, %s_IDX, %d, %d, %d }' % (nm, nm, w, h, pc))
        out.append('  { %s },' % ', '.join(row))
    out.append('};')
    nm, w, h, pc = names[base + len(XBACKS) * 2]
    out.append('static const BackScene MAP_ART = { %s_PAL, %s_IDX, %d, %d, %d };' % (nm, nm, w, h, pc))
    # The main screen: `pal` holds HOME_PHASE_COUNT palettes of palCount back
    # to back, one per light, all over the same idx.
    out.append('enum : uint8_t { %s, HOME_PHASE_COUNT };' % ', '.join('HP_' + p for p in HOME_PHASES))
    out.append('#define HOME_COUNT %d' % len(HOMES))
    out.append('static const BackScene HOMES[HOME_COUNT] = {')
    for hi, nm_ in enumerate(HOMES):
        out.append('  { HOME_%d_PAL, HOME_%d_IDX, %d, %d, %d },   // %s'
                   % (hi, hi, HOME_SIZE[0], HOME_SIZE[1], homes[hi], nm_.lower()))
    out.append('};')
    # The pins: where each place sits, in map pixels, in the firmware's PL_*
    # order. From MAP.json so a replacement map brings its own.
    import json
    meta = json.load(open(os.path.join(SRC, MAP + '.json')))
    sc = (w / float(meta['size'])) if meta.get('size') else 1.0
    out.append('struct MapPin { uint8_t x, y; };')
    out.append('#define MAP_PIN_COUNT %d' % len(meta['pins']))
    out.append('static const MapPin MAP_PINS[MAP_PIN_COUNT] = {')
    for nm_, x, y in meta['pins']:
        out.append('  { %d, %d },   // %s' % (round(x * sc), round(y * sc), nm_))
    out.append('};')
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print('backgrounds: %d, %.1f KB of data -> %s'
          % (len(names), total / 1024.0, os.path.normpath(OUT)))


if __name__ == '__main__':
    main()
