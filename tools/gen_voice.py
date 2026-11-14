#!/usr/bin/env python3
"""Speech clips for the spoken UI (speech.cpp), one pack per UI language.

    python tools/gen_voice.py <lang> <voice.onnx>     # es en fr de it pt nl
    python tools/gen_voice.py --list <lang>           # what would be said, no audio

Needs piper-tts and numpy. Writes tools/sdcard/voice/<lang>/{index.bin,vNN.bin}
and web/voice-<lang>.pak (the web installer sends it like a sprite pack: same PUT
protocol, the files land in /voice/<lang>/ on the card).

What gets a clip, in that language:
  - every UI string, split at its %-placeholders (i18n.cpp), and the medal and
    sticker names
  - every Pokemon and move name as that language's screen shows it, every type
  - short capitalised UI literals in the code (region names, OK, BALL...)
  - the numbers 0..200 and the hundreds up to 1000: speech.cpp builds any
    other number out of those (347 is "300" + "47"), which saves ~1000 clips
  - every single word of all of the above, so text that only partly matches
    still gets read word by word
The board looks phrases up by an FNV-1a hash of the normalised words, greedy
longest match first, so speechTokens() in speech.cpp and tokens() here must agree.

The screen has no accents (the font cannot draw them), but the voice should not
say "bano" for BANO: RESPELL gives each language the spelling to SAY, and the
Pokemon names are said as PokeAPI spells them (tools/pokeapi_cache), accents and
all, whenever that folds back to what the screen shows.

The first version (Dutch only) is Carl Megens' work; this generalises it.
"""
import glob, hashlib, json, os, re, struct, sys, unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, 'tools', 'voice_cache')
CHUNK = 3_900_000          # PUT accepts files up to 4 MB
MAX_TOK = 8                # longest phrase, in words
RATE = 16000

# Lang order in i18n.h. A pack lives in /voice/<code>/ (speechLangDir()).
LANGS = ['es', 'en', 'fr', 'de', 'it', 'pt', 'nl']

# The two language-neutral tokens speechTokens() makes of "3/10" and "+5".
SYMBOLS = {
    'es': {'slash': 'de', 'plus': 'más'}, 'en': {'slash': 'of', 'plus': 'plus'},
    'fr': {'slash': 'sur', 'plus': 'plus'}, 'de': {'slash': 'von', 'plus': 'plus'},
    'it': {'slash': 'su', 'plus': 'più'}, 'pt': {'slash': 'de', 'plus': 'mais'},
    'nl': {'slash': 'van', 'plus': 'plus'},
}

