#include "pt_lock.h"
#include "pt_paths.h"
#include "pt_util.h"

#include <switch.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_stamp(void) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)pt_now());
    FILE* f = fopen(PT_LOCK_STAMP, "wb");
    if (f) {
        fwrite(buf, 1, (size_t)n, f);
        fclose(f);
    }
}

// Instante em que o lock foi visto sem carimbo (persistente entre tentativas).
static int64_t g_missing_since = 0;
static int g_mkdir_errno = 0;

// true se o lock existente pode ser considerado abandonado.
static bool lock_is_stale(void) {
    char buf[32];
    int64_t now = pt_now();
    if (pt_read_file(PT_LOCK_STAMP, buf, sizeof(buf)) <= 0) {
        // Dono pode estar entre mkdir() e a escrita do carimbo: espera um pouco.
        if (g_missing_since == 0) g_missing_since = now;
        return now - g_missing_since > PT_LOCK_STALE_SECONDS;
    }
    g_missing_since = 0;
    int64_t stamp = strtoll(buf, NULL, 10);
    int64_t age = now - stamp;
    // age < 0: relógio voltou (ajuste manual); trata como abandonado se grande.
    return age > PT_LOCK_STALE_SECONDS || age < -PT_LOCK_STALE_SECONDS;
}

static void force_remove(void) {
    unlink(PT_LOCK_STAMP);
    rmdir(PT_LOCK_DIR);
}

bool pt_lock_acquire(int timeout_ms) {
    int waited = 0;
    for (;;) {
        if (mkdir(PT_LOCK_DIR, 0777) == 0) {
            write_stamp();
            g_missing_since = 0;
            return true;
        }
        g_mkdir_errno = errno;
        if (lock_is_stale()) {
            force_remove();
            g_missing_since = 0;
            continue;
        }
        if (waited >= timeout_ms) return false;
        svcSleepThread(50'000'000ULL);
        waited += 50;
    }
}

int pt_lock_last_errno(void) { return g_mkdir_errno; }

void pt_lock_release(void) {
    force_remove();
}
