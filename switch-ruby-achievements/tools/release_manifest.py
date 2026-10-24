#!/usr/bin/env python3
"""Manifesto assinado da atualização automática (aba Atualização do app).

A release tem três assets: o pacote `ruby-achievements-vX.Y.Z.zip` (o `dist/` inteiro, que é o que
uma pessoa baixa para instalar), `manifest.json` e `manifest.sig`. O app baixa os dois do manifesto,
confere a assinatura Ed25519 com a chave pública embutida (app_nro/source/update_key.inc), baixa o
zip conferindo tamanho e SHA-256 e extrai dele só os binários da lista fixa, conferindo cada um.
O zip é gravado sem compressão (ZIP_STORED): o app só copia os bytes, sem descompactar.

Comandos:
  keygen <arquivo>       cria a chave privada (32 bytes em hex) FORA do repositório e grava
                         app_nro/source/update_key.inc com a chave pública
  pubkey <arquivo>       mostra a chave pública de uma chave privada
  make --key <arquivo> [--dist DIR] [--out DIR] [--min X.Y.Z] [--notes TEXTO]
                         gera o zip, manifest.json e manifest.sig (versão do arquivo VERSION)
  selftest               confere o Ed25519 com os vetores da RFC 8032

Sem dependências: Ed25519 é a implementação de referência da RFC 8032 (seção 6), lenta mas
suficiente para assinar um manifesto. A chave privada nunca vai para o Git.
"""
import argparse
import hashlib
import json
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # switch-ruby-achievements/

# Mesma lista fixa de common/core/pt_update.cpp: caminho no SD e nome do asset.
TARGETS = [
    ("switch/ruby_achievements/ruby_achievements.nro", "ruby_achievements.nro"),
    ("switch/.overlays/ruby-achievements-overlay.ovl", "ruby-achievements-overlay.ovl"),
    ("atmosphere/contents/0100000000007A11/exefs.nsp", "exefs.nsp"),
    ("atmosphere/contents/0100000000007A11/toolbox.json", "toolbox.json"),
]

# --- Ed25519 (RFC 8032, seção 6) ---------------------------------------------------------
_p = 2**255 - 19
_q = 2**252 + 27742317777372353535851937790883648493


def _inv(x):
    return pow(x, _p - 2, _p)


_d = -121665 * _inv(121666) % _p
_sqrt_m1 = pow(2, (_p - 1) // 4, _p)


def _sha512(b):
    return hashlib.sha512(b).digest()


def _sha512_modq(b):
    return int.from_bytes(_sha512(b), "little") % _q


def _add(P, Q):
    A, B = (P[1] - P[0]) * (Q[1] - Q[0]) % _p, (P[1] + P[0]) * (Q[1] + Q[0]) % _p
    C, D = 2 * P[3] * Q[3] * _d % _p, 2 * P[2] * Q[2] % _p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F, G * H, F * G, E * H)


def _mul(s, P):
    Q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            Q = _add(Q, P)
        P = _add(P, P)
        s >>= 1
    return Q


def _equal(P, Q):
    return (P[0] * Q[2] - Q[0] * P[2]) % _p == 0 and (P[1] * Q[2] - Q[1] * P[2]) % _p == 0


def _recover_x(y, sign):
    if y >= _p:
        return None
    x2 = (y * y - 1) * _inv(_d * y * y + 1) % _p
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_p + 3) // 8, _p)
    if (x * x - x2) % _p != 0:
        x = x * _sqrt_m1 % _p
    if (x * x - x2) % _p != 0:
        return None
    if (x & 1) != sign:
        x = _p - x
    return x


_gy = 4 * _inv(5) % _p
_gx = _recover_x(_gy, 0)
_G = (_gx, _gy, 1, _gx * _gy % _p)


def _compress(P):
    zi = _inv(P[2])
    x, y = P[0] * zi % _p, P[1] * zi % _p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % _p)


def _expand(secret):
    if len(secret) != 32:
        raise ValueError("chave privada precisa de 32 bytes")
    h = _sha512(secret)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(secret):
    a, _ = _expand(secret)
    return _compress(_mul(a, _G))


def sign(secret, msg):
    a, prefix = _expand(secret)
    A = _compress(_mul(a, _G))
    r = _sha512_modq(prefix + msg)
    Rs = _compress(_mul(r, _G))
    s = (r + _sha512_modq(Rs + A + msg) * a) % _q
    return Rs + int.to_bytes(s, 32, "little")


def verify(public, msg, sig):
    if len(public) != 32 or len(sig) != 64:
        return False
    A, R = _decompress(public), _decompress(sig[:32])
    if not A or not R:
        return False
    s = int.from_bytes(sig[32:], "little")
    if s >= _q:
        return False
    h = _sha512_modq(sig[:32] + public + msg)
    return _equal(_mul(s, _G), _add(R, _mul(h, A)))