# How an on-screen word is SAID, per language: abbreviations spelled out, and
# the accents the font had to drop put back. Keys are lowercase screen words.
COMMON = {'hp': 'HP', 'iv': 'IV', 'xp': 'XP', 'sd': 'SD', 'lan': 'LAN', 'ok': 'OK', 'exp': 'XP'}
RESPELL = {
    'en': {'lv': 'level', 'atk': 'attack', 'def': 'defence', 'spd': 'speed', 'wgt': 'weight',
           'rec': 'record', 'min': 'minutes', 'vol': 'volume', 'snd': 'sound', 'h': 'hours',
           'pwr': 'power', 'pokedex': 'pokédex', 'pokemon': 'pokémon'},
    'nl': {'lv': 'level', 'kra': 'kracht', 'aanv': 'aanval', 'ver': 'verdediging', 'sne': 'snelheid',
           'snlhd': 'snelheid', 'gew': 'gewicht', 'ene': 'energie', 'rec': 'record', 'min': 'minuten',
           'vol': 'volume', 'snd': 'geluid', 'h': 'uur', 'ok': 'oké', 'pokedex': 'pokédex',
           'pokemon': 'pokémon', 'nv': 'level'},
    'fr': {'niv': 'niveau', 'lv': 'niveau', 'atq': 'attaque', 'def': 'défense', 'vit': 'vitesse',
           'pds': 'poids', 'nour': 'nourriture', 'hyg': 'hygiène', 'ene': 'énergie', 'pui': 'puissance',
           'rec': 'record', 'min': 'minutes', 'vol': 'volume', 'h': 'heures',
           'pokedex': 'pokédex', 'pokemon': 'pokémon', 'abandonne': 'abandonné', 'age': 'âge',
           'annee': 'année', 'arenes': 'arènes', 'arrivee': 'arrivée', 'attrape': 'attrapé',
           'aujourd': "aujourd'hui", 'hui': '', 'blesse': 'blessé', 'boite': 'boîte', 'boites': 'boîtes',
           'brulure': 'brûlure', 'capacite': 'capacité', 'capacites': 'capacités', 'creer': 'créer',
           'defense': 'défense', 'differente': 'différente', 'echange': 'échange', 'echanger': 'échanger',
           'eclore': 'éclore', 'entrainement': 'entraînement', 'entrainer': 'entraîner', 'epuise': 'épuisé',
           'equipe': 'équipe', 'evolue': 'évolue', 'evoluer': 'évoluer', 'evolution': 'évolution',
           'foret': 'forêt', 'gagne': 'gagné', 'gele': 'gelé', 'legendaire': 'légendaire', 'libere': 'libéré',
           'lumiere': 'lumière', 'medaille': 'médaille', 'medailles': 'médailles', 'negligences': 'négligences',
           'oeuf': 'œuf', 'ocean': 'océan', 'pret': 'prêt', 'progres': 'progrès', 'rate': 'raté',
           'recois': 'reçois', 'region': 'région', 'reglages': 'réglages', 'regler': 'régler',
           'relacher': 'relâcher', 'reponse': 'réponse', 'serie': 'série', 'tres': 'très',
           'verrouille': 'verrouillé', 'parametres': 'paramètres', 'a': 'à', 'deja': 'déjà',
           'ete': 'été', 'etait': 'était', 'deplace': 'déplace', 'electrique': 'électrique'},
    'es': {'nv': 'nivel', 'niv': 'nivel', 'lv': 'nivel', 'fue': 'fuerza', 'def': 'defensa',
           'vel': 'velocidad', 'pes': 'peso', 'com': 'comida', 'fel': 'felicidad', 'ene': 'energía',
           'lim': 'limpieza', 'vin': 'vínculo', 'pot': 'potencia', 'rec': 'récord', 'min': 'minutos',
           'vol': 'volumen', 'h': 'horas', 'pokedex': 'pokédex', 'pokemon': 'pokémon',
           'adios': 'adiós', 'ano': 'año', 'aqui': 'aquí', 'bano': 'baño', 'cumpleanos': 'cumpleaños',
           'critico': 'crítico', 'dia': 'día', 'dias': 'días', 'dificil': 'difícil', 'evolucion': 'evolución',
           'region': 'región', 'manana': 'mañana', 'montana': 'montaña', 'nino': 'niño', 'ningun': 'ningún',
           'rapido': 'rápido', 'paralisis': 'parálisis', 'oceano': 'océano', 'volcan': 'volcán',
           'version': 'versión', 'cargalos': 'cárgalos', 'tocalo': 'tócalo', 'oirlo': 'oírlo',
           'confusion': 'confusión', 'mas': 'más', 'unira': 'unirá', 'vacio': 'vacío', 'esta': 'está',
           'record': 'récord', 'opciones': 'opciones', 'debilito': 'debilitó', 'escapo': 'escapó',
           'quien': 'quién', 'que': 'qué', 'tu': 'tú', 'si': 'sí', 'despues': 'después',
           'pegatina': 'pegatina', 'tambien': 'también', 'dijo': 'dijo'},
    'de': {'lv': 'Level', 'ang': 'Angriff', 'ver': 'Verteidigung', 'ini': 'Initiative', 'gew': 'Gewicht',
           'ene': 'Energie', 'hyg': 'Hygiene', 'std': 'Stunden', 'stk': 'Stärke', 'rek': 'Rekord',
           'bnd': 'Bindung', 'min': 'Minuten', 'vol': 'Lautstärke', 'h': 'Stunden',
           'pokedex': 'Pokédex', 'pokemon': 'Pokémon', 'aendern': 'ändern', 'anhoeren': 'anhören',
           'ausbrueten': 'ausbrüten', 'beruehre': 'berühre', 'erschoepft': 'erschöpft', 'fuehlt': 'fühlt',
           'fuer': 'für', 'fuettern': 'füttern', 'gefaellt': 'gefällt', 'gehoert': 'gehört',
           'gekaempft': 'gekämpft', 'gewaehlt': 'gewählt', 'gruene': 'grüne', 'hoehle': 'Höhle',
           'kaempfen': 'kämpfen', 'lautstaerke': 'Lautstärke', 'legendaeres': 'legendäres',
           'naechste': 'nächste', 'schliessen': 'schließen', 'schlaeft': 'schläft', 'schoen': 'schön',
           'spaeter': 'später', 'staerke': 'Stärke', 'tschuess': 'tschüss', 'waehle': 'wähle',
           'waehlen': 'wählen', 'zurueck': 'zurück', 'tippen': 'tippen', 'beere': 'Beere',
           'gruen': 'grün', 'fuehlen': 'fühlen', 'koennen': 'können', 'muede': 'müde', 'uebung': 'Übung'},
    'it': {'liv': 'livello', 'lv': 'livello', 'att': 'attacco', 'dif': 'difesa', 'vel': 'velocità',
           'pes': 'peso', 'cib': 'cibo', 'gio': 'gioia', 'ene': 'energia', 'igi': 'igiene', 'leg': 'legame',
           'pot': 'potenza', 'aud': 'audio', 'rec': 'record', 'min': 'minuti', 'vol': 'volume', 'h': 'ore',
           'pokedex': 'pokédex', 'pokemon': 'pokémon', 'eta': 'età', 'felicita': 'felicità',
           'velocita': 'velocità', 'puo': 'può', 'cio': 'ciò', 'entrera': 'entrerà', 'perche': 'perché',
           'piu': 'più', 'gia': 'già', 'e': 'è'},
    'pt': {'nv': 'nível', 'niv': 'nível', 'lv': 'nível', 'atq': 'ataque', 'def': 'defesa',
           'vel': 'velocidade', 'pes': 'peso', 'hig': 'higiene', 'ene': 'energia', 'pot': 'potência',
           'seq': 'sequência', 'ale': 'alegria', 'rec': 'recorde', 'min': 'minutos', 'vol': 'volume',
           'h': 'horas', 'pokedex': 'pokédex', 'pokemon': 'pokémon', 'nao': 'não', 'amanha': 'amanhã',
           'aniversario': 'aniversário', 'regiao': 'região', 'versao': 'versão', 'evolucao': 'evolução',
           'confusao': 'confusão', 'crianca': 'criança', 'opcoes': 'opções', 'forca': 'força',
           'espaco': 'espaço', 'parabens': 'parabéns', 'ginasios': 'ginásios', 'lendario': 'lendário',
           'critico': 'crítico', 'dificil': 'difícil', 'rapido': 'rápido', 'proximo': 'próximo',
           'otimo': 'ótimo', 'ninguem': 'ninguém', 'laco': 'laço', 'quao': 'quão', 'esta': 'está',
           'entrara': 'entrará', 'voce': 'você', 'tambem': 'também', 'ate': 'até', 'ja': 'já'},
}


