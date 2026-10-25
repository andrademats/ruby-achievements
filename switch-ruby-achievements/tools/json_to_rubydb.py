#!/usr/bin/env python3
"""Converte um pacote de conquistas .json (formato antigo) para .rubydb (SQLite).

O .rubydb é o único formato que o app e o sysmodule leem: um arquivo por pacote, com as
conquistas e os ícones (PNG em BLOB). O pacote não tem som: o do pop-up é o escolhido no
app. Esquema em ../rubydb_schema.sql.

Uso:
  python tools/json_to_rubydb.py --input 01007EF00011E000.aventureiro.json \
      --output 01007EF00011E000.aventureiro.rubydb [--assets PASTA]
      [--author NOME] [--version 1.0.0] [--description TEXTO] [--target-type native|port|emulator]

Ícones: o campo "icon" (e o opcional "icon_locked") de cada conquista é um PNG relativo a
--assets (padrão: a pasta do .json), como "icons/<Title ID>/<id>.png" no SD antigo.
Campos de topo só do Studio (studio_chains, pack_variant) e "points" ficam de fora.

O AchieveSet Studio importa este módulo (write_rubydb) para exportar direto em .rubydb.
"""
import argparse
import json
import os
import re
import sqlite3
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # switch-ruby-achievements/
SCHEMA_PATH = os.path.join(ROOT, "rubydb_schema.sql")
FORMAT_VERSION = 1

MAX_TEXT = 64 * 1024        # texto das conquistas (pt_pack.cpp: kPtPackMaxText)
MAX_ICON_BYTES = 256 * 1024  # pt_pack.cpp: kPtPackMaxIcon
MAX_ICON_SIDE = 512
ID_RE = re.compile(r"^[A-Za-z0-9_.\-]{1,64}$")
TID_RE = re.compile(r"^[0-9A-Fa-f]{16}$")
HEX_RE = re.compile(r"^[0-9A-Fa-f]{1,64}$")
RARITIES = ("bronze", "silver", "gold", "ruby")
TARGET_TYPES = ("native", "port", "emulator")
# Campos de uma conquista que viram colunas; o resto é a condição (condition_json).
META_KEYS = ("id", "title", "description", "rarity", "secret", "icon", "icon_locked", "points")


class PackError(ValueError):
    pass


