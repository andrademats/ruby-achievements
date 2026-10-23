#!/usr/bin/env python3
"""Acha no executável `main` do BOTW a cadeia de ponteiros até as flags do
gdt::Manager e preenche os `PREENCHER_...` do JSON de teste.

Uso:
  python find_botw_ram_offsets.py caminho/main [--json tools/botw_ram_teste/01007EF00011E000.json]

`main` é o NSO da ExeFS (comprimido ou não). Sem dependências: o LZ4 é
descomprimido aqui mesmo.

O que o script procura (layout do decomp zeldaret/botw, conferido no código):
  1. ksys::gdt::Manager::createInstance: `mov w0, #0xdc8` (sizeof(Manager))
     antes do `new`; o ponteiro de sInstance vem da GOT carregada logo antes,
     resolvida pela realocação R_AARCH64_RELATIVE.
  2. Manager::allocParam1 (string "param1"): `new TriggerParam` (0x3f0) gravado
     em mFlagBuffer1 e copiado de mFlagBuffer, que fica 8 bytes depois.
  3. TriggerParam: construtor zera mResourceFlags (u32 em 0) e os arrays a
     partir de +8; a cópia percorre mBoolFlags (+8) e mS32Flags (+0x18), cada
     um sead::PtrArray {s32 num, s32 max, FlagBase** buffer em +8}.

Cadeia resultante (como o sysmodule lê `pointer_offsets`, a partir do início
do módulo principal):
  [main + sInstance] -> Manager; [+mFlagBuffer] -> TriggerParam;
  [+mBoolFlags/mS32Flags + 8] -> buffer de ponteiros; [+0] -> primeiro objeto.

Os offsets saem do código estático. Que as flags fiquem dentro da janela de
`search_size` a partir do primeiro objeto só dá para confirmar no console.
"""
import argparse
import json
import struct
import sys

MANAGER_SIZE = 0xDC8
TRIGGER_PARAM_SIZE = 0x3F0
PTR_ARRAY_SIZE = 0x10  # sead::PtrArray: s32 num, s32 max, void** buffer
PTR_ARRAY_BUFFER = 0x8


class AnalysisError(Exception):
    """O main não segue o padrão esperado (ou não é um NSO)."""


def fail(msg):
    raise AnalysisError(msg)


_log = print  # analyze() troca pelo callback de quem chamou


# --- NSO -----------------------------------------------------------------

