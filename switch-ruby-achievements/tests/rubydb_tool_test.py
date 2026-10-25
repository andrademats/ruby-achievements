"""tools/json_to_rubydb.py: conversão de um pacote JSON com ícones, BLOBs conferidos
byte a byte, a linha de comando e os pacotes recusados (nada gravado pela metade).

Uso: python -I rubydb_tool_test.py <pasta switch-ruby-achievements>. Roda na pasta atual."""
import json
import os
import sqlite3
import struct
import subprocess
import sys
import zlib

ROOT = sys.argv[1]
sys.path.insert(0, os.path.join(ROOT, "tools"))
import json_to_rubydb as tool  # noqa: E402

fails = 0


def check(name, ok):
    global fails
    print("%-66s %s" % (name, "ok" if ok else "FALHOU"))
    if not ok:
        fails += 1


def png(w, h, seed=0):
    """PNG RGBA válido de w x h."""
    row = b"\x00" + bytes((seed + x) & 0xFF for x in range(w * 4))
    raw = row * h

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def write(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def refused(pack, why, **kw):
    out = "recusado.rubydb"
    if os.path.exists(out):
        os.remove(out)
    try:
        tool.write_rubydb(pack, out, assets_dir=".", **kw)
    except tool.PackError as e:
        if why not in str(e):
            print("  motivo:", e)
        return why in str(e) and not os.path.exists(out) and not os.path.exists(out + ".tmp")
    return False


icon_a, icon_a_locked, icon_b = png(128, 128, 1), png(128, 128, 2), png(64, 64, 3)
write("icons/0100000000000ABC/a.png", icon_a)
write("icons/0100000000000ABC/a_lock.png", icon_a_locked)
write("icons/0100000000000ABC/b.png", icon_b)
pack = {
    "game_name": "Jogo de Teste", "title_id": "0100000000000abc", "build_id": "AABBCC, DDEEFF",
    "pack_name": "Principal", "studio_chains": {"bool": ["0x1"]},
    "achievements": [
        {"id": "a", "title": "A", "description": "primeira", "rarity": "gold", "secret": True,
         "points": 10, "icon": "icons/0100000000000ABC/a.png", "icon_locked": "icons/0100000000000ABC/a_lock.png",
         "type": "playtime_stat", "target_seconds": 60},
        {"id": "b", "title": "B", "icon": "icons/0100000000000ABC/b.png", "type": "count_of", "min": 1,
         "conditions": [{"type": "playtime_stat", "target_seconds": 1},
                        {"type": "ram_pointer_chain", "pointer_offsets": ["0x10", "0x8"], "data_type": "s32",
                         "operator": ">=", "expected_value_int": 5}]},
        {"id": "plat", "title": "Platina", "rarity": "ruby", "type": "playtime_stat", "target_seconds": 1},
    ]}
with open("0100000000000ABC.json", "w", encoding="utf-8") as f:
    json.dump(pack, f, ensure_ascii=False)

# --- linha de comando (o caminho que o usuário roda)
cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "json_to_rubydb.py"), "--input", "0100000000000ABC.json",
       "--output", "0100000000000ABC.rubydb", "--author", "Teste", "--version", "2.1.0"]
r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
check("linha de comando: conversão ok", r.returncode == 0 and os.path.exists("0100000000000ABC.rubydb"))
if r.returncode:
    print(r.stdout, r.stderr)

db = sqlite3.connect("0100000000000ABC.rubydb")
check("formato 1 (user_version)", db.execute("PRAGMA user_version").fetchone()[0] == 1)
check("integridade do banco", db.execute("PRAGMA integrity_check").fetchone()[0] == "ok")
info = db.execute("SELECT * FROM pack_info").fetchall()
check("pack_info: id, título, autor, native, Title ID maiúsculo, builds, versão",
      info == [("0100000000000ABC", "Principal", "Teste", None, "native", "0100000000000ABC", "AABBCC,DDEEFF", "2.1.0")])