def cls(c):
    if c.isascii() and c.isalpha():
        return 1
    if c.isascii() and c.isdigit():
        return 2
    return 0


def tokens(text):
    """Mirror of speechTokens() in speech.cpp."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = cls(text[i])
        if not c:
            num_before = i > 0 and cls(text[i - 1]) == 2
            num_after = i + 1 < n and cls(text[i + 1]) == 2
            if text[i] == '/' and num_before and num_after:
                out.append('slash')
            elif text[i] == '+' and num_after:
                out.append('plus')
            i += 1
            continue
        j = i
        while j < n and cls(text[j]) == c:
            j += 1
        w = text[i:j]
        if c == 2:
            w = w.lstrip('0') or '0'
        out.append(w.lower()[:31])
        i = j
    return out[:24]


def fnv(s):
    h = 2166136261
    for b in s.encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def make_spoken(lang):
    words = dict(COMMON)
    words.update(RESPELL[lang])
    words.update(SYMBOLS[lang])

    def spoken(piece):
        """What piper is asked to say for a piece of on-screen text."""
        s = re.sub(r'(?<=\d)/(?=\d)', ' slash ', piece)
        s = re.sub(r'\+(?=\d)', ' plus ', s)
        s = re.sub(r'[A-Za-z]+', lambda m: words.get(m.group(0).lower(), m.group(0).lower()), s)
        s = re.sub(r'[_*>=<+\-/]+', ' ', s)
        return re.sub(r'\s+', ' ', s).strip()
    return spoken, words


def lits(src):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', src)


def ui_rows(lang):
    c = open(os.path.join(ROOT, 'i18n.cpp'), encoding='utf-8').read()
    code = lang.upper()
    a = c.index('// ---------------- %s ' % code)
    nxt = c.find('// ---------------- ', a + 10)
    end = c.index('};', a)
    ui = lits(c[a:nxt if 0 < nxt < end else end])
    rest = []
    k = LANGS.index(lang)
    for name in ('MED_NAME', 'MED_LBL', 'MED_DSC', 'STK_NAME'):
        s = c.index(name + '[LANG_COUNT]')
        block = c[s:c.index('};', s)]
        rows = re.findall(r'\{([^{}]*)\}', block)
        rest += lits(rows[k])
    return ui + rest


def code_literals():
    """Short UI words written straight into the code: region names, OK, BALL, trainer names..."""
    found = []
    skip = {'dex_names.h', 'move_names.h', 'backs.h', 'badges.h', 'music.h', 'avatars.h', 'i18n.cpp'}
    for f in glob.glob(os.path.join(ROOT, '*.ino')) + glob.glob(os.path.join(ROOT, '*.cpp')) + \
            glob.glob(os.path.join(ROOT, '*.h')):
        if os.path.basename(f) in skip:
            continue
        for line in open(f, encoding='utf-8', errors='replace'):
            if 'Serial' in line or 'printf(' in line and 'snprintf' not in line:
                continue
            for s in lits(line):
                if '%' in s or '/' in s or '\\' in s or not (2 <= len(s) <= 30):
                    continue
                if not re.search(r'[A-Z]', s) or not re.search(r'[A-Za-z]{2}', s):
                    continue
                found.append(s)
    return found


def table_names(path, table):
    c = open(os.path.join(ROOT, path), encoding='utf-8').read()
    s = c.index(table)
    return re.findall(r'\{\s*"([^"]+)"', c[s:c.index('};', s)])


def screen_names(lang):
    """Species and move names as the screen shows them in this language."""
    dex = table_names('dex.h', 'DEX_TBL[')
    mov = table_names('moves.h', 'MOVE_TBL[')
    out = []
    for path, base, prefix in (('dex_names.h', dex, 'DEX_NAMES_'), ('move_names.h', mov, 'MOVE_NAMES_')):
        c = open(os.path.join(ROOT, path), encoding='utf-8').read()
        over = {}
        m = re.search(r'%s%s\[[^\]]*\] = \{(.*?)\};' % (prefix, lang.upper()), c, re.S)
        if m:
            for i, line in enumerate(re.findall(r'^\s*(nullptr|"[^"]*"),', m.group(1), re.M)):
                if line != 'nullptr':
                    over[i] = line.strip('"')
        # the C table is indexed from 0; DEX_TBL[0] / MOVE_TBL[0] are fillers
        for i, n in enumerate(base):
            out.append(over.get(i, n))
    return [n for n in out if n not in ('?', '-')]


def fold(s):
    s = unicodedata.normalize('NFKD', s)
    s = ''.join(c for c in s if not unicodedata.combining(c))
    return re.sub(r'[^A-Z0-9]', '', s.upper())


def pokeapi_spellings(lang):
    """folded screen name -> PokeAPI's own spelling, for the names the voice says."""
    api = {'es': 'es', 'fr': 'fr', 'de': 'de', 'it': 'it', 'en': 'en', 'pt': 'en', 'nl': 'en'}[lang]
    out = {}
    for f in glob.glob(os.path.join(ROOT, 'tools', 'pokeapi_cache', '*.json')):
        try:
            d = json.load(open(f, encoding='utf-8'))
        except Exception:
            continue
        for n in d.get('names', []):
            if n['language']['name'] == api:
                out.setdefault(fold(n['name']), n['name'])
    return out