def lz4_block(src, out_size):
    dst = bytearray(out_size)
    si = di = 0
    n = len(src)
    while si < n:
        tok = src[si]
        si += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[si]
                si += 1
                lit += b
                if b != 255:
                    break
        dst[di:di + lit] = src[si:si + lit]
        si += lit
        di += lit
        if si >= n:
            break
        off = src[si] | (src[si + 1] << 8)
        si += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[si]
                si += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        st = di - off
        if off >= ml:
            dst[di:di + ml] = dst[st:st + ml]
        else:
            pat = bytes(dst[st:di])
            dst[di:di + ml] = (pat * (ml // off + 1))[:ml]
        di += ml
    if di != out_size:
        fail('LZ4 com tamanho errado (%d != %d)' % (di, out_size))
    return dst


def load_nso(path):
    d = open(path, 'rb').read()
    if d[:4] != b'NSO0':
        fail('%s nao e um NSO (falta NSO0)' % path)
    flags = struct.unpack_from('<I', d, 0xC)[0]
    segs = [struct.unpack_from('<III', d, o) for o in (0x10, 0x20, 0x30)]
    csz = struct.unpack_from('<III', d, 0x60)
    bss = struct.unpack_from('<I', d, 0x3C)[0]
    img = bytearray(segs[2][1] + segs[2][2] + bss)
    for i, (fo, mo, sz) in enumerate(segs):
        raw = d[fo:fo + csz[i]]
        img[mo:mo + sz] = lz4_block(raw, sz) if flags & (1 << i) else raw[:sz]
    build_id = d[0x40:0x60].rstrip(b'\0').hex().upper()
    return bytes(img), segs[0][2], build_id


def relative_relocs(img):
    mod0 = struct.unpack_from('<I', img, 4)[0]
    if img[mod0:mod0 + 4] != b'MOD0':
        fail('MOD0 nao encontrado')
    o = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]
    tags = {}
    while True:
        t, v = struct.unpack_from('<qQ', img, o)
        o += 16
        if t == 0:
            break
        tags.setdefault(t, v)
    out = {}
    if 7 in tags:  # DT_RELA, DT_RELASZ
        rela, relasz = tags[7], tags[8]
        for i in range(relasz // 24):
            off, info, add = struct.unpack_from('<QQq', img, rela + 24 * i)
            if info & 0xFFFFFFFF == 1027:  # R_AARCH64_RELATIVE
                out[off] = add
    if 0x24 in tags:  # DT_RELR, DT_RELRSZ: relativas compactas, valor já gravado no lugar
        relr, relrsz = tags[0x24], tags[0x23]
        where = 0
        for i in range(relrsz // 8):
            e = struct.unpack_from('<Q', img, relr + 8 * i)[0]
            if e & 1 == 0:
                out[e] = struct.unpack_from('<Q', img, e)[0]
                where = e + 8
            else:
                for b in range(63):
                    if e >> (b + 1) & 1:
                        p = where + 8 * b
                        out[p] = struct.unpack_from('<Q', img, p)[0]
                where += 63 * 8
    return out


# --- AArch64 (só o necessário) -----------------------------------------------

class Code:
    def __init__(self, img, text_size):
        self.img = img
        self.n = text_size // 4
        self.w = struct.unpack_from('<%dI' % self.n, img, 0)

    def at(self, addr):
        return self.w[addr // 4]

    def find_all(self, pred):
        return [i * 4 for i, w in enumerate(self.w) if pred(w)]


def adrp(addr, w):
    if (w & 0x9F000000) != 0x90000000:
        return None
    imm = ((w >> 29) & 3) | (((w >> 5) & 0x7FFFF) << 2)
    if imm & (1 << 20):
        imm -= 1 << 21
    return (w & 31), (addr & ~0xFFF) + (imm << 12)


def ldst64(w):
    """('ldr'|'str', rt, rn, imm) para LDR/STR Xt, [Xn, #imm]."""
    op = w & 0xFFC00000
    if op not in (0xF9400000, 0xF9000000):
        return None
    return ('ldr' if op == 0xF9400000 else 'str'), w & 31, (w >> 5) & 31, ((w >> 10) & 0xFFF) * 8


def ldr32(w):
    """(rt, rn, imm, pre_index) para LDR Wt, [Xn, #imm] e LDR Wt, [Xn, #imm]!."""
    if (w & 0xFFC00000) == 0xB9400000:
        return w & 31, (w >> 5) & 31, ((w >> 10) & 0xFFF) * 4, False
    if (w & 0xFFE00C00) == 0xB8400C00:
        imm = (w >> 12) & 0x1FF
        if imm & 0x100:
            imm -= 0x200
        return w & 31, (w >> 5) & 31, imm, True
    return None


def mov_w0_imm(w, value):
    """mov w0, #value (MOVZ) ou orr w0, wzr, #value (imediato lógico)."""
    if (w & 0xFFE0001F) == 0x52800000 and ((w >> 5) & 0xFFFF) == value:
        return True
    if (w & 0xFF8003FF) == 0x320003E0:  # ORR W0, WZR, #bitmask
        return decode_bitmask32((w >> 16) & 0x3F, (w >> 10) & 0x3F) == value
    return False


def decode_bitmask32(immr, imms):
    """DecodeBitMasks do ARM para N=0 (32 bits)."""
    inv = ~imms & 0x3F
    if inv == 0:
        return None
    esize = 1 << (inv.bit_length() - 1)
    ones = (imms & (esize - 1)) + 1
    if ones == esize:
        return None
    elem = (1 << ones) - 1
    r = immr & (esize - 1)
    elem = ((elem >> r) | (elem << (esize - r))) & ((1 << esize) - 1)
    out = 0
    for i in range(32 // esize):
        out |= elem << (i * esize)
    return out


def bl_target(addr, w):
    if (w & 0xFC000000) != 0x94000000:
        return None
    imm = w & 0x3FFFFFF
    if imm & (1 << 25):
        imm -= 1 << 26
    return addr + imm * 4


def mov_reg(w):
    """(rd, rm) para MOV Xd, Xm (ORR Xd, XZR, Xm)."""
    if (w & 0xFFE0FFE0) == 0xAA0003E0:
        return w & 31, (w >> 16) & 31
    return None


def find_cstring(img, s, start, end):
    b = s.encode() + b'\0'
    out = []
    i = img.find(b, start)
    while 0 <= i < end:
        if img[i - 1] == 0:
            out.append(i)
        i = img.find(b, i + 1)
    return out


def string_xrefs(code, target):
    """Endereços de ADD Xd, Xn, #lo logo depois de um ADRP para `target`."""
    out = []
    page = target & ~0xFFF
    for i, w in enumerate(code.w[:-8]):
        a = adrp(i * 4, w)
        if not a or a[1] != page:
            continue
        for j in range(1, 8):
            w2 = code.w[i + j]
            if (w2 & 0xFFC00000) == 0x91000000 and ((w2 >> 5) & 31) == a[0] and ((w2 >> 10) & 0xFFF) == (target & 0xFFF):
                out.append((i + j) * 4)
                break
    return out


# --- análise -------------------------------------------------------------

def find_sinstance(code, relocs, near):
    # Outros singletons podem ter o mesmo tamanho: fica o que está junto do
    # código do gdt::Manager (allocParam1).
    allocs = [a for a in code.find_all(lambda w: mov_w0_imm(w, MANAGER_SIZE)) if abs(a - near) < 0x20000]
    found = []
    for a in allocs:
        # if (!sInstance) { ... new(0xdc8) ... sInstance = obj; }: o sInstance é lido
        # logo antes, pela GOT (ldr x, [got]; ldr x, [x]) ou direto (ldr x, [página+lo]).
        for back in range(1, 12):
            p = a - back * 4
            r = adrp(p, code.at(p))
            nxt = ldst64(code.at(p + 4)) if r else None
            if not (nxt and nxt[0] == 'ldr' and nxt[2] == r[0]):
                continue
            addr = r[1] + nxt[3]
            if addr in relocs:  # GOT: a instância é gravada em [Xgot]
                base, off, sinstance, how = nxt[1], 0, relocs[addr], 'via GOT main+0x%X' % addr
            else:  # direto: gravada em [página+lo]
                base, off, sinstance, how = r[0], nxt[3], addr, 'acesso direto (ADRP+LDR)'
            stores = [q for q in range(a, a + 0x80, 4)
                      if (s := ldst64(code.at(q))) and s[0] == 'str' and s[2] == base and s[3] == off]
            if stores and text_end(code) <= sinstance < len(code.img):
                found.append((a, sinstance, how))
            break
    if len(found) != 1:
        fail('createInstance do gdt::Manager: %d candidatos (esperado 1): %s' % (len(found), found))
    a, sinstance, how = found[0]
    _log('gdt::Manager::createInstance em main+0x%X (new de 0x%X bytes)' % (a, MANAGER_SIZE))
    _log('  sInstance em main+0x%X (%s)' % (sinstance, how))
    return sinstance


def text_end(code):
    return code.n * 4


def find_flag_buffer(code, img, ro_start, ro_end):
    strs = find_cstring(img, 'param1', ro_start, ro_end)
    for s in strs:
        for x in string_xrefs(code, s):
            # new TriggerParam logo depois do heap "param1".
            for p in range(x, x + 0x80, 4):
                if not mov_w0_imm(code.at(p), TRIGGER_PARAM_SIZE):
                    continue
                q = p + 4
                while q < p + 0x20 and bl_target(q, code.at(q)) is None:
                    q += 4
                obj = None
                ctor = None
                buf1 = src = None
                this_reg = None
                for r in range(q + 4, q + 0x40, 4):
                    w = code.at(r)
                    m = mov_reg(w)
                    if m and m[1] == 0 and obj is None:
                        obj = m[0]
                    t = bl_target(r, w)
                    if t is not None and ctor is None:
                        ctor = t
                    s64 = ldst64(w)
                    if s64 and s64[0] == 'str' and obj is not None and s64[1] == obj and s64[3] > 0x100:
                        buf1, this_reg = s64[3], s64[2]
                    if s64 and s64[0] == 'ldr' and s64[1] == 1 and s64[3] > 0x100:
                        src = (s64[2], s64[3])
                    if buf1 is not None and src and t is not None and r > q + 8:
                        copy_fn = t
                        break
                else:
                    continue
                if src[0] != this_reg or src[1] != buf1 + 8:
                    fail('allocParam1: mFlagBuffer1=+0x%X, origem da copia=+0x%X (esperado mFlagBuffer1+8)' % (buf1, src[1]))
                _log('Manager::allocParam1 (string "param1" em main+0x%X):' % s)
                _log('  new TriggerParam (0x%X) em mFlagBuffer1 = Manager+0x%X, copiado de mFlagBuffer = Manager+0x%X'
                      % (TRIGGER_PARAM_SIZE, buf1, src[1]))
                return src[1], ctor, copy_fn, p
    fail('allocParam1 (string "param1" + new de 0x%X) nao encontrado' % TRIGGER_PARAM_SIZE)


def check_trigger_param(code, ctor, copy_fn):
    # Construtor: str wzr, [x0|xN] (mResourceFlags) e add Xk, xN, #8 (início dos arrays).
    body = [code.at(ctor + 4 * i) for i in range(24)]
    zero_flags = any(w & 0xFFFFFC1F == 0xB900001F for w in body)
    arrays_at_8 = any((w & 0xFFFFFC00) == 0x91002000 for w in body)  # add Xd, Xn, #0x8
    if not (zero_flags and arrays_at_8):
        fail('construtor do TriggerParam (main+0x%X) fora do layout esperado' % ctor)
    _log('TriggerParam::TriggerParam em main+0x%X: mResourceFlags em +0x0, arrays a partir de +0x8' % ctor)

    # Cópia: lê o num de cada array (bool +0x8, s32 +0x18, f32 +0x28: um PtrArray de
    # 0x10 bytes atrás do outro), com offset normal ou pré-indexado ([x, #8]!).
    nums = {l[2] for i in range(0, 0x400, 4) if (l := ldr32(code.at(copy_fn + i)))}
    if not {0x8, 0x18, 0x28} <= nums:
        fail('copia do TriggerParam (main+0x%X) nao percorre mBoolFlags em +0x8 / mS32Flags em +0x18 '
             '(num lido em %s)' % (copy_fn, sorted(hex(n) for n in nums)))
    bool_arr = 0x8
    s32_arr = bool_arr + PTR_ARRAY_SIZE
    _log('TriggerParam copia em main+0x%X: mBoolFlags em +0x%X, mS32Flags em +0x%X' % (copy_fn, bool_arr, s32_arr))
    return bool_arr + PTR_ARRAY_BUFFER, s32_arr + PTR_ARRAY_BUFFER


def update_json(path, chains):
    pack = json.load(open(path, encoding='utf-8'))
    changed = 0

    def fix(cond):
        nonlocal changed
        if cond.get('type') == 'ram_flag_search':
            # Tipo da flag pelo layout do objeto: FlagBool tem o valor 1 byte antes
            # do hash, FlagS32 4 bytes antes.
            arr = {-1: 'mBoolFlags', -4: 'mS32Flags'}.get(cond.get('offset_after_flag'))
            if arr:
                cond['pointer_offsets'] = chains[arr]
                changed += 1
        for c in cond.get('conditions', []) or []:
            fix(c)

    for a in pack['achievements']:
        fix(a)
    left = json.dumps(pack).count('PREENCHER')
    json.dump(pack, open(path, 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent=2)
    _log('%s: %d condicoes preenchidas, %d PREENCHER restantes' % (path, changed, left))
    if left:
        fail('ainda ha PREENCHER no JSON')


def analyze(path, log=print):
    """Analisa o `main` e devolve build id, tamanho do módulo e as cadeias por
    tipo de flag ("bool", "s32", "f32"). Levanta AnalysisError se o binário não
    tiver o padrão do gdt::Manager."""
    global _log
    _log = log
    img, text_size, build_id = load_nso(path)
    _log('main: build id %s, .text 0x%X bytes, modulo 0x%X bytes' % (build_id, text_size, len(img)))
    code = Code(img, text_size)
    relocs = relative_relocs(img)
    ro_start = (text_size + 0xFFF) & ~0xFFF

    flag_buffer, ctor, copy_fn, alloc_param1 = find_flag_buffer(code, img, ro_start, len(img))
    sinstance = find_sinstance(code, relocs, alloc_param1)
    bool_buf, s32_buf = check_trigger_param(code, ctor, copy_fn)

    f32_buf = s32_buf + PTR_ARRAY_SIZE  # mF32Flags: o array seguinte (+0x28), conferido na cópia
    chain = lambda buf: ['0x%X' % sinstance, '0x%X' % flag_buffer, '0x%X' % buf, '0x0']
    return {
        'engine': 'gdt::Manager',
        'build_id': build_id,
        'module_size': len(img),
        'chains': {'bool': chain(bool_buf), 's32': chain(s32_buf), 'f32': chain(f32_buf)},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('main')
    ap.add_argument('--json', help='JSON de teste para preencher (pointer_offsets com PREENCHER_...)')
    a = ap.parse_args()
    try:
        r = analyze(a.main)
        print()
        for k, v in r['chains'].items():
            print('pointer_offsets %-4s = %s' % (k, json.dumps(v)))
        if a.json:
            update_json(a.json, {'mBoolFlags': r['chains']['bool'], 'mS32Flags': r['chains']['s32']})
    except AnalysisError as e:
        sys.exit('ERRO: ' + str(e))


if __name__ == '__main__':
    main()
