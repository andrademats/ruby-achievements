#include "pt_title.h"
#include "pt_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void copy_str(char* dst, size_t cap, const char* src, size_t n) {
    if (cap == 0) return;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// Firmware 21+: a tabela de nomes do NACP pode vir comprimida (deflate cru),
// com o formato no byte 0x3215 e o tamanho nos 2 primeiros bytes. Sem isso o nome
// lido é lixo binário. Adaptado do Sphaira (NormalizeNacpLangData, GPL-3.0).
static void normalize_nacp_lang(NacpStruct* nacp) {
    const size_t kLangData = 0x3000, kFormatOffset = 0x3215, kCount = 32, kLegacy = 16;
    u8* raw = (u8*)nacp;
    if (raw[kFormatOffset] != 1) return;
    u16 csize;
    memcpy(&csize, raw, sizeof(csize));
    if (!csize || csize > kLangData - sizeof(u16)) return;

    NacpLanguageEntry* out = (NacpLanguageEntry*)calloc(kCount, sizeof(NacpLanguageEntry));
    if (!out) return;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    bool ok = inflateInit2(&zs, -15) == Z_OK;
    if (ok) {
        zs.next_in = raw + sizeof(u16);
        zs.avail_in = csize;
        zs.next_out = (Bytef*)out;
        zs.avail_out = sizeof(NacpLanguageEntry) * kCount;
        ok = inflate(&zs, Z_FINISH) == Z_STREAM_END;
        inflateEnd(&zs);
    }
    if (ok) {
        bool legacy = false;
        for (size_t i = 0; i < kLegacy; i++) legacy |= out[i].name[0] != 0;
        memset(raw, 0, kLangData);
        if (legacy) {
            memcpy(raw, out, kLangData);  // os 16 idiomas de sempre: o idioma do sistema continua valendo
        } else {
            for (size_t i = kLegacy; i < kCount; i++)
                if (out[i].name[0]) {
                    memcpy(raw, &out[i], sizeof(NacpLanguageEntry));
                    break;
                }
        }
        raw[kFormatOffset] = 0;
    }
    free(out);
}

Result pt_title_read(u64 tid, char* name, size_t name_cap, char* icon_path, size_t icon_cap, u64* nacp_size) {
    name[0] = '\0';
    icon_path[0] = '\0';
    *nacp_size = 0;

    // ~145 KiB: alocado só quando é preciso.
    NsApplicationControlData* data = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (!data) return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

    u64 size = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, data,
                                            sizeof(NsApplicationControlData), &size);
    *nacp_size = size;
    if (R_SUCCEEDED(rc) && size >= sizeof(NacpStruct)) {
        normalize_nacp_lang(&data->nacp);
        NacpLanguageEntry* lang = NULL;
        if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &lang)) && lang && lang->name[0])
            copy_str(name, name_cap, lang->name, strnlen(lang->name, sizeof(lang->name)));

        size_t icon_size = size - sizeof(NacpStruct);
        if (icon_size > 0) {
            char path[96];
            snprintf(path, sizeof(path), PT_ICONS_DIR "/%016lX.jpg", tid);
            FILE* f = fopen(path, "wb");
            if (f) {
                bool ok = fwrite(data->icon, 1, icon_size, f) == icon_size;
                ok = fclose(f) == 0 && ok;
                if (ok) copy_str(icon_path, icon_cap, path, strlen(path));
            }
        }
    } else if (R_SUCCEEDED(rc)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);  // NACP incompleto
    }
    free(data);
    return rc;
}

bool pt_account_nickname(AccountUid uid, char* nick, size_t cap) {
    AccountProfile profile;
    if (R_FAILED(accountGetProfile(&profile, uid))) return false;
    AccountProfileBase base;
    memset(&base, 0, sizeof(base));
    Result rc = accountProfileGet(&profile, NULL, &base);
    accountProfileClose(&profile);
    if (R_FAILED(rc) || !base.nickname[0]) return false;
    copy_str(nick, cap, base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
    return true;
}

void pt_uid_to_string(AccountUid uid, char out[33]) {
    snprintf(out, 33, "%016lx%016lx", uid.uid[1], uid.uid[0]);
}
