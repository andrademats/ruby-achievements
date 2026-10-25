#pragma once
#include <stdarg.h>

// Log em arquivo texto no SD, uma linha por chamada com o horário (pt_now).
// Cada linha vai para o cartão na hora (fflush + fsync): se o console travar,
// a última linha antes do travamento continua lá. Ao passar de 32 KiB o arquivo
// vira <arquivo>.old (o .old anterior é apagado) e recomeça.
void pt_log_vfile(const char* path, const char* fmt, va_list ap);
void pt_log_file(const char* path, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Log do sysmodule (PT_LOG_PATH).
void pt_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Lote do log do sysmodule: entre begin e end as linhas de PT_LOG_PATH ficam na memória
// e vão para o cartão numa escrita só (a cada 16 KiB ou no end). Usado na avaliação das
// conquistas, que pode registrar centenas de linhas de RAM de uma vez: uma escrita com
// fsync por linha disputava o SD com o jogo e com o som do pop-up.
void pt_log_batch_begin(void);
void pt_log_batch_end(void);
