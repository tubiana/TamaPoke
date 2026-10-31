# Unpacks a web/sprites-<region>.pak (pack_bundle.py's TPAK format) into a
# folder, so the emulator can draw the real sprites in the manual's captures.
#   python unpack_pak.py <file.pak> <out dir>
import os, struct, sys

pak, out = sys.argv[1], sys.argv[2]
d = open(pak, 'rb').read()
if d[:4] != b'TPAK':
    raise SystemExit('not a TPAK file: ' + pak)
n = struct.unpack('<H', d[4:6])[0]
p, ents = 6, []
for _ in range(n):
    l = d[p]; p += 1
    name = d[p:p + l].decode(); p += l
    size = struct.unpack('<I', d[p:p + 4])[0]; p += 4
    ents.append((name, size))
for name, size in ents:
    if name.startswith('/') or '..' in name:
        raise SystemExit('refusing path: ' + name)
    f = os.path.join(out, name)
    os.makedirs(os.path.dirname(f), exist_ok=True)
    with open(f, 'wb') as o:
        o.write(d[p:p + size])
    p += size
print('%d files into %s' % (n, out))