def check_png(data, what):
    """PNG de até MAX_ICON_BYTES e MAX_ICON_SIDE x MAX_ICON_SIDE; devolve (largura, altura)."""
    if len(data) > MAX_ICON_BYTES:
        raise PackError("%s: %d KB (máximo %d KB)" % (what, len(data) // 1024, MAX_ICON_BYTES // 1024))
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise PackError("%s: não é um PNG" % what)
    w, h = struct.unpack(">II", data[16:24])
    if not (0 < w <= MAX_ICON_SIDE and 0 < h <= MAX_ICON_SIDE):
        raise PackError("%s: %dx%d (máximo %dx%d)" % (what, w, h, MAX_ICON_SIDE, MAX_ICON_SIDE))
    return w, h


def _read(path, what):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        raise PackError("%s: %s" % (what, e.strerror or e))


def _text(v):
    return v if isinstance(v, str) else ("" if v is None else str(v))


def condition_text(a):
    """condition_json de uma conquista: os campos que não viram colunas; a Ruby fica com '{}'
    (desbloqueia sozinha depois das outras)."""
    if (_text(a.get("rarity")) or "bronze") == "ruby":
        return "{}"
    return json.dumps({k: v for k, v in a.items() if k not in META_KEYS}, ensure_ascii=False, separators=(",", ":"))


def achievement_text_bytes(a):
    """Bytes de texto de uma conquista como o sysmodule conta: título, descrição e condição
    (pt_pack.cpp, kPtPackMaxText)."""
    aid = _text(a.get("id"))
    text = (_text(a.get("title")) or aid) + _text(a.get("description")) + condition_text(a)
    return len(text.encode("utf-8"))


def text_bytes(pack):
    return sum(achievement_text_bytes(a) for a in pack.get("achievements", []) if isinstance(a, dict))


def write_rubydb(pack, out_path, assets_dir=None, icons=None, author="", version="1.0.0",
                 description="", target_type=None, pack_id=None):
    """Grava `pack` (dict no formato JSON antigo) como .rubydb em `out_path`.

    Ícones: `icons` = {id: (png_unlocked ou None, png_locked ou None)} em bytes; sem ele, os
    campos "icon"/"icon_locked" das conquistas são lidos de `assets_dir`. Valida tudo antes
    de gravar e grava num .tmp trocado no fim: um erro nunca deixa um pacote pela metade.
    Devolve um resumo (dict)."""
    if not isinstance(pack, dict) or not isinstance(pack.get("achievements"), list):
        raise PackError('o pacote não tem a lista "achievements"')
    pack_id = pack_id or os.path.basename(out_path)
    if pack_id.endswith(".rubydb"):
        pack_id = pack_id[:-7]
    title_id = _text(pack.get("title_id")).upper()
    stem_tid = pack_id.split(".", 1)[0]
    if not title_id and TID_RE.match(stem_tid):  # o nome do arquivo no SD é o Title ID
        title_id = stem_tid.upper()
    if title_id and not TID_RE.match(title_id):
        raise PackError("title_id deve ter 16 dígitos hexadecimais")
    builds = [b.strip() for b in re.split(r"[,\s]+", _text(pack.get("build_id"))) if b.strip()]
    for b in builds:
        if not HEX_RE.match(b):
            raise PackError("build_id inválido: %s (hex, até 64 dígitos)" % b)
    target_type = target_type or ("native" if title_id else "emulator")
    if target_type not in TARGET_TYPES:
        raise PackError("target_type deve ser native, port ou emulator")
    title = _text(pack.get("pack_name")) or _text(pack.get("pack_variant")) or _text(pack.get("game_name")) or pack_id

    rows, seen, text_size, rubies = [], set(), 0, 0
    for i, a in enumerate(pack["achievements"]):
        if not isinstance(a, dict):
            raise PackError("conquista %d não é um objeto" % (i + 1))
        aid = _text(a.get("id"))
        if not ID_RE.match(aid):
            raise PackError("conquista %d: id %r inválido (use letras, números, _ . -)" % (i + 1, aid))
        if aid in seen:
            raise PackError("id repetido: %s" % aid)
        seen.add(aid)
        rarity = _text(a.get("rarity")) or "bronze"
        if rarity not in RARITIES:
            raise PackError("%s: raridade %r (use bronze, silver, gold ou ruby)" % (aid, rarity))
        rubies += rarity == "ruby"
        cond_text = condition_text(a)
        if rarity != "ruby" and not a.get("type"):
            raise PackError("%s: sem condição (\"type\")" % aid)
        t, d = _text(a.get("title")) or aid, _text(a.get("description"))
        text_size += achievement_text_bytes(a)

        unlocked = locked = None
        if icons is not None:
            unlocked, locked = icons.get(aid, (None, None))
        else:
            for key in ("icon", "icon_locked"):
                rel = _text(a.get(key))
                if not rel:
                    continue
                if os.path.isabs(rel) or ".." in rel.replace("\\", "/").split("/"):
                    raise PackError("%s: caminho do ícone fora da pasta: %s" % (aid, rel))
                data = _read(os.path.join(assets_dir or ".", rel), "%s: ícone %s" % (aid, rel))
                if key == "icon":
                    unlocked = data
                else:
                    locked = data
        for data, what in ((unlocked, "ícone"), (locked, "ícone bloqueado")):
            if data is not None:
                check_png(data, "%s: %s" % (aid, what))
        rows.append((aid, i, t, d, rarity, 1 if a.get("secret") is True else 0, cond_text, unlocked, locked))

    if not any(r[4] != "ruby" for r in rows):
        raise PackError("o pacote não tem conquistas (além da Ruby)")
    if rubies > 1:
        raise PackError("o pacote tem %d conquistas Ruby (só uma vale)" % rubies)
    if text_size > MAX_TEXT:
        raise PackError("o texto das conquistas soma %d KB (máximo %d KB: memória do sysmodule)"
                        % (text_size // 1024, MAX_TEXT // 1024))

    with open(SCHEMA_PATH, encoding="utf-8") as f:
        schema = f.read()
    tmp = out_path + ".tmp"
    if os.path.exists(tmp):
        os.remove(tmp)
    db = sqlite3.connect(tmp)
    try:
        db.executescript(schema)
        db.execute("INSERT INTO pack_info VALUES (?,?,?,?,?,?,?,?)",
                   (pack_id, title, author or "desconhecido", description or None, target_type, title_id or None,
                    ",".join(builds) or None, version or "1.0.0"))
        db.executemany("INSERT INTO achievements(id,sort_order,title,description,rarity,secret,"
                       "condition_json,icon_unlocked,icon_locked) VALUES (?,?,?,?,?,?,?,?,?)", rows)
        db.commit()
        db.execute("VACUUM")
    finally:
        db.close()
    os.replace(tmp, out_path)
    return {"pack_id": pack_id, "achievements": len(rows), "icons": sum(r[7] is not None for r in rows),
            "locked_icons": sum(r[8] is not None for r in rows), "text_bytes": text_size,
            "bytes": os.path.getsize(out_path)}


def check_rubydb(path):
    """Confere um .rubydb gravado: formato, linhas e BLOBs. Devolve um resumo (dict)."""
    db = sqlite3.connect("file:%s?mode=ro" % path.replace("\\", "/"), uri=True)
    try:
        if db.execute("PRAGMA user_version").fetchone()[0] != FORMAT_VERSION:
            raise PackError("formato desconhecido (user_version)")
        if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
            raise PackError("banco corrompido")
        if db.execute("SELECT count(*) FROM pack_info").fetchone()[0] != 1:
            raise PackError("pack_info precisa de uma linha")
        out = {"achievements": 0, "icons": 0, "locked_icons": 0}
        for aid, cond, unl, lck in db.execute(
                "SELECT id, condition_json, icon_unlocked, icon_locked FROM achievements ORDER BY sort_order"):
            json.loads(cond)
            out["achievements"] += 1
            for data, key in ((unl, "icons"), (lck, "locked_icons")):
                if data is not None:
                    check_png(bytes(data), aid)
                    out[key] += 1
        return out
    finally:
        db.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", required=True, help="pacote .json")
    ap.add_argument("--output", help="arquivo .rubydb (padrão: o nome do .json com .rubydb)")
    ap.add_argument("--assets", help="pasta dos ícones (padrão: a do .json)")
    ap.add_argument("--author", default="desconhecido")
    ap.add_argument("--version", default="1.0.0")
    ap.add_argument("--description", default="")
    ap.add_argument("--target-type", choices=TARGET_TYPES)
    args = ap.parse_args()
    out = args.output or re.sub(r"\.json$", "", args.input) + ".rubydb"
    if not out.endswith(".rubydb"):
        sys.exit("a saída precisa terminar em .rubydb")
    try:
        with open(args.input, encoding="utf-8") as f:
            pack = json.load(f)
        info = write_rubydb(pack, out, assets_dir=args.assets or os.path.dirname(os.path.abspath(args.input)),
                            author=args.author, version=args.version,
                            description=args.description, target_type=args.target_type)
        check = check_rubydb(out)
    except (OSError, ValueError) as e:
        sys.exit("erro: %s" % e)
    print("%s: %d conquistas, %d ícones (%d bloqueados), texto %d bytes, arquivo %d bytes"
          % (out, info["achievements"], info["icons"], info["locked_icons"], info["text_bytes"], info["bytes"]))
    print("conferido: %d conquistas, %d ícones e %d bloqueados lidos de volta"
          % (check["achievements"], check["icons"], check["locked_icons"]))


if __name__ == "__main__":
    main()