def collect(lang):
    spoken, words = make_spoken(lang)
    accented = pokeapi_spellings(lang)
    phrases = {}   # key -> spoken text

    def add(piece, say=None):
        t = tokens(piece)
        if not t or len(t) > MAX_TOK:
            return
        k = ' '.join(t)
        if k not in phrases:
            phrases[k] = say if say is not None else spoken(piece)

    for s in ui_rows(lang):
        for piece in re.split(r'%[-0-9.]*l?[sudxc]', s):
            add(piece)
    for s in code_literals():
        add(s)
    for n in screen_names(lang):
        add(n, accented.get(fold(n), n.title()))
    for t in ('NORMAL', 'FIRE', 'WATER', 'ELECTRIC', 'GRASS', 'ICE', 'FIGHTING', 'POISON', 'GROUND',
              'FLYING', 'PSYCHIC', 'BUG', 'ROCK', 'GHOST', 'DRAGON', 'DARK', 'STEEL', 'FAIRY'):
        add(t, t.title())      # the type chips are English in every language
    for n in list(range(0, 201)) + list(range(300, 1001, 100)):
        add(str(n), str(n))
    # single words, for text that only partly matches a phrase
    for k in list(phrases):
        for w in k.split():
            if not w.isdigit() and w not in phrases:
                phrases[w] = words.get(w, w)
    return {k: v for k, v in phrases.items() if v}


