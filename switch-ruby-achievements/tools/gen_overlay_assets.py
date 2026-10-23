"""Gera os recursos do pop-up de conquista do overlay.

  python -I tools/gen_overlay_assets.py

1) overlay/source/trophies.inc: troféus de raridade (bronze, prata, ouro, Ruby) em
   RGBA8888, a partir dos PNG de tools/assets/, já no tamanho do pop-up, e o ícone padrão
   das conquistas sem imagem (fundo branco com uma estrela): o overlay não decodifica
   imagens embutidas, só desenha bitmaps prontos.
1b) app_nro/resources/img/trophy_<raridade>.png e trophy_<raridade>_off.png: os mesmos
   troféus para a galeria do app (colorido = desbloqueada; cinza e translúcido = bloqueada).
2) sd_card_structure/switch/ruby_achievements/sfx/conquista.wav: som provisório do pop-up
   (WAV PCM 16 bits, 48 kHz, mono) com a duração exata do pop-up padrão. Para trocar,
   basta pôr outro WAV com o mesmo nome no SD (formato em LEIA-ME.txt da pasta): o
   pop-up passa a durar o tempo do som (de 3 a 10 s).

Requer Pillow. Os arquivos gerados ficam no projeto (o build não roda este script).
"""
import math
import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                         # switch-ruby-achievements/
PRINTS = os.path.join(HERE, "assets")         # PNG originais dos troféus

TROPHY_SIZE = 34                                     # popup::kRarity (overlay/source/popup_anim.hpp)
ICON_SIZE, ICON_RADIUS = 44, 10                      # popup::kIcon, popup::kIconRadius
APP_TROPHY = 72                                      # galeria do app (desenhado com 40 px)
STAR_COLOR = (230, 180, 40, 255)                     # a estrela dourada do app
TROPHIES = [("kTrophyBronze", "Bronze_trophy.png"), ("kTrophySilver", "silver_trophy.png"),
            ("kTrophyGold", "gold_trophy.png"), ("kTrophyRuby", "ruby_trophy.png")]

RATE = 48000
SFX_MS = 5000                                        # popup::kDefaultMs


def trophy_rgba(path, size):
    im = Image.open(path).convert("RGBA")
    im = im.crop(im.getbbox())
    side = max(im.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
    # O Pillow reduz RGBA com alfa pré-multiplicado: bordas sem halo escuro.
    return sq.resize((size, size), Image.LANCZOS).tobytes()


def write_trophies():
    out = os.path.join(ROOT, "overlay", "source", "trophies.inc")
    lines = ["// Gerado por tools/gen_overlay_assets.py a partir de tools/assets/*.png: não editar.",
             "// Troféus de raridade %dx%d RGBA8888 para Renderer::drawBitmap." % (TROPHY_SIZE, TROPHY_SIZE),
             "constexpr int kTrophySize = %d;" % TROPHY_SIZE]
    for name, fn in TROPHIES:
        data = trophy_rgba(os.path.join(PRINTS, fn), TROPHY_SIZE)
        lines.append("static const u8 %s[%d] = {" % (name, len(data)))
        for i in range(0, len(data), 24):
            lines.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        lines.append("};")
    data = star_icon_rgba()
    lines.append("// Ícone padrão (conquista sem imagem): fundo branco arredondado e uma estrela.")
    lines.append("static const u8 kStarIcon[%d] = {" % len(data))
    for i in range(0, len(data), 24):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("ok", out)


def star_icon_rgba():
    """Quadrado branco de cantos arredondados com uma estrela de 5 pontas (4x e reduz)."""
    k = 4
    n = ICON_SIZE * k
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, n - 1, n - 1], radius=ICON_RADIUS * k, fill=(255, 255, 255, 255))
    cx, cy, ro = n / 2, n / 2 + n * 0.03, n * 0.34
    ri = ro * 0.42
    pts = []
    for i in range(10):
        r = ro if i % 2 == 0 else ri
        ang = -math.pi / 2 + i * math.pi / 5
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    d.polygon(pts, fill=STAR_COLOR)
    return im.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS).tobytes()


def write_app_trophies():
    d = os.path.join(ROOT, "app_nro", "resources", "img")
    for (_, fn), rarity in zip(TROPHIES, ("bronze", "silver", "gold", "ruby")):
        im = Image.open(os.path.join(PRINTS, fn)).convert("RGBA")
        im = im.crop(im.getbbox())
        side = max(im.size)
        sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
        sq = sq.resize((APP_TROPHY, APP_TROPHY), Image.LANCZOS)
        sq.save(os.path.join(d, "trophy_%s.png" % rarity), optimize=True)
        r, g, b, a = sq.split()
        gray = Image.merge("RGB", (r, g, b)).convert("L")
        off = Image.merge("RGBA", (gray, gray, gray, a.point(lambda v: int(v * 0.45))))
        off.save(os.path.join(d, "trophy_%s_off.png" % rarity), optimize=True)
    print("ok", d, "(trophy_*.png)")


def chime(n):
    """Toque provisório: acorde curto (estilo "conquista") que se apaga até o fim."""
    notes = [(0.00, 659.25), (0.09, 987.77), (0.18, 1318.51)]  # mi, si, mi agudo
    out = []
    for i in range(n):
        t = i / RATE
        v = 0.0
        for start, f in notes:
            if t >= start:
                u = t - start
                env = math.exp(-u * 2.2) * min(1.0, u / 0.006)
                v += env * (math.sin(2 * math.pi * f * u) + 0.25 * math.sin(4 * math.pi * f * u))
        v *= 0.22
        fade = min(1.0, (n - i) / (RATE * 0.25))     # termina em silêncio exato
        out.append(int(max(-1.0, min(1.0, v * fade)) * 32767))
    return out


def write_sfx():
    d = os.path.join(ROOT, "sd_card_structure", "switch", "ruby_achievements", "sfx")
    os.makedirs(d, exist_ok=True)
    n = RATE * SFX_MS // 1000
    pcm = struct.pack("<%dh" % n, *chime(n))
    head = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    head += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16)
    head += b"data" + struct.pack("<I", len(pcm))
    path = os.path.join(d, "conquista.wav")
    with open(path, "wb") as f:
        f.write(head + pcm)
    with open(os.path.join(d, "LEIA-ME.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("Som do pop-up de conquista do overlay do Ruby Achievements.\n\n"
                "Para trocar, substitua conquista.wav por outro arquivo com o mesmo nome:\n"
                "  - WAV PCM de 16 bits, 48000 Hz, mono ou estéreo;\n"
                "  - o pop-up dura exatamente o tempo do som (de 3 a 10 segundos;\n"
                "    fora disso o som é cortado ou o pop-up completa os 3 s).\n"
                "Sem o arquivo (ou em outro formato) o pop-up fica mudo e dura 5 segundos.\n")
    print("ok", path, "%d ms" % SFX_MS)


if __name__ == "__main__":
    write_trophies()
    write_app_trophies()
    write_sfx()
    sys.exit(0)