# Vetores 1 e 2 da RFC 8032 (seção 7.1): chave privada, pública, mensagem, assinatura.
_VECTORS = [
    ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
     "d25bf5f0595bbe24655141438e7a100b"),
    ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c"
     "387b2eaeb4302aeeb00d291612bb0c00"),
]


def selftest():
    for sk, pk, msg, sig in _VECTORS:
        sk, pk, msg, sig = map(bytes.fromhex, (sk, pk, msg, sig))
        if public_key(sk) != pk or sign(sk, msg) != sig or not verify(pk, msg, sig):
            return False
        if verify(pk, msg + b"x", sig):
            return False
    return True


# --- Chaves e manifesto --------------------------------------------------------------------
def read_secret(path):
    with open(path, encoding="ascii") as f:
        return bytes.fromhex(f.read().strip())


def write_key_inc(pub):
    path = os.path.join(ROOT, "app_nro", "source", "update_key.inc")
    body = ", ".join(f"0x{b:02x}" for b in pub)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Chave pública Ed25519 das releases (gerada por tools/release_manifest.py keygen).\n"
                "// A privada fica só na máquina de quem publica, fora do Git.\n"
                f"{{{body}}}\n")
    return path


def parse_version(text):
    parts = text.strip().lstrip("v").split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        raise ValueError(f"versão inválida: {text!r} (use X.Y.Z)")
    return ".".join(str(int(p)) for p in parts)


def cmd_keygen(args):
    if os.path.exists(args.file):
        sys.exit(f"{args.file} já existe: não sobrescrevo uma chave de release")
    if os.path.commonpath([os.path.abspath(args.file), os.path.dirname(ROOT)]) == os.path.dirname(ROOT):
        sys.exit("guarde a chave privada fora da pasta do projeto (ela nunca vai para o Git)")
    secret = os.urandom(32)
    os.makedirs(os.path.dirname(os.path.abspath(args.file)), exist_ok=True)
    with open(args.file, "w", encoding="ascii") as f:
        f.write(secret.hex() + "\n")
    pub = public_key(secret)
    print("chave privada:", os.path.abspath(args.file), "(faça backup; sem ela não há como publicar)")
    print("chave pública:", pub.hex())
    print("gravado:", write_key_inc(pub))


def cmd_pubkey(args):
    print(public_key(read_secret(args.file)).hex())


def cmd_make(args):
    if not selftest():
        sys.exit("selftest do Ed25519 falhou")
    with open(os.path.join(ROOT, "VERSION"), encoding="ascii") as f:
        version = parse_version(f.read())
    secret = read_secret(args.key)
    files = []
    for rel, _ in TARGETS:
        src = os.path.join(args.dist, *rel.split("/"))
        if not os.path.isfile(src):
            sys.exit(f"falta no dist: {rel} (rode make dist)")
        with open(src, "rb") as f:
            data = f.read()
        files.append({"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    os.makedirs(args.out, exist_ok=True)
    zip_name = f"ruby-achievements-v{version}.zip"
    zip_path = os.path.join(args.out, zip_name)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_STORED) as z:
        for dirpath, dirnames, filenames in os.walk(args.dist):
            dirnames.sort()
            for name in sorted(filenames):
                full = os.path.join(dirpath, name)
                z.write(full, os.path.relpath(full, args.dist).replace(os.sep, "/"))
    with open(zip_path, "rb") as f:
        data = f.read()
    package = {"name": zip_name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    manifest = {"version": version, "min_version": parse_version(args.min) if args.min else "0.0.0",
                "notes": args.notes or "", "package": package, "files": files}
    raw = (json.dumps(manifest, ensure_ascii=False, indent=1) + "\n").encode("utf-8")
    sig = sign(secret, raw)
    if not verify(public_key(secret), raw, sig):
        sys.exit("assinatura não confere")
    with open(os.path.join(args.out, "manifest.json"), "wb") as f:
        f.write(raw)
    with open(os.path.join(args.out, "manifest.sig"), "w", encoding="ascii", newline="\n") as f:
        f.write(sig.hex() + "\n")
    print(f"versão {version}: {zip_name} ({len(data)} bytes), manifesto assinado em {args.out}")
    print("chave pública:", public_key(secret).hex())
    print(f"publique {zip_name}, manifest.json e manifest.sig como assets da release v{version}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("file")
    p = sub.add_parser("pubkey")
    p.add_argument("file")
    m = sub.add_parser("make")
    m.add_argument("--key", required=True)
    m.add_argument("--dist", default=os.path.join(ROOT, "dist"))
    m.add_argument("--out", default=os.path.join(ROOT, "release"))
    m.add_argument("--min")
    m.add_argument("--notes")
    sub.add_parser("selftest")
    args = ap.parse_args()
    if args.cmd == "keygen":
        cmd_keygen(args)
    elif args.cmd == "pubkey":
        cmd_pubkey(args)
    elif args.cmd == "make":
        cmd_make(args)
    else:
        ok = selftest()
        print("Ed25519 (RFC 8032):", "ok" if ok else "FALHOU")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
