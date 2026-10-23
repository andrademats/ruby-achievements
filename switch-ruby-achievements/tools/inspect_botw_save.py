#!/usr/bin/env python3
"""Inspeciona flags de GameData num save do Zelda (game_data.sav) no PC.

Os saves de GameData guardam cada flag como [hash do nome, 4 bytes][valor, 4 bytes],
little-endian. O BOTW usa CRC32 do nome; o TotK, MurmurHash3. Este script usa a
mesma regra do sysmodule (tipo save_flag_hash_search): o hash é procurado em
posições múltiplas de 4 e o valor fica 4 bytes depois do início do hash. Então o
que aparece aqui como ativo é o que a conquista vai ver no console.

Uso:
  python inspect_botw_save.py game_data.sav
      Verifica a lista de flags embutida (exemplos) e mostra as ativas.
  python inspect_botw_save.py game_data.sav --flags flags.txt
      Verifica os nomes do arquivo (um por linha; linhas com # são ignoradas).
  python inspect_botw_save.py antes.sav --diff depois.sav [--flags flags.txt]
      Mostra o que mudou entre dois saves (ex.: antes e depois de pegar um
      item). Acha a flag mesmo sem saber o nome: o hash aparece ao lado.
  --hash murmur3      para o TotK (padrão: crc32, do BOTW)
  --json              imprime a definição de conquista pronta para cada flag ativa
  --all               mostra também as flags com valor 0

Só lê os arquivos; nada é alterado.
"""
import argparse
import json
import struct
import sys
import zlib

# Exemplos para começar. Os nomes reais variam e nem todos estes existem no
# jogo: confirme com uma lista de flags de GameData da comunidade (passe-a com
# --flags) ou descubra com --diff. Nome ausente aparece como "não encontrada".
DEFAULT_FLAGS = [
    "IsGet_Paraglider",
    "IsMasterSwordGet",
    "Defeated_Ganon",
]


def murmur3_32(data, seed=0):
    c1, c2, mask = 0xCC9E2D51, 0x1B873593, 0xFFFFFFFF
    rotl = lambda x, r: ((x << r) | (x >> (32 - r))) & mask
    h, n = seed, len(data)
    for i in range(0, n // 4 * 4, 4):
        k = int.from_bytes(data[i:i + 4], "little")
        k = rotl((k * c1) & mask, 15)
        h ^= (k * c2) & mask
        h = (rotl(h, 13) * 5 + 0xE6546B64) & mask
    tail = data[n // 4 * 4:]
    if tail:
        k = int.from_bytes(tail, "little")
        h ^= (rotl((k * c1) & mask, 15) * c2) & mask
    h ^= n
    h ^= h >> 16
    h = (h * 0x85EBCA6B) & mask
    h ^= h >> 13
    h = (h * 0xC2B2AE35) & mask
    return h ^ (h >> 16)


def flag_hash(name, kind):
    data = name.encode("utf-8")
    return zlib.crc32(data) & 0xFFFFFFFF if kind == "crc32" else murmur3_32(data)


def u32_at(buf, pos):
    return struct.unpack_from("<I", buf, pos)[0] if 0 <= pos <= len(buf) - 4 else None


def first_positions(buf):
    """Primeira posição múltipla de 4 de cada valor de 4 bytes (como o sysmodule)."""
    first = {}
    for pos in range(0, len(buf) - 3, 4):
        first.setdefault(struct.unpack_from("<I", buf, pos)[0], pos)
    return first


def read_names(path):
    names = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                names.append(line)
    return names


def achievement_json(name, kind, value):
    return json.dumps({
        "id": name.lower(), "title": name, "description": "", "type": "save_flag_hash_search", "save_file": "game_data.sav",
        "flag_name": name, "hash_type": kind, "operator": "==", "expected_value_int": value,
    }, ensure_ascii=False)


def inspect(buf, names, kind, show_all, as_json):
    first = first_positions(buf)
    active = missing = 0
    print(f"{'flag':40} {'hash':>10} {'offset':>10} {'valor':>10}")
    for name in names:
        h = flag_hash(name, kind)
        pos = first.get(h)
        if pos is None:
            missing += 1
            print(f"{name:40} {h:#010x} {'-':>10} {'não encontrada':>10}")
            continue
        value = u32_at(buf, pos + 4)
        if value is None:
            print(f"{name:40} {h:#010x} {pos:#010x} {'fim do arquivo':>10}")
            continue
        if value != 0:
            active += 1
        if value != 0 or show_all:
            mark = "ATIVA" if value == 1 else ""
            print(f"{name:40} {h:#010x} {pos:#010x} {value:>10} {mark}")
            if as_json and value != 0:
                print("    " + achievement_json(name, kind, value))
    print(f"\n{len(names)} flags verificadas: {active} com valor diferente de 0, {missing} não encontradas "
          f"(hash {kind}).")
    if missing == len(names) and names:
        print("Nenhuma encontrada: confira o --hash (BOTW = crc32, TotK = murmur3) e os nomes.")


def diff(a, b, names, kind):
    if len(a) != len(b):
        print(f"Aviso: tamanhos diferentes ({len(a)} e {len(b)} bytes); comparando o trecho comum.")
    by_hash = {flag_hash(n, kind): n for n in names}
    changes = 0
    print(f"{'offset':>10} {'hash antes do valor':>20} {'antes':>10} {'depois':>10}  flag")
    for pos in range(4, min(len(a), len(b)) - 3, 4):
        va, vb = struct.unpack_from("<I", a, pos)[0], struct.unpack_from("<I", b, pos)[0]
        if va == vb:
            continue
        h = struct.unpack_from("<I", b, pos - 4)[0]
        changes += 1
        if changes <= 200:
            print(f"{pos:#010x} {h:#20x} {va:>10} {vb:>10}  {by_hash.get(h, '?')}")
    if changes > 200:
        print(f"... mais {changes - 200} mudanças")
    print(f"\n{changes} valores mudaram. Numa entrada [hash][valor], o hash é o número à esquerda; "
          "para criar a conquista sem saber o nome, ache-o numa lista de flags pelo hash.")


def main():
    ap = argparse.ArgumentParser(description="Inspeciona flags de GameData num save do Zelda (só leitura).")
    ap.add_argument("save")
    ap.add_argument("--flags", help="arquivo com nomes de flags, um por linha")
    ap.add_argument("--diff", metavar="OUTRO_SAVE", help="compara com outro save")
    ap.add_argument("--hash", choices=["crc32", "murmur3"], default="crc32")
    ap.add_argument("--all", action="store_true", help="mostra também flags com valor 0")
    ap.add_argument("--json", action="store_true", help="imprime a definição de conquista das flags ativas")
    args = ap.parse_args()

    with open(args.save, "rb") as f:
        buf = f.read()
    names = read_names(args.flags) if args.flags else DEFAULT_FLAGS
    if args.diff:
        with open(args.diff, "rb") as f:
            diff(buf, f.read(), names, args.hash)
    else:
        if not args.flags:
            print("Usando a lista de exemplo (nomes a confirmar). Para a lista completa: --flags arquivo.txt\n")
        inspect(buf, names, args.hash, args.all, args.json)


if __name__ == "__main__":
    sys.exit(main())
