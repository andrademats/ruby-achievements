#include "pt_util.h"
#include "pt_log.h"
#include "pt_paths.h"
#include "pt_update.h"

#include <switch.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

int64_t pt_now(void) {
    u64 t = 0;
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &t)))
        return 0;
    return (int64_t)t;
}

namespace {

bool path_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool is_dir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

__attribute__((format(printf, 1, 2))) void mlog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pt_log_vfile(PT_MIGRATION_LOG_PATH, fmt, ap);
    va_end(ap);
}

// Mesmo conteúdo, byte a byte.
bool same_file(const std::string& a, const std::string& b) {
    FILE* fa = fopen(a.c_str(), "rb");
    FILE* fb = fa ? fopen(b.c_str(), "rb") : nullptr;
    bool same = fa && fb;
    char ba[2048], bb[2048];
    while (same) {
        size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
        same = na == nb && memcmp(ba, bb, na) == 0;
        if (na < sizeof(ba)) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

// Move os itens de `from` para `to`; pastas presentes nos dois são mescladas. Lê os
// nomes antes de mover (renomear durante o readdir pode pular entradas). Retorna
// quantos itens ficaram em `from` (já existiam no destino ou o rename falhou).
int merge_dir(const std::string& from, const std::string& to, int& moved) {
    std::vector<std::string> names;
    DIR* d = opendir(from.c_str());
    if (!d) return 1;
    while (struct dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
    }
    closedir(d);
    int left = 0;
    for (const std::string& n : names) {
        std::string src = from + "/" + n, dst = to + "/" + n;
        if (!path_exists(dst)) {
            if (rename(src.c_str(), dst.c_str()) == 0) {
                moved++;
            } else {
                mlog("nao foi possivel mover %s (fica na pasta antiga)", src.c_str());
                left++;
            }
        } else if (is_dir(src) && is_dir(dst)) {
            int sub = merge_dir(src, dst, moved);
            if (sub == 0) rmdir(src.c_str());
            left += sub;
        } else if (!is_dir(src) && same_file(src, dst)) {
            unlink(src.c_str());  // cópia idêntica (arquivos do dist): nada se perde
        } else {
            mlog("%s ja existe em %s: o antigo fica na pasta antiga", n.c_str(), to.c_str());
            left++;
        }
    }
    return left;
}

// Renomeia `from` para `to` se `from` existir e `to` não.
void rename_free(const std::string& from, const std::string& to) {
    if (!path_exists(from)) return;
    if (path_exists(to)) {
        mlog("%s e %s existem: o antigo nao foi renomeado", from.c_str(), to.c_str());
        return;
    }
    if (rename(from.c_str(), to.c_str()) == 0) mlog("%s -> %s", from.c_str(), to.c_str());
    else mlog("nao foi possivel renomear %s para %s", from.c_str(), to.c_str());
}

}  // namespace

int pt_migrate_legacy(void) {
    const std::string legacy = PT_LEGACY_DIR;
    if (!is_dir(legacy)) return 0;
    mkdir("sdmc:/switch", 0777);
    mkdir(PT_DIR, 0777);
    if (!is_dir(PT_DIR)) return 1;

    // O app antigo: abriria a pasta antiga de novo (o novo é PT_APP_NRO).
    if (unlink(PT_LEGACY_DIR "/" PT_LEGACY_NRO_NAME) == 0) mlog("removido %s", PT_LEGACY_DIR "/" PT_LEGACY_NRO_NAME);
    int moved = 0;
    int left = merge_dir(legacy, PT_DIR, moved);
    rename_free(PT_DIR "/" PT_LEGACY_DB_NAME, PT_DB_PATH);
    rename_free(PT_DIR "/" PT_LEGACY_DB_NAME "-journal", PT_DB_PATH "-journal");
    // Overlay antigo: com os dois instalados, o Ultrahand listaria dois.
    if (path_exists(PT_OVERLAY_PATH) && unlink(PT_LEGACY_OVERLAY_PATH) == 0)
        mlog("removido %s", PT_LEGACY_OVERLAY_PATH);
    if (left == 0) rmdir(legacy.c_str());
    mlog("migracao de %s para %s: %d itens movidos, %d ficaram na pasta antiga", PT_LEGACY_DIR, PT_DIR, moved, left);
    return left;
}

void pt_ensure_dirs(void) {
    pt_migrate_legacy();
    mkdir("sdmc:/switch", 0777);
    mkdir(PT_DIR, 0777);
    mkdir(PT_ICONS_DIR, 0777);
    mkdir(PT_COVERS_DIR, 0777);
    mkdir(PT_ACH_DIR, 0777);
    mkdir(PT_ACH_ICONS_DIR, 0777);
    mkdir(PT_ACH_CACHE_DIR, 0777);
    mkdir(PT_POPUP_CACHE_DIR, 0777);
    pt_update_recover();  // atualização interrompida (queda de energia): volta a versão anterior
}

void pt_format_duration(char* out, size_t cap, int64_t seconds) {
    if (seconds < 0) seconds = 0;
    long long h = seconds / 3600;
    long long m = (seconds % 3600) / 60;
    if (h > 0)
        snprintf(out, cap, "%lldh %02lldm", h, m);
    else
        snprintf(out, cap, "%lldm", m);
}

long pt_read_file(const char* path, char* buf, size_t cap) {
    if (cap == 0) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (long)n;
}

int pt_write_file_atomic(const char* path, const char* data, size_t len) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return -1;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        unlink(tmp);
        return -1;
    }
    // O FS do Switch não sobrescreve no rename: remove o destino antes.
    unlink(path);
    return rename(tmp, path) == 0 ? 0 : -1;
}

char* pt_trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}
