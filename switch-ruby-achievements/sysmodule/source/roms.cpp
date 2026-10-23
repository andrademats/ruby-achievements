#include "roms.h"
#include "pt_log.h"

#include <switch.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

// Caminhos dentro do SD (sem "sdmc:"): consultados direto pela API de FS.
const char* const kRetroArch[] = {"/retroarch/playlists/builtin/content_history.lpl", "/retroarch/content_history.lpl"};
const char* const kPpsspp[] = {"/switch/ppsspp/PSP/SYSTEM/ppsspp.ini", "/switch/ppsspp/config/ppsspp/PSP/SYSTEM/ppsspp.ini",
                               "/PSP/SYSTEM/ppsspp.ini"};
// Frontends sem histórico: pastas onde gravam saves/states.
const char* const kScanRoots[] = {"/tico", "/switch/tico"};
// Onde procurar o arquivo da ROM pelo nome do save (uma pasta por sistema).
const char* const kRomRoots[] = {"/tico/roms", "/roms"};
// Subpastas grandes que não têm saves.
const char* const kSkipDirs[] = {"roms", "system", "bios", "cores", "media", "artwork", "boxart", "covers",
                                 "assets", "shaders", "thumbnails", "downloads", "cache", "themes", "fonts"};

constexpr int kScanDepth = 5;
constexpr int kScanMaxFiles = 600;
constexpr int kDirBatch = 4;

FsFileSystem* sd(void) { return fsdevGetDeviceFileSystem("sdmc"); }

