#include "pt_vfs.h"
#include "sqlite3.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {

bool g_trace = false;
Result g_last_rc = 0;

#define TRACE(...)                          \
    do {                                    \
        if (g_trace) pt_dblog(__VA_ARGS__); \
    } while (0)

// Kernel recusa mapear o buffer na IPC (mesmo caso tratado pelo fsdev): usa um
// buffer intermediário na pilha.
constexpr u32 kBadBuffer = 0xD401;
constexpr u64 kBounce = 0x400;

constexpr u32 kModuleFs = 2;  // módulo do serviço FS nos códigos de resultado

bool is_not_found(Result rc) { return R_MODULE(rc) == kModuleFs && R_DESCRIPTION(rc) == 1; }  // 0x202

Result fail(Result rc) {
    if (R_FAILED(rc)) g_last_rc = rc;
    return rc;
}

Result read_at(FsFile* f, s64 off, void* buf, u64 len, u64* got) {
    Result rc = fsFileRead(f, off, buf, len, FsReadOption_None, got);
    if (R_VALUE(rc) != kBadBuffer) return rc;
    u8 tmp[kBounce];
    *got = 0;
    while (*got < len) {
        u64 chunk = len - *got < kBounce ? len - *got : kBounce;
        u64 n = 0;
        rc = fsFileRead(f, off + (s64)*got, tmp, chunk, FsReadOption_None, &n);
        if (R_FAILED(rc)) return rc;
        memcpy((u8*)buf + *got, tmp, n);
        *got += n;
        if (n < chunk) break;
    }
    return 0;
}

Result write_at(FsFile* f, s64 off, const void* buf, u64 len) {
    Result rc = fsFileWrite(f, off, buf, len, FsWriteOption_None);
    if (R_VALUE(rc) != kBadBuffer) return rc;
    u8 tmp[kBounce];
    for (u64 done = 0; done < len;) {
        u64 chunk = len - done < kBounce ? len - done : kBounce;
        memcpy(tmp, (const u8*)buf + done, chunk);
        rc = fsFileWrite(f, off + (s64)done, tmp, chunk, FsWriteOption_None);
        if (R_FAILED(rc)) return rc;
        done += chunk;
    }
    return 0;
}

struct PtFile {
    sqlite3_file base;
    FsFile f;
    FsFileSystem* fs;
    char path[FS_MAX_PATH];  // caminho dentro do FS (sem "sdmc:")
    const char* name;        // nome dado pelo SQLite (válido até o xClose)
    bool delete_on_close;
};

int x_close(sqlite3_file* p) {
    PtFile* f = (PtFile*)p;
    fsFileClose(&f->f);
    TRACE("[vfs] close %s", f->name);
    if (f->delete_on_close) fsFsDeleteFile(f->fs, f->path);
    return SQLITE_OK;
}

