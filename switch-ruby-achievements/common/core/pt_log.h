#pragma once
#include <stdarg.h>

// Log em arquivo texto no SD, uma linha por chamada com o horário (pt_now).
// Cada linha vai para o cartão na hora (fflush + fsync): se o console travar,
// a última linha antes do travamento continua lá. O arquivo recomeça ao
// passar de 32 KiB.
void pt_log_vfile(const char* path, const char* fmt, va_list ap);
void pt_log_file(const char* path, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Log do sysmodule (PT_LOG_PATH).
void pt_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