void copy_str(char* dst, size_t cap, const char* src, size_t n) {
    if (cap == 0) return;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// Data de modificação sem abrir o arquivo. 0 = não existe/indisponível.
u64 mtime_of(const char* path) {
    FsFileSystem* fs = sd();
    FsTimeStampRaw ts;
    if (!fs || R_FAILED(fsFsGetFileTimeStampRaw(fs, path, &ts)) || !ts.is_valid) return 0;
    return ts.modified;
}

// Lê o começo de um arquivo (só depois de estável; ver FileSource).
long read_head(const char* path, char* buf, size_t cap) {
    char full[600];
    snprintf(full, sizeof(full), "sdmc:%s", path);
    FILE* f = fopen(full, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (long)n;
}

void strip_sdmc(char* s) {
    if (strncasecmp(s, "sdmc:", 5) == 0) memmove(s, s + 5, strlen(s + 5) + 1);
}

// Nome do arquivo sem pasta e sem a última extensão.
void base_name(const char* path, char* out, size_t cap) {
    const char* b = strrchr(path, '/');
    b = b ? b + 1 : path;
    const char* dot = strrchr(b, '.');
    copy_str(out, cap, b, dot && dot > b ? (size_t)(dot - b) : strlen(b));
}

// Decodifica uma string JSON começando logo após a aspa de abertura.
static const char* json_string(const char* s, const char* end, char* out, size_t cap) {
    size_t n = 0;
    while (s < end && *s != '"') {
        unsigned cp = (unsigned char)*s++;
        if (cp == '\\' && s < end) {
            char e = *s++;
            switch (e) {
                case 'n': cp = '\n'; break;
                case 't': cp = '\t'; break;
                case 'r': cp = '\r'; break;
                case 'b': cp = '\b'; break;
                case 'f': cp = '\f'; break;
                case 'u':
                    if (end - s >= 4) {
                        char hex[5] = {s[0], s[1], s[2], s[3], 0};
                        cp = (unsigned)strtoul(hex, NULL, 16);
                        s += 4;
                        if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';
                    }
                    break;
                default: cp = (unsigned char)e; break;  // \" \\ \/
            }
        }
        char enc[4];
        size_t len;
        if (cp < 0x80) { enc[0] = (char)cp; len = 1; }
        else if (cp < 0x800) { enc[0] = (char)(0xC0 | (cp >> 6)); enc[1] = (char)(0x80 | (cp & 0x3F)); len = 2; }
        else { enc[0] = (char)(0xE0 | (cp >> 12)); enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[2] = (char)(0x80 | (cp & 0x3F)); len = 3; }
        if (n + len < cap) {
            memcpy(out + n, enc, len);
            n += len;
        }
    }
    if (cap) out[n] = '\0';
    return s < end ? s + 1 : NULL;
}

// Procura "key": "valor" em [s, end). Retorna true se encontrou.
static bool json_find_value(const char* s, const char* end, const char* key, char* out, size_t cap) {
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t nlen = strlen(needle);
    for (const char* p = s; p + nlen < end; p++) {
        if (memcmp(p, needle, nlen) != 0) continue;
        const char* q = p + nlen;
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
        if (q >= end || *q != ':') continue;
        q++;
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
        if (q >= end || *q != '"') return false;
        return json_string(q + 1, end, out, cap) != NULL;
    }
    return false;
}

// --- Parsers das fontes de início ---------------------------------------------

// RetroArch: o item mais recente fica no início; 16 KiB bastam.
bool parse_retroarch(const char* file, RomHit* hit) {
    enum { CAP = 16 * 1024 };
    char* buf = (char*)malloc(CAP);
    if (!buf) return false;
    long n = read_head(file, buf, CAP);
    bool ok = false;
    if (n > 0) {
        const char* end = buf + n;
        const char* items = strstr(buf, "\"items\"");
        const char* obj = items ? strchr(items, '{') : NULL;
        const char* obj_end = obj ? strchr(obj, '}') : NULL;  // itens não têm objetos aninhados
        if (obj && obj_end && obj_end <= end && json_find_value(obj, obj_end, "path", hit->path, sizeof(hit->path)) &&
            hit->path[0]) {
            strip_sdmc(hit->path);
            hit->label[0] = '\0';
            json_find_value(obj, obj_end, "label", hit->label, sizeof(hit->label));
            if (!hit->label[0]) base_name(hit->path, hit->label, sizeof(hit->label));
            ok = true;
        }
    }
    free(buf);
    return ok;
}

// PPSSPP: "[Recent]" seguido de "FileName0 = <caminho>" (mais recente).
bool parse_ppsspp(const char* file, RomHit* hit) {
    enum { CAP = 32 * 1024 };
    char* buf = (char*)malloc(CAP);
    if (!buf) return false;
    bool ok = false;
    if (read_head(file, buf, CAP) > 0) {
        const char* sec = strstr(buf, "[Recent]");
        const char* key = sec ? strstr(sec, "FileName0") : NULL;
        const char* eq = key ? strchr(key, '=') : NULL;
        if (eq) {
            eq++;
            while (*eq == ' ' || *eq == '\t') eq++;
            size_t len = strcspn(eq, "\r\n");
            while (len > 0 && (eq[len - 1] == ' ' || eq[len - 1] == '\t')) len--;
            if (len > 0) {
                copy_str(hit->path, sizeof(hit->path), eq, len);
                strip_sdmc(hit->path);
                base_name(hit->path, hit->label, sizeof(hit->label));
                ok = true;
            }
        }
    }
    free(buf);
    return ok;
}

// --- Fontes de início: arquivo de histórico -----------------------------------------

struct FileSource {
    const char* name;
    const char* const* paths;
    int count;
    bool (*parse)(const char*, RomHit*);
    const char* active;  // caminho encontrado
    u64 base;            // data já processada
    u64 pending;         // data nova vista uma vez (espera estabilizar)
};

FileSource g_files[] = {
    {"RetroArch", kRetroArch, (int)(sizeof(kRetroArch) / sizeof(kRetroArch[0])), parse_retroarch, NULL, 0, 0},
    {"PPSSPP", kPpsspp, (int)(sizeof(kPpsspp) / sizeof(kPpsspp[0])), parse_ppsspp, NULL, 0, 0},
};

u64 locate(FileSource& s, const char** path) {
    if (s.active) {
        u64 m = mtime_of(s.active);
        if (m) {
            *path = s.active;
            return m;
        }
    }
    for (int i = 0; i < s.count; i++) {
        u64 m = mtime_of(s.paths[i]);
        if (m) {
            *path = s.paths[i];
            return m;
        }
    }
    return 0;
}

bool file_poll(FileSource& s, RomHit* out) {
    const char* path = NULL;
    u64 m = locate(s, &path);
    if (!m || m == s.base) {
        s.pending = 0;
        return false;
    }
    if (m != s.pending) {
        // Mudou agora: o emulador pode estar gravando. Lê na próxima consulta.
        s.pending = m;
        s.active = path;
        return false;
    }
    s.base = m;
    s.pending = 0;
    s.active = path;
    memset(out, 0, sizeof(*out));
    if (!s.parse(path, out)) {
        pt_log("rom: %s mudou, mas %s nao tem um item legivel", s.name, path);
        return false;
    }
    out->at_start = true;
    out->source = s.name;
    return true;
}

// --- Fonte de saída: saves e save states ------------------------------------------
// Cada varredura guarda (caminho, tamanho, data) de todos os saves achados. Um
// arquivo novo, ou com tamanho/data diferentes da varredura anterior, é um save
// que acabou de ser gravado. Comparar o tamanho cobre cartões em que a data de
// modificação não é gravada.

struct SaveEntry {
    u64 hash;  // do caminho
    s64 size;
    u64 mtime;
};

SaveEntry g_snap[kScanMaxFiles];
int g_snap_n = 0;
bool g_scan_roots = false;  // alguma pasta de frontend existe

struct ScanState {
    SaveEntry* out;
    int n;
    int dirs;
    int no_date;
    // Melhor candidato (alterado) desta varredura.
    bool found;
    u64 best_mtime;
    char best_path[FS_MAX_PATH];
};

u64 hash_path(const char* s) {
    u64 h = 1469598103934665603ULL;  // FNV-1a
    for (; *s; s++) h = (h ^ (u8)tolower((unsigned char)*s)) * 1099511628211ULL;
    return h;
}

bool ieq(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

// Extensões que só embrulham o save (compressão, miniatura do state).
bool is_wrapper_ext(const char* ext) {
    static const char* const kWrap[] = {"zst", "gz", "zip", "lz4", "png", "jpg", "bak", "tmp"};
    for (const char* w : kWrap)
        if (ieq(ext, w)) return true;
    return false;
}

bool is_save_ext(const char* ext) {
    if (!ext || !*ext) return false;
    // mpk/eeprom: memory pak e EEPROM do N64; bkr/bcr: Saturn; nv: NVRAM.
    static const char* const kExt[] = {"srm", "sav", "auto", "dsv", "mcr", "mcd", "eep", "fla", "sra", "rtc",
                                       "sps", "ppst", "sstate", "savestate", "frz", "st", "ss", "sgm", "oops",
                                       "mpk", "eeprom", "bkr", "bcr", "nv"};
    for (const char* e : kExt)
        if (ieq(ext, e)) return true;
    if (!strncasecmp(ext, "state", 5)) return true;  // state, state1, state10...
    return (tolower((unsigned char)ext[0]) == 's') &&
           (tolower((unsigned char)ext[1]) == 's' || tolower((unsigned char)ext[1]) == 't') &&
           isdigit((unsigned char)ext[2]) && !ext[3];  // ss0..ss9 / st0..st9
}

// "Pokemon.gba.state.zst" -> "Pokemon.gba"; retorna se havia extensão de save.
bool strip_save_exts(char* name) {
    bool save = false;
    for (char* dot = strrchr(name, '.'); dot && dot > name; dot = strrchr(name, '.')) {
        if (is_save_ext(dot + 1)) save = true;
        else if (!is_wrapper_ext(dot + 1)) break;
        *dot = '\0';
    }
    return save;
}

// Pastas com "save" ou "state" no nome: qualquer arquivo nelas conta como save.
bool is_save_dir(const char* path) {
    char low[FS_MAX_PATH];
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(low); i++) low[i] = (char)tolower((unsigned char)path[i]);
    low[i] = '\0';
    return strstr(low, "save") || strstr(low, "state");
}

bool skip_dir(const char* name) {
    if (name[0] == '.') return true;
    for (const char* d : kSkipDirs)
        if (ieq(name, d)) return true;
    return false;
}

const SaveEntry* snap_find(u64 hash) {
    for (int i = 0; i < g_snap_n; i++)
        if (g_snap[i].hash == hash) return &g_snap[i];
    return NULL;
}

void walk(FsFileSystem* fs, const char* dir, int depth, ScanState* st) {
    if (st->n >= kScanMaxFiles) return;
    FsDir d;
    if (R_FAILED(fsFsOpenDirectory(fs, dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) return;
    st->dirs++;
    bool save_dir = is_save_dir(dir);
    FsDirectoryEntry* ent = (FsDirectoryEntry*)malloc(sizeof(FsDirectoryEntry) * kDirBatch);
    if (ent) {
        s64 n = 0;
        while (st->n < kScanMaxFiles && R_SUCCEEDED(fsDirRead(&d, &n, kDirBatch, ent)) && n > 0) {
            for (s64 i = 0; i < n && st->n < kScanMaxFiles; i++) {
                char full[FS_MAX_PATH];
                if (snprintf(full, sizeof(full), "%s/%s", dir, ent[i].name) >= (int)sizeof(full)) continue;
                if (ent[i].type == FsDirEntryType_Dir) {
                    if (depth < kScanDepth && !skip_dir(ent[i].name)) walk(fs, full, depth + 1, st);
                    continue;
                }
                char stem[FS_MAX_PATH];
                copy_str(stem, sizeof(stem), ent[i].name, strlen(ent[i].name));
                if (!strip_save_exts(stem) && !save_dir) continue;

                SaveEntry e{hash_path(full), ent[i].file_size, mtime_of(full)};
                if (!e.mtime) st->no_date++;
                st->out[st->n++] = e;
                const SaveEntry* old = snap_find(e.hash);
                bool changed = !old || old->size != e.size || old->mtime != e.mtime;
                // Entre os alterados, o mais recente (sem data: o último visto).
                if (changed && (!st->found || e.mtime >= st->best_mtime)) {
                    st->found = true;
                    st->best_mtime = e.mtime;
                    copy_str(st->best_path, sizeof(st->best_path), full, strlen(full));
                }
            }
        }
        free(ent);
    }
    fsDirClose(&d);
}

// Varre todas as raízes. Atualiza a referência. Retorna false se nenhuma existe.
bool scan(ScanState* st, bool log_summary) {
    static SaveEntry next[kScanMaxFiles];
    memset(st, 0, sizeof(*st));
    st->out = next;
    FsFileSystem* fs = sd();
    if (!fs) return false;
    bool any = false;
    for (const char* root : kScanRoots) {
        FsDirEntryType type;
        if (R_FAILED(fsFsGetEntryType(fs, root, &type)) || type != FsDirEntryType_Dir) continue;
        any = true;
        walk(fs, root, 1, st);
    }
    memcpy(g_snap, next, sizeof(SaveEntry) * st->n);
    g_snap_n = st->n;
    if (log_summary) {
        if (any)
            pt_log("rom: varredura de saves: %d pastas, %d saves (%d sem data de modificacao)%s", st->dirs, st->n,
                   st->no_date, st->n >= kScanMaxFiles ? ", limite atingido" : "");
        else
            pt_log("rom: nenhuma pasta de frontend encontrada (/tico, /switch/tico)");
    }
    return any;
}

// Nomes de save que não dizem qual é o jogo: usa a pasta do save.
bool generic_stem(const char* s) {
    static const char* const kGeneric[] = {"auto", "autosave", "quicksave", "save", "state", "savestate", "slot",
                                           "latest", "last", "resume", "suspend", "default"};
    for (const char* g : kGeneric)
        if (!strncasecmp(s, g, strlen(g)) && (!s[strlen(g)] || isdigit((unsigned char)s[strlen(g)]))) return true;
    for (const char* p = s; *p; p++)
        if (!isdigit((unsigned char)*p)) return false;
    return true;  // só dígitos
}

// Nomes de pasta que são sistemas, não jogos.
bool system_dir_name(const char* s) {
    static const char* const kSys[] = {"gb", "gbc", "gba", "nes", "fds", "snes", "sfc", "n64", "nds", "ds", "3ds",
                                       "psx", "ps1", "psp", "md", "genesis", "megadrive", "sms", "gg", "pce", "tg16",
                                       "dc", "dreamcast", "saturn", "segacd", "32x", "ngp", "ngpc", "ws", "wsc",
                                       "atari2600", "a2600", "lynx", "arcade", "mame", "fbneo", "neogeo", "gc",
                                       "gamecube", "wii", "states", "saves", "savestates", "sram"};
    for (const char* k : kSys)
        if (ieq(s, k)) return true;
    return false;
}

// Rótulo da ROM a partir do caminho do save. false se não dá para saber o jogo.
bool save_label(const char* save_path, char* out, size_t cap) {
    const char* b = strrchr(save_path, '/');
    copy_str(out, cap, b ? b + 1 : save_path, strlen(b ? b + 1 : save_path));
    strip_save_exts(out);
    if (!generic_stem(out)) return true;
    // ".../Pokemon Emerald/auto.state": o jogo é a pasta.
    if (!b || b == save_path) return false;
    const char* pe = b;
    const char* ps = pe - 1;
    while (ps > save_path && *ps != '/') ps--;
    if (*ps == '/') ps++;
    char parent[256];
    copy_str(parent, sizeof(parent), ps, (size_t)(pe - ps));
    if (!parent[0] || system_dir_name(parent)) return false;
    copy_str(out, cap, parent, strlen(parent));
    return true;
}

// Procura em kRomRoots (raiz e uma pasta por sistema) um arquivo cujo nome sem
// extensão seja `label`. Só lista pastas; não abre arquivos.
bool find_rom(FsFileSystem* fs, const char* dir, const char* label, int depth, char* out, size_t cap) {
    FsDir d;
    if (R_FAILED(fsFsOpenDirectory(fs, dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &d)))
        return false;
    bool found = false;
    FsDirectoryEntry* ent = (FsDirectoryEntry*)malloc(sizeof(FsDirectoryEntry) * kDirBatch);
    if (ent) {
        s64 n = 0;
        while (!found && R_SUCCEEDED(fsDirRead(&d, &n, kDirBatch, ent)) && n > 0) {
            for (s64 i = 0; i < n && !found; i++) {
                char full[FS_MAX_PATH];
                if (snprintf(full, sizeof(full), "%s/%s", dir, ent[i].name) >= (int)sizeof(full)) continue;
                if (ent[i].type == FsDirEntryType_Dir) {
                    if (depth < 3) found = find_rom(fs, full, label, depth + 1, out, cap);
                } else {
                    char base[256];
                    base_name(ent[i].name, base, sizeof(base));
                    if (ieq(base, label) || ieq(ent[i].name, label)) {
                        copy_str(out, cap, full, strlen(full));
                        found = true;
                    }
                }
            }
        }
        free(ent);
    }
    fsDirClose(&d);
    return found;
}

bool scan_poll(RomHit* out) {
    ScanState st;
    if (!scan(&st, false) || !st.found) return false;
    memset(out, 0, sizeof(*out));
    if (!save_label(st.best_path, out->label, sizeof(out->label))) {
        pt_log("rom: save gravado %s, mas o nome nao identifica o jogo; tempo continua no frontend", st.best_path);
        return false;
    }
    FsFileSystem* fs = sd();
    bool found = false;
    for (const char* root : kRomRoots)
        if (!found && fs) found = find_rom(fs, root, out->label, 1, out->path, sizeof(out->path));
    if (found) base_name(out->path, out->label, sizeof(out->label));
    else snprintf(out->path, sizeof(out->path), "nome:%s", out->label);  // só o nome: unifica por rótulo igual
    out->at_start = false;
    out->source = "saves";
    pt_log("rom: save state/save detectado: %s -> ROM \"%s\" (%s)", st.best_path, out->label,
           found ? out->path : "arquivo da ROM nao encontrado, usando o nome");
    return true;
}

}  // namespace

void roms_baseline_start(void) {
    for (FileSource& s : g_files) {
        s.active = NULL;
        s.pending = 0;
        const char* path = NULL;
        s.base = locate(s, &path);
        s.active = path;
    }
}

void roms_baseline_scan(void) {
    ScanState st;
    g_scan_roots = scan(&st, true);
}

bool roms_poll(RomHit* out, bool with_scan) {
    for (FileSource& s : g_files)
        if (file_poll(s, out)) {
            pt_log("rom: %s abriu \"%s\" (%s)", s.name, out->label, out->path);
            return true;
        }
    return with_scan && g_scan_roots && scan_poll(out);
}

bool roms_scan_now(RomHit* out) { return g_scan_roots && scan_poll(out); }
