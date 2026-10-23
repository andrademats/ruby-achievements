#!/usr/bin/env python3
"""Extrai o executável `main` (NSO) da ExeFS de um .nsz.

INCOMPLETO. Num .nsz, o NCA de programa vira .ncz: os primeiros 0x4000 bytes
são guardados como estão no NCA (criptografados) e o resto das seções vem
decriptado e comprimido com zstd. No update 1.6.0 do BOTW a ExeFS começa em
0xC00, então o cabeçalho PFS0, o cabeçalho do NSO e o início do .text ficam
nesse trecho criptografado: este script ainda não o decripta e falha com
"ExeFS nao encontrada".

Uso: python extract_exefs_main.py jogo_update.nsz saida/main [--zstd caminho/zstd.exe]
Requer o zstd de linha de comando (vem com o MSYS2 do devkitPro).
"""
import argparse
import shutil
import struct
import subprocess
import sys
import threading

NCZ_HEADER = 0x4000
SEARCH_LIMIT = 256 * 1024 * 1024  # a ExeFS fica no início do NCA


def pfs0_entries(buf, at=0):
    """[(nome, offset, tamanho)] de um PFS0 em buf[at:], ou None."""
    if buf[at:at + 4] != b'PFS0' or at + 16 > len(buf):
        return None
    n, st = struct.unpack_from('<II', buf, at + 4)
    if not (0 < n < 64 and st < 0x10000) or at + 16 + 24 * n + st > len(buf):
        return None
    names = buf[at + 16 + 24 * n: at + 16 + 24 * n + st]
    data = at + 16 + 24 * n + st
    out = []
    for i in range(n):
        off, size, no, _ = struct.unpack_from('<QQII', buf, at + 16 + 24 * i)
        end = names.find(b'\0', no)
        if end < 0:
            return None
        out.append((names[no:end].decode('ascii', 'replace'), data + off, size))
    return out


def find_ncz(nsz):
    with open(nsz, 'rb') as f:
        head = f.read(0x10000)
    ents = pfs0_entries(head)
    if not ents:
        sys.exit('nao e um NSP/NSZ (PFS0)')
    nczs = [e for e in ents if e[0].endswith('.ncz')]
    if not nczs:
        sys.exit('nenhum .ncz no arquivo: ' + ', '.join(e[0] for e in ents))
    return max(nczs, key=lambda e: e[2])  # o NCA de programa é o maior


def ncz_stream_start(f, ncz_off):
    """Posição do fluxo zstd depois da tabela de seções (formato NCZSECTN)."""
    f.seek(ncz_off + NCZ_HEADER)
    magic, count = struct.unpack('<8sQ', f.read(16))
    if magic != b'NCZSECTN':
        sys.exit('ncz sem NCZSECTN')
    pos = ncz_off + NCZ_HEADER + 16 + 0x40 * count
    f.seek(pos)
    if f.read(8) == b'NCZBLOCK':
        sys.exit('ncz em blocos (NCZBLOCK) nao suportado por este script')
    return pos


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('nsz')
    ap.add_argument('out')
    ap.add_argument('--zstd', default=shutil.which('zstd') or 'C:/devkitPro/msys2/usr/bin/zstd.exe')
    a = ap.parse_args()

    name, ncz_off, ncz_size = find_ncz(a.nsz)
    print('NCA de programa: %s (%d MB)' % (name, ncz_size >> 20))
    f = open(a.nsz, 'rb')
    start = ncz_stream_start(f, ncz_off)
    proc = subprocess.Popen([a.zstd, '-d', '-c', '-q'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def feed():
        f.seek(start)
        left = ncz_off + ncz_size - start
        try:
            while left > 0:
                chunk = f.read(min(1 << 20, left))
                if not chunk:
                    break
                proc.stdin.write(chunk)
                left -= len(chunk)
            proc.stdin.close()
        except (BrokenPipeError, OSError, ValueError):
            pass

    threading.Thread(target=feed, daemon=True).start()
    buf = bytearray()
    found = None
    scanned = 0
    while len(buf) < SEARCH_LIMIT:
        chunk = proc.stdout.read(4 << 20)
        if not chunk:
            break
        buf += chunk
        while found is None:
            i = buf.find(b'PFS0', scanned)
            if i < 0:
                scanned = max(0, len(buf) - 4)
                break
            ents = pfs0_entries(buf, i)
            names = [e[0] for e in ents] if ents else []
            if 'main' in names and 'main.npdm' in names:
                found = (i, ents)
                print('ExeFS no offset 0x%X do NCA: %s' % (NCZ_HEADER + i, ', '.join(names)))
            else:
                scanned = i + 1
        if found:
            _, ents = found
            nm, off, size = next(e for e in ents if e[0] == 'main')
            if len(buf) >= off + size:
                break
    proc.kill()
    if not found:
        sys.exit('ExeFS nao encontrada nos primeiros %d MB' % (SEARCH_LIMIT >> 20))
    nm, off, size = next(e for e in found[1] if e[0] == 'main')
    data = bytes(buf[off:off + size])
    if data[:4] != b'NSO0':
        sys.exit('main nao comeca com NSO0')
    with open(a.out, 'wb') as o:
        o.write(data)
    build_id = data[0x40:0x60].rstrip(b'\0').hex().upper()
    print('main salvo em %s (%d bytes, NSO, build id %s)' % (a.out, len(data), build_id))


if __name__ == '__main__':
    main()