def resample(x, src, dst):
    import numpy as np
    n = int(round(len(x) * dst / src))
    X = np.fft.rfft(x)
    keep = n // 2 + 1
    Y = np.zeros(keep, dtype=complex)
    m = min(keep, len(X))
    Y[:m] = X[:m]
    return np.fft.irfft(Y, n) * (n / len(x))


def to_clip(pcm16, rate):
    import numpy as np
    x = pcm16.astype(np.float64) / 32768.0
    if rate != RATE:
        x = resample(x, rate, RATE)
    peak = np.max(np.abs(x)) if len(x) else 0
    if peak < 1e-4:
        return b''
    on = np.where(np.abs(x) > peak * 0.02)[0]
    pad = int(RATE * 0.015)
    x = x[max(0, on[0] - pad):on[-1] + pad]
    x = x / peak * 0.95
    return np.clip(np.round(x * 127), -127, 127).astype(np.int8).tobytes()


def synth_all(phrases, model, lang):
    import numpy as np
    from piper import PiperVoice
    try:
        from piper import SynthesisConfig
        cfg = SynthesisConfig(length_scale=1.05)
    except ImportError:
        cfg = None
    voice = PiperVoice.load(model)
    cache = os.path.join(CACHE, lang)
    os.makedirs(cache, exist_ok=True)
    clips, texts = {}, sorted(set(phrases.values()))
    for i, t in enumerate(texts):
        cp = os.path.join(cache, hashlib.sha1(t.encode()).hexdigest()[:16] + '.i8')
        if os.path.exists(cp):
            clips[t] = open(cp, 'rb').read()
            continue
        chunks = list(voice.synthesize(t, syn_config=cfg) if cfg else voice.synthesize(t))
        rate = chunks[0].sample_rate if chunks else 22050
        pcm = np.concatenate([np.frombuffer(c.audio_int16_bytes, dtype=np.int16) for c in chunks]) \
            if chunks else np.zeros(0, np.int16)
        clips[t] = to_clip(pcm, rate)
        open(cp, 'wb').write(clips[t])
        if i % 250 == 0:
            print(f'  {lang} {i}/{len(texts)} {t!r}', flush=True)
    return clips


def write(phrases, clips, lang):
    out = os.path.join(ROOT, 'tools', 'sdcard', 'voice', lang)
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, '*')):
        os.remove(f)
    entries, chunks, cur, where = [], [], bytearray(), {}
    for k in sorted(phrases):
        data = clips[phrases[k]]
        if not data:
            continue
        if data not in where:      # identical clips stored once
            if len(cur) + len(data) > CHUNK:
                chunks.append(bytes(cur)); cur = bytearray()
            where[data] = (len(chunks), len(cur))
            cur += data
        ch, off = where[data]
        entries.append((fnv(k), ch, off, len(data), k))
    chunks.append(bytes(cur))
    entries.sort()
    for a, b in zip(entries, entries[1:]):
        assert a[0] != b[0], f'hash collision: {a[4]!r} / {b[4]!r}'
    files = {}
    idx = b'VIX1' + struct.pack('<IB3x', len(entries), MAX_TOK)
    idx += b''.join(struct.pack('<IHHII', h, ch, 0, off, ln) for h, ch, off, ln, _ in entries)
    files[f'voice/{lang}/index.bin'] = idx
    for i, c in enumerate(chunks):
        files[f'voice/{lang}/v{i:02d}.bin'] = c
    for name, data in files.items():
        open(os.path.join(out, os.path.basename(name)), 'wb').write(data)
    # TPAK bundle, same format as the sprite packs (web/index.html parsePak)
    pak = b'TPAK' + struct.pack('<H', len(files))
    for name, data in files.items():
        pak += bytes([len(name)]) + name.encode() + struct.pack('<I', len(data))
    pak += b''.join(files.values())
    open(os.path.join(ROOT, 'web', f'voice-{lang}.pak'), 'wb').write(pak)
    total = sum(len(c) for c in chunks)
    print(f'{lang}: {len(entries)} phrases, {len(where)} clips, {total / 1e6:.1f} MB in {len(chunks)} chunks')


if __name__ == '__main__':
    args = sys.argv[1:]
    listing = args and args[0] == '--list'
    if listing:
        args = args[1:]
    if not args or args[0] not in LANGS or (not listing and len(args) < 2):
        sys.exit(__doc__)
    lang = args[0]
    ph = collect(lang)
    print(f'{lang}: {len(ph)} phrases, {len(set(ph.values()))} texts to synthesise', file=sys.stderr)
    if listing:
        for k, v in sorted(ph.items()):
            print(f'{k}\t{v}')
        sys.exit()
    write(ph, synth_all(ph, args[1], lang), lang)