check("esquema sem categoria e sem tabela de som",
      "category" not in [c[1] for c in db.execute("PRAGMA table_info(achievements)")] and
      db.execute("SELECT count(*) FROM sqlite_master WHERE name='assets'").fetchone()[0] == 0)
rows = {r[0]: r for r in db.execute("SELECT id, sort_order, title, description, rarity, secret, "
                                    "condition_json, icon_unlocked, icon_locked FROM achievements")}
check("3 conquistas na ordem do JSON", [rows[k][1] for k in ("a", "b", "plat")] == [0, 1, 2])
check("BLOB: ícone desbloqueado igual ao PNG", bytes(rows["a"][7]) == icon_a)
check("BLOB: ícone bloqueado igual ao PNG", bytes(rows["a"][8]) == icon_a_locked)
check("BLOB: ícone de b, sem bloqueado", bytes(rows["b"][7]) == icon_b and rows["b"][8] is None)
cond_a = json.loads(rows["a"][6])
check("condição sem as colunas nem points", cond_a == {"type": "playtime_stat", "target_seconds": 60})
check("colunas: raridade, secreta, descrição", rows["a"][4:6] == ("gold", 1) and rows["a"][3] == "primeira")
check("count_of aninhado preservado", json.loads(rows["b"][6])["conditions"][1]["pointer_offsets"] == ["0x10", "0x8"])
check("Ruby sem condição ('{}')", rows["plat"][6] == "{}" and rows["plat"][4] == "ruby")
check("campos do Studio fora do pacote", b"studio_chains" not in open("0100000000000ABC.rubydb", "rb").read())
db.close()
summary = tool.check_rubydb("0100000000000ABC.rubydb")
check("check_rubydb lê de volta 3 conquistas, 2 ícones e 1 bloqueado",
      summary == {"achievements": 3, "icons": 2, "locked_icons": 1})

# Title ID pelo nome do arquivo quando o JSON não tem; sem ele, emulador.
p = {"achievements": [{"id": "x", "type": "playtime_stat", "target_seconds": 1}]}
tool.write_rubydb(p, "0100000000000DEF.variante.rubydb")
db = sqlite3.connect("0100000000000DEF.variante.rubydb")
check("Title ID do nome do arquivo; título = pack_id",
      db.execute("SELECT pack_id, title, target_type, target_title_id FROM pack_info").fetchone() ==
      ("0100000000000DEF.variante", "0100000000000DEF.variante", "native", "0100000000000DEF"))
db.close()
tool.write_rubydb(p, "Minha ROM.rubydb")
db = sqlite3.connect("Minha ROM.rubydb")
check("ROM: emulador, sem Title ID",
      db.execute("SELECT target_type, target_title_id FROM pack_info").fetchone() == ("emulator", None))
db.close()

# --- recusados (nada gravado)
ok_a = {"id": "ok", "type": "playtime_stat", "target_seconds": 1}
check("sem a lista achievements", refused({"x": 1}, "achievements"))
check("id com caractere inválido", refused({"achievements": [dict(ok_a, id="a b")]}, "inválido"))
check("id repetido", refused({"achievements": [ok_a, ok_a]}, "repetido"))
check("raridade desconhecida", refused({"achievements": [dict(ok_a, rarity="platina")]}, "raridade"))
check("conquista sem condição", refused({"achievements": [{"id": "x"}]}, "sem condição"))
check("duas Rubys", refused({"achievements": [ok_a, {"id": "r1", "rarity": "ruby"}, {"id": "r2", "rarity": "ruby"}]},
                           "Ruby"))
check("só a Ruby", refused({"achievements": [{"id": "r", "rarity": "ruby"}]}, "não tem conquistas"))
check("Title ID inválido", refused({"title_id": "123", "achievements": [ok_a]}, "title_id"))
check("build id que não é hex", refused({"build_id": "XYZ", "achievements": [ok_a]}, "build_id"))
check("texto acima de 64 KB",
      refused({"achievements": [dict(ok_a, description="x" * (65 * 1024))]}, "64 KB"))