int x_read(sqlite3_file* p, void* buf, int amt, sqlite3_int64 off) {
    PtFile* f = (PtFile*)p;
    s64 size = 0;
    u64 got = 0;
    Result rc = fsFileGetSize(&f->f, &size);
    // O Horizon não lê além do fim: o que faltar volta zerado (short read).
    if (R_SUCCEEDED(rc) && off < size) {
        u64 want = (u64)amt < (u64)(size - off) ? (u64)amt : (u64)(size - off);
        rc = read_at(&f->f, off, buf, want, &got);
    }
    TRACE("[vfs] read %s off=%lld len=%d -> %llu bytes (tamanho %lld, rc=0x%X)", f->name, (long long)off, amt,
          (unsigned long long)got, (long long)size, rc);
    if (R_FAILED(fail(rc))) return SQLITE_IOERR_READ;
    if (got < (u64)amt) {
        memset((u8*)buf + got, 0, (size_t)amt - got);
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

int x_write(sqlite3_file* p, const void* buf, int amt, sqlite3_int64 off) {
    PtFile* f = (PtFile*)p;
    s64 size = 0;
    Result rc = fsFileGetSize(&f->f, &size);
    // Escrita além do fim: estende primeiro (o Horizon não cria buracos).
    if (R_SUCCEEDED(rc) && off > size) rc = fsFileSetSize(&f->f, off);
    if (R_SUCCEEDED(rc)) rc = write_at(&f->f, off, buf, (u64)amt);
    TRACE("[vfs] write %s off=%lld len=%d (tamanho antes %lld) -> rc=0x%X", f->name, (long long)off, amt,
          (long long)size, rc);
    return R_FAILED(fail(rc)) ? SQLITE_IOERR_WRITE : SQLITE_OK;
}

int x_truncate(sqlite3_file* p, sqlite3_int64 size) {
    PtFile* f = (PtFile*)p;
    Result rc = fsFileSetSize(&f->f, size);
    TRACE("[vfs] truncate %s para %lld -> rc=0x%X", f->name, (long long)size, rc);
    return R_FAILED(fail(rc)) ? SQLITE_IOERR_TRUNCATE : SQLITE_OK;
}

int x_sync(sqlite3_file* p, int flags) {
    (void)flags;
    PtFile* f = (PtFile*)p;
    Result rc = fsFileFlush(&f->f);
    TRACE("[vfs] sync %s -> rc=0x%X", f->name, rc);
    return R_FAILED(fail(rc)) ? SQLITE_IOERR_FSYNC : SQLITE_OK;
}

int x_file_size(sqlite3_file* p, sqlite3_int64* out) {
    PtFile* f = (PtFile*)p;
    s64 size = 0;
    Result rc = fsFileGetSize(&f->f, &size);
    *out = size;
    return R_FAILED(fail(rc)) ? SQLITE_IOERR_FSTAT : SQLITE_OK;
}

int x_lock(sqlite3_file*, int) { return SQLITE_OK; }
int x_check_reserved(sqlite3_file*, int* out) {
    *out = 0;
    return SQLITE_OK;
}
int x_file_control(sqlite3_file*, int, void*) { return SQLITE_NOTFOUND; }
int x_sector_size(sqlite3_file*) { return 512; }
int x_device_characteristics(sqlite3_file*) { return 0; }

const sqlite3_io_methods kIo = {
    1,          x_close, x_read,          x_write,       x_truncate,       x_sync, x_file_size,
    x_lock,     x_lock,  x_check_reserved, x_file_control, x_sector_size, x_device_characteristics,
    nullptr,    nullptr, nullptr,          nullptr,        nullptr,       nullptr,
};

const char* kind(int flags) {
    if (flags & SQLITE_OPEN_MAIN_DB) return "banco";
    if (flags & SQLITE_OPEN_MAIN_JOURNAL) return "journal";
    return "outro";
}

int x_open(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* p, int flags, int* out_flags) {
    PtFile* f = (PtFile*)p;
    memset(f, 0, sizeof(*f));  // pMethods NULL: em erro o SQLite não chama xClose
    if (!name) {
        // SQLITE_TEMP_STORE=3 mantém arquivos temporários em memória.
        pt_dblog("[vfs] open sem nome (temporario) recusado, flags=0x%X", flags);
        return SQLITE_CANTOPEN;
    }
    if (fsdevTranslatePath(name, &f->fs, f->path) < 0) {
        pt_dblog("[vfs] open %s: caminho fora de um dispositivo montado", name);
        return SQLITE_CANTOPEN;
    }

    FsDirEntryType type;
    Result rc = fsFsGetEntryType(f->fs, f->path, &type);
    bool exists = R_SUCCEEDED(rc);
    if (exists && type == FsDirEntryType_Dir) {
        pt_dblog("[vfs] open %s: e um diretorio", name);
        return SQLITE_CANTOPEN;
    }
    if (exists && (flags & SQLITE_OPEN_EXCLUSIVE)) return SQLITE_CANTOPEN;
    if (!exists) {
        if (!(flags & SQLITE_OPEN_CREATE)) {
            TRACE("[vfs] open %s (%s): nao existe", name, kind(flags));
            return SQLITE_CANTOPEN;
        }
        rc = fsFsCreateFile(f->fs, f->path, 0, 0);
        TRACE("[vfs] create %s (%s, tamanho 0) -> rc=0x%X", name, kind(flags), rc);
        if (R_FAILED(fail(rc))) return SQLITE_CANTOPEN;
    }

    u32 mode = (flags & SQLITE_OPEN_READONLY) ? FsOpenMode_Read
                                              : (FsOpenMode_Read | FsOpenMode_Write | FsOpenMode_Append);
    rc = fsFsOpenFile(f->fs, f->path, mode, &f->f);
    s64 size = -1;
    if (R_SUCCEEDED(rc)) fsFileGetSize(&f->f, &size);
    TRACE("[vfs] open %s (%s, %s) -> rc=0x%X tamanho %lld", name, kind(flags), exists ? "existente" : "novo", rc,
          (long long)size);
    if (R_FAILED(fail(rc))) return SQLITE_CANTOPEN;

    f->name = name;
    f->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
    f->base.pMethods = &kIo;
    if (out_flags) *out_flags = flags;
    return SQLITE_OK;
}

int x_delete(sqlite3_vfs*, const char* name, int) {
    FsFileSystem* fs;
    char path[FS_MAX_PATH];
    if (fsdevTranslatePath(name, &fs, path) < 0) return SQLITE_IOERR_DELETE;
    Result rc = fsFsDeleteFile(fs, path);
    TRACE("[vfs] delete %s -> rc=0x%X", name, rc);
    if (R_SUCCEEDED(rc)) return SQLITE_OK;
    if (is_not_found(rc)) return SQLITE_IOERR_DELETE_NOENT;
    fail(rc);
    return SQLITE_IOERR_DELETE;
}

int x_access(sqlite3_vfs*, const char* name, int, int* out) {
    FsFileSystem* fs;
    char path[FS_MAX_PATH];
    FsDirEntryType type;
    *out = fsdevTranslatePath(name, &fs, path) >= 0 && R_SUCCEEDED(fsFsGetEntryType(fs, path, &type));
    TRACE("[vfs] access %s -> %s", name, *out ? "existe" : "nao existe");
    return SQLITE_OK;
}

int x_full_pathname(sqlite3_vfs*, const char* name, int n_out, char* out) {
    // Os caminhos já são absolutos ("sdmc:/...").
    if ((int)strlen(name) >= n_out) return SQLITE_CANTOPEN;
    strcpy(out, name);
    return SQLITE_OK;
}

int x_randomness(sqlite3_vfs*, int n, char* out) {
    randomGet(out, (size_t)n);
    return n;
}

int x_sleep(sqlite3_vfs*, int us) {
    svcSleepThread((s64)us * 1000);
    return us;
}

int x_current_time_int64(sqlite3_vfs*, sqlite3_int64* out) {
    *out = (sqlite3_int64)time(NULL) * 1000 + 210866760000000LL;  // ms desde a época juliana
    return SQLITE_OK;
}

int x_current_time(sqlite3_vfs* v, double* out) {
    sqlite3_int64 ms;
    x_current_time_int64(v, &ms);
    *out = (double)ms / 86400000.0;
    return SQLITE_OK;
}

int x_get_last_error(sqlite3_vfs*, int n, char* out) {
    if (n > 0) snprintf(out, (size_t)n, "libnx 0x%X", g_last_rc);
    return (int)g_last_rc;
}

sqlite3_vfs g_vfs = {
    2,       (int)sizeof(PtFile), 512,     nullptr,        PT_VFS_NAME, nullptr,
    x_open,  x_delete,            x_access, x_full_pathname, nullptr,     nullptr,
    nullptr, nullptr,             x_randomness, x_sleep,     x_current_time, x_get_last_error,
    x_current_time_int64, nullptr, nullptr, nullptr,
};

}  // namespace

extern "C" int sqlite3_os_init(void) { return sqlite3_vfs_register(&g_vfs, 1); }
extern "C" int sqlite3_os_end(void) { return SQLITE_OK; }

void pt_vfs_set_trace(bool on) { g_trace = on; }
uint32_t pt_vfs_last_result(void) { return g_last_rc; }

bool pt_vfs_probe(const char* path, int64_t* size, uint8_t* head, size_t cap, size_t* got) {
    *size = -1;
    *got = 0;
    FsFileSystem* fs;
    char p[FS_MAX_PATH];
    FsDirEntryType type;
    if (fsdevTranslatePath(path, &fs, p) < 0 || R_FAILED(fsFsGetEntryType(fs, p, &type))) return false;
    FsFile f;
    if (R_FAILED(fsFsOpenFile(fs, p, FsOpenMode_Read, &f))) return true;
    s64 sz = 0;
    if (R_SUCCEEDED(fsFileGetSize(&f, &sz))) {
        *size = sz;
        u64 want = (u64)sz < cap ? (u64)sz : cap;
        u64 n = 0;
        if (want > 0 && R_SUCCEEDED(read_at(&f, 0, head, want, &n))) *got = (size_t)n;
    }
    fsFileClose(&f);
    return true;
}

int pt_vfs_discard(const char* path) {
    FsFileSystem* fs;
    char db[FS_MAX_PATH], journal[FS_MAX_PATH + 16], bad[FS_MAX_PATH + 16];
    if (fsdevTranslatePath(path, &fs, db) < 0) return -1;
    snprintf(journal, sizeof(journal), "%s-journal", db);
    snprintf(bad, sizeof(bad), "%s.corrupt", db);

    Result rc = fsFsDeleteFile(fs, journal);
    pt_dblog("[vfs] apagar journal %s -> rc=0x%X%s", journal, rc, is_not_found(rc) ? " (nao existia)" : "");
    rc = fsFsDeleteFile(fs, bad);
    pt_dblog("[vfs] apagar copia anterior %s -> rc=0x%X%s", bad, rc, is_not_found(rc) ? " (nao existia)" : "");
    rc = fsFsRenameFile(fs, db, bad);
    pt_dblog("[vfs] renomear %s -> %s: rc=0x%X", db, bad, rc);
    if (R_FAILED(rc) && !is_not_found(rc)) {
        rc = fsFsDeleteFile(fs, db);
        pt_dblog("[vfs] renomear falhou; apagar %s -> rc=0x%X", db, rc);
        if (R_FAILED(rc) && !is_not_found(rc)) return -1;
    }
    return 0;
}
