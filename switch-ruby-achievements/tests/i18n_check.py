"""Confere a tabela de tradução do app (i18n_en.inc) contra o código.

- toda chave usada em tr("...") / trf("...") existe na tabela;
- trf: a tradução tem os mesmos especificadores %d/%s/... na mesma ordem;
- sem chave repetida e sem entrada sem uso;
- lista literais com cara de texto de tela fora de tr() (para revisão).
Uso: python -I i18n_check.py <app_nro/source> [--list-untranslated]
"""
import glob
import os
import re
import sys

SRC = sys.argv[1]
LIST = '--list-untranslated' in sys.argv


def read_literal(s, i):
    """Lê um literal C começando em s[i] == '"'. Retorna (texto, índice após)."""
    assert s[i] == '"'
    i += 1
    out = []
    while True:
        c = s[i]
        if c == '"':
            return ''.join(out), i + 1
        if c == '\\':
            n = s[i + 1]
            if n == 'n': out.append('\n'); i += 2
            elif n == 't': out.append('\t'); i += 2
            elif n == 'r': out.append('\r'); i += 2
            elif n == '0': out.append('\0'); i += 2
            elif n == '"': out.append('"'); i += 2
            elif n == '\\': out.append('\\'); i += 2
            elif n == "'": out.append("'"); i += 2
            elif n == 'x':
                m = re.match(r'[0-9A-Fa-f]{1,2}', s[i + 2:])
                out.append(chr(int(m.group(0), 16))); i += 2 + len(m.group(0))
            elif n == 'u':
                out.append(chr(int(s[i + 2:i + 6], 16))); i += 6
            else:
                raise ValueError('escape desconhecido: \\' + n)
        else:
            out.append(c); i += 1


def read_concat(s, i):
    """Literais adjacentes ("a" "b"), com espaços e quebras entre eles."""
    parts = []
    while True:
        while i < len(s) and s[i] in ' \t\r\n':
            i += 1
        if i < len(s) and s[i] == '"':
            t, i = read_literal(s, i)
            parts.append(t)
        else:
            break
    return ''.join(parts), i, len(parts) > 0


def strip_comments(s):
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith('//', i):
            j = s.find('\n', i)
            i = n if j < 0 else j
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif s[i] == '"':
            _, j = read_literal(s, i)
            out.append(s[i:j]); i = j
        elif s[i] == "'":
            j = i + 1
            while s[j] != "'":
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1]); i = j + 1
        else:
            out.append(s[i]); i += 1
    return ''.join(out)


SPEC = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcsp])')


def specs(t):
    return [m for m in SPEC.findall(t) if m != '%%']


# --- tabela ---
inc = open(os.path.join(SRC, 'i18n_en.inc'), encoding='utf-8').read()
inc = strip_comments(inc)
table = {}
problems = []
for m in re.finditer(r'\{', inc):
    i = m.end()
    pt, i, ok = read_concat(inc, i)
    if not ok:
        continue
    while inc[i] in ' \t\r\n':
        i += 1
    assert inc[i] == ',', inc[m.start():m.start() + 80]
    en, i, ok2 = read_concat(inc, i + 1)
    assert ok2
    if pt in table:
        problems.append('chave repetida: %r' % pt)
    table[pt] = en

# --- código ---
used = {}
untranslated = []
files = sorted(glob.glob(os.path.join(SRC, '*.cpp')))
for f in files:
    raw = open(f, encoding='utf-8').read()
    s = strip_comments(raw)
    for m in re.finditer(r'\b(trf?)\(', s):
        key, _, ok = read_concat(s, m.end())
        if ok:
            used.setdefault(key, set()).add((os.path.basename(f), m.group(1)))
    # Literais fora de tr(): só para revisão manual.
    i = 0
    while i < len(s):
        if s[i] == '"':
            start = i
            t, i = read_literal(s, i)
            before = s[max(0, start - 40):start]
            if re.search(r'\btrf?\(\s*(?:"[^"]*"\s*)*$', before):
                continue
            if re.search(r'[a-zà-ú]{3,} [a-zà-ú]{2,}', t) or re.search(r'[à-úÀ-Ú]', t):
                untranslated.append((os.path.basename(f), t))
        else:
            i += 1

# Etapas da instalação do forwarder: o app mostra tr(what) com o texto de step(n, "...").
owo = os.path.join(SRC, 'forwarder', 'owo.cpp')
if os.path.exists(owo):
    t = strip_comments(open(owo, encoding='utf-8').read())
    for m in re.finditer(r'\bstep\(\s*\d+\s*,\s*', t):
        key, _, ok = read_concat(t, m.end())
        if ok:
            used.setdefault(key, set()).add(('forwarder/owo.cpp', 'tr'))

missing = sorted(k for k in used if k not in table)
unused = sorted(k for k in table if k not in used)
for k in missing:
    problems.append('sem tradução (%s): %r' % (', '.join(sorted(a for a, _ in used[k])), k))
for k, en in table.items():
    if any(kind == 'trf' for _, kind in used.get(k, ())) and specs(k) != specs(en):
        problems.append('especificadores diferentes: %r -> %r' % (k, en))
    if not en.strip() and k.strip():
        problems.append('tradução vazia: %r' % k)

print('chaves usadas no código: %d; entradas na tabela: %d' % (len(used), len(table)))
for p in problems:
    print('FALHA', p)
for k in unused:
    print('aviso: entrada sem uso: %r' % k)
if LIST:
    for f, t in untranslated:
        print('fora de tr() %s: %r' % (f, t))
print('TUDO OK (0 falhas)' if not problems else 'FALHOU (%d falhas)' % len(problems))
sys.exit(1 if problems else 0)
