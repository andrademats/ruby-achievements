#include "pt_log.h"
#include "pt_paths.h"
#include "pt_util.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void pt_log_vfile(const char* path, const char* fmt, va_list ap) {
    struct stat st;
    const char* mode = (stat(path, &st) == 0 && st.st_size > 32 * 1024) ? "w" : "a";
    FILE* f = fopen(path, mode);
    if (!f) return;
    fprintf(f, "[%lld] ", (long long)pt_now());
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
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
