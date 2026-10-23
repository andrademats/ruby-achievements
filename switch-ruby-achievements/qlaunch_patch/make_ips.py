#!/usr/bin/env python3
"""Gera o patch IPS32 da QLaunch para o Atmosphère (exefs_patches).

Uso:
    python make_ips.py patch.txt <BUILD_ID> [saida_dir]

patch.txt tem uma linha por trecho: "<offset hex> <bytes hex>", com o offset
relativo ao início do .text descomprimido do `main` da QLaunch (o mesmo que o
Ghidra/IDA mostram a partir da base do módulo). O Atmosphère soma o cabeçalho
NSO (0x100) aos offsets dos IPS de exefs, então este script soma 0x100.
Linhas vazias e começadas por '#' são ignoradas. Exemplo:

    # hook no handler de mudança de foco da grade -> trampolim na code cave
    0x0012A3B4 1F2003D5
    0x00F80000 E00300AA...

Saída: <saida_dir>/atmosphere/exefs_patches/ruby_achievements_qlaunch/<BUILD_ID>.ips
(BUILD_ID = build id do `main` em hex, normalmente 40 dígitos: os zeros finais
dos 32 bytes podem ser omitidos; é o nome que o Atmosphère procura).
"""
import os
import sys

NSO_HEADER = 0x100


def parse(path):
    chunks = []
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            off, data = line.split(None, 1)
            raw = bytes.fromhex(data.replace(' ', ''))
            if not raw:
                raise SystemExit(f'linha {n}: sem bytes')
            chunks.append((int(off, 16) + NSO_HEADER, raw))
    return chunks


def ips32(chunks):
    out = bytearray(b'IPS32')
    for off, raw in chunks:
        if off == 0x45454F46:  # "EEOF" seria lido como fim do arquivo
            raise SystemExit('offset 0x45454F46 não é representável em IPS32')
        for i in range(0, len(raw), 0xFFFF):
            part = raw[i:i + 0xFFFF]
            out += (off + i).to_bytes(4, 'big') + len(part).to_bytes(2, 'big') + part
    out += b'EEOF'
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    build_id = sys.argv[2].strip().upper()
    if len(build_id) < 16 or any(c not in '0123456789ABCDEF' for c in build_id):
        raise SystemExit('BUILD_ID inválido (hex)')
    out_dir = sys.argv[3] if len(sys.argv) > 3 else '.'
    target = os.path.join(out_dir, 'atmosphere', 'exefs_patches', 'ruby_achievements_qlaunch')
    os.makedirs(target, exist_ok=True)
    path = os.path.join(target, build_id + '.ips')
    with open(path, 'wb') as f:
        f.write(ips32(parse(sys.argv[1])))
    print(path)


if __name__ == '__main__':
    main()