write("grande.png", png(600, 10))
check("ícone acima de 512 px", refused({"achievements": [dict(ok_a, icon="grande.png")]}, "512"))
write("nao_png.png", b"GIF89a....")
check("ícone que não é PNG", refused({"achievements": [dict(ok_a, icon="nao_png.png")]}, "não é um PNG"))
write("pesado.png", png(128, 128) + b"\x00" * (300 * 1024))
check("ícone acima de 256 KB", refused({"achievements": [dict(ok_a, icon="pesado.png")]}, "256 KB"))
check("ícone ausente", refused({"achievements": [dict(ok_a, icon="sumiu.png")]}, "sumiu.png"))
check("ícone fora da pasta (..)", refused({"achievements": [dict(ok_a, icon="../x.png")]}, "fora da pasta"))
check("pacote válido não deixa .tmp", not os.path.exists("0100000000000ABC.rubydb.tmp"))

# Catálogo da comunidade (tools/rubydb_index.py): entrada tirada do pacote, SHA-256, troca pelo pack_id.
import hashlib  # noqa: E402
import rubydb_index as idx_tool  # noqa: E402
URL = "https://exemplo.invalid/packs/"
e = idx_tool.make_entry("0100000000000DEF.variante.rubydb", URL + "v.rubydb", version_min="1.2.0")
check("catálogo: pack_id da variante, alvo native com Title ID e versão mínima",
      e["pack_id"] == "variante" and e["target"] == {"type": "native", "title_id": "0100000000000DEF",
                                                       "supported_build_ids": [], "game_version_min": "1.2.0"})
check("catálogo: SHA-256 e total do pacote",
      e["sha256"] == hashlib.sha256(open("0100000000000DEF.variante.rubydb", "rb").read()).hexdigest() and
      e["total_achievements"] == 1)
cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "rubydb_index.py"), "--index", "index.json",
       "--pack", "0100000000000ABC.rubydb", "--url", URL + "abc.rubydb", "--pack-id", "principal"]
r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
idx_tool.add_to_index("index.json", e)
idx_tool.add_to_index("index.json", dict(e, version="9.9"))  # mesmo pack_id: substitui
index = json.load(open("index.json", encoding="utf-8"))
check("catálogo: linha de comando e substituição pelo pack_id (2 pacotes)",
      r.returncode == 0 and [p["pack_id"] for p in index["packs"]] == ["principal", "variante"] and
      index["packs"][1]["version"] == "9.9" and index["updated_at"].endswith("Z"))
check("catálogo: builds e autor do pack_info",
      index["packs"][0]["target"]["supported_build_ids"] == ["AABBCC", "DDEEFF"] and index["packs"][0]["author"] == "Teste")
e = idx_tool.make_entry("Minha ROM.rubydb", URL + "rom.rubydb", pack_id="rom", platform="gba",
                        rom_hash="0123456789ABCDEF0123456789ABCDEF")
check("catálogo: ROM com plataforma e hash",
      e["target"] == {"type": "rom", "platform_code": "GBA", "rom_hash": "0123456789abcdef0123456789abcdef"})


def index_refused(why, *args, **kw):
    try:
        idx_tool.make_entry(*args, **kw)
    except tool.PackError as ex:
        return why in str(ex)
    return False


check("catálogo: ROM sem Title ID nem --platform recusada",
      index_refused("port-slug", "Minha ROM.rubydb", URL + "x", pack_id="rom"))
check("catálogo: URL sem https recusada", index_refused("https", "0100000000000ABC.rubydb", "http://x"))
check("catálogo: pack_id com ponto recusado", index_refused("pack_id", "0100000000000ABC.rubydb", URL, pack_id="a.b"))

print("%s (%d falhas)" % ("FALHOU" if fails else "TUDO OK", fails))
sys.exit(1 if fails else 0)
