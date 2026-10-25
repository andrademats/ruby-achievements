"""Acrescenta (ou atualiza) um pacote .rubydb no catálogo da comunidade (index.json).

O app (app_nro/source/community.cpp) lê o catálogo de
https://raw.githubusercontent.com/andrademats/ruby-achievements-packs/main/index.json e confere
o SHA-256 de cada pacote baixado. Esta ferramenta tira do próprio .rubydb o título, o autor, a
descrição, a versão, o Title ID, os build ids e o número de conquistas, calcula o SHA-256 e grava
a entrada (a de mesmo pack_id é substituída). O .rubydb é conferido antes (check_rubydb).

Exemplo (jogo do Switch):
  python -I tools/rubydb_index.py --index index.json --pack 01007EF00011E000.desafios.rubydb \\
      --url https://raw.githubusercontent.com/andrademats/ruby-achievements-packs/main/packs/01007EF00011E000.desafios.rubydb \\
      --version-min 1.6.0
Ports e ROMs: --port-slug soh_oot, ou --platform GBA --rom-hash <MD5 ou SHA-1 da ROM>.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import json_to_rubydb as tool  # noqa: E402

PACK_ID_RE = re.compile(r"^[a-z0-9_-]{1,48}$")
HEX_RE = re.compile(r"^[0-9A-Fa-f]+$")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def pack_id_from_name(path):
    """Variante do nome "<TID>.<variante>.rubydb" ou o nome sem extensão, em minúsculas."""
    stem = os.path.basename(path)[:-len(".rubydb")] if path.endswith(".rubydb") else os.path.basename(path)
    parts = stem.split(".", 1)
    return (parts[1] if len(parts) == 2 and len(parts[0]) == 16 else stem).lower()


def make_entry(pack_path, url, pack_id=None, version_min="", port_slug="", platform="", rom_hash=""):
    tool.check_rubydb(pack_path)
    db = sqlite3.connect("file:%s?mode=ro" % pack_path.replace("\\", "/"), uri=True)
    try:
        title, author, description, ttype, tid, builds, version = db.execute(
            "SELECT title, author, COALESCE(description,''), target_type, COALESCE(target_title_id,''), "
            "COALESCE(supported_build_ids,''), version FROM pack_info").fetchone()
        total = db.execute("SELECT count(*) FROM achievements").fetchone()[0]
    finally:
        db.close()
    pack_id = (pack_id or pack_id_from_name(pack_path)).lower()
    if not PACK_ID_RE.match(pack_id):
        raise tool.PackError("pack_id %r: só a-z, 0-9, _ e -, até 48 caracteres" % pack_id)
    if not url.startswith("https://"):
        raise tool.PackError("a URL precisa ser https://")
    target = {}
    if port_slug:
        target = {"type": "port", "port_slug": port_slug.lower()}
    elif platform or rom_hash:
        if not platform or len(rom_hash) not in (32, 40) or not HEX_RE.match(rom_hash):
            raise tool.PackError("ROM: --platform e --rom-hash (MD5 de 32 ou SHA-1 de 40 dígitos hex)")
        target = {"type": "rom", "platform_code": platform.upper(), "rom_hash": rom_hash.lower()}
    else:
        if ttype != "native" or len(tid) != 16 or not HEX_RE.match(tid):
            raise tool.PackError("pacote sem target_title_id de jogo do Switch: use --port-slug ou --platform/--rom-hash")
        target = {"type": "native", "title_id": tid.upper(),
                  "supported_build_ids": [b.strip().upper() for b in builds.split(",") if b.strip()]}
    if version_min and target["type"] != "rom":
        target["game_version_min"] = version_min
    return {"pack_id": pack_id, "title": title, "author": author, "description": description,
            "version": version, "total_achievements": total, "target": target,
            "download_url": url, "sha256": sha256(pack_path)}


def add_to_index(index_path, entry, now=None):
    index = {"updated_at": "", "packs": []}
    if os.path.exists(index_path):
        with open(index_path, encoding="utf-8") as f:
            index = json.load(f)
    packs = [p for p in index.get("packs", []) if p.get("pack_id") != entry["pack_id"]]
    packs.append(entry)
    packs.sort(key=lambda p: (p.get("target", {}).get("title_id", ""), p.get("pack_id", "")))
    now = now or datetime.datetime.now(datetime.timezone.utc)
    index = {"updated_at": now.strftime("%Y-%m-%dT%H:%M:%SZ"), "packs": packs}
    tmp = index_path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, index_path)
    return index


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index", required=True, help="index.json (criado se não existir)")
    ap.add_argument("--pack", required=True, help="arquivo .rubydb")
    ap.add_argument("--url", required=True, help="endereço https:// do .rubydb no repositório")
    ap.add_argument("--pack-id", help="padrão: a variante do nome do arquivo")
    ap.add_argument("--version-min", default="", help="versão mínima do jogo (ex.: 1.6.0)")
    ap.add_argument("--port-slug", default="")
    ap.add_argument("--platform", default="")
    ap.add_argument("--rom-hash", default="")
    a = ap.parse_args()
    try:
        entry = make_entry(a.pack, a.url, a.pack_id, a.version_min, a.port_slug, a.platform, a.rom_hash)
        index = add_to_index(a.index, entry)
    except (tool.PackError, sqlite3.Error, OSError, ValueError) as e:
        print("erro: %s" % e, file=sys.stderr)
        return 1
    print("%s: %s (%d conquistas, sha256 %s...); %d pacotes no catálogo" % (
        a.index, entry["pack_id"], entry["total_achievements"], entry["sha256"][:12], len(index["packs"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
