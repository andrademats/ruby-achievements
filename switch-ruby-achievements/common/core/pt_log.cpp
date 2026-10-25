#include "pt_log.h"
#include "pt_paths.h"
#include "pt_util.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace {

constexpr long kLogMax = 32 * 1024;      // passou disso: vira <arquivo>.old e recomeça
constexpr size_t kBatchMax = 16 * 1024;  // lote do sysmodule gravado antes de crescer mais

bool g_batching = false;
std::string g_batch;  // linhas de PT_LOG_PATH esperando pt_log_batch_end

void append(const char* path, const char* text, size_t len) {
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > kLogMax) {
        std::string old = std::string(path) + ".old";
        remove(old.c_str());
        rename(path, old.c_str());
    }
    FILE* f = fopen(path, "a");
    if (!f) return;
    fwrite(text, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

}  // namespace

void pt_log_vfile(const char* path, const char* fmt, va_list ap) {
    char line[640];
    int n = snprintf(line, sizeof(line), "[%lld] ", (long long)pt_now());
    if (n < 0) return;
    int m = vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, ap);
    if (m < 0) m = 0;
    size_t len = (size_t)n + ((size_t)m < sizeof(line) - (size_t)n - 1 ? (size_t)m : sizeof(line) - (size_t)n - 2);
    line[len++] = '\n';
    if (g_batching && strcmp(path, PT_LOG_PATH) == 0) {
        g_batch.append(line, len);
        if (g_batch.size() >= kBatchMax) {
            append(PT_LOG_PATH, g_batch.data(), g_batch.size());
            g_batch.clear();
        }
        return;
    }
    append(path, line, len);
}

void pt_log_file(const char* path, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pt_log_vfile(path, fmt, ap);
    va_end(ap);
}

void pt_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pt_log_vfile(PT_LOG_PATH, fmt, ap);
    va_end(ap);
}

void pt_log_batch_begin(void) { g_batching = true; }

void pt_log_batch_end(void) {
    g_batching = false;
    if (!g_batch.empty()) append(PT_LOG_PATH, g_batch.data(), g_batch.size());
    g_batch.clear();
    g_batch.shrink_to_fit();  // heap de 1,5 MB: o lote não fica reservado
}
