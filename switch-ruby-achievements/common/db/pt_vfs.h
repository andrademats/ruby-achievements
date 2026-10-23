#pragma once
// VFS "switch" do SQLite: arquivos via API de FS do libnx (fsFs*/fsFile*), sem a
// camada POSIX da newlib/fsdev. Sem locks: a exclusão mútua é feita por pt_lock.
// Registrada como VFS padrão em sqlite3_os_init (SQLITE_OS_OTHER).
#include <stddef.h>
#include <stdint.h>

#define PT_VFS_NAME "switch"

// Liga o log de cada operação de I/O (open/read/write/truncate/sync/delete).
void pt_vfs_set_trace(bool on);

// Inspeciona um arquivo fechado: retorna false se não existe. Preenche o tamanho
// e lê até `cap` bytes do início em `head` (`got` = bytes lidos).
bool pt_vfs_probe(const char* path, int64_t* size, uint8_t* head, size_t cap, size_t* got);

// Tira o banco do caminho: apaga o journal e renomeia o arquivo para
// "<path>.corrupt" (substitui o anterior). Retorna 0 se o caminho ficou livre.
int pt_vfs_discard(const char* path);

// Último código de resultado do libnx que falhou dentro da VFS (0 = nenhum).
uint32_t pt_vfs_last_result(void);

// Log de diagnóstico do banco (implementado em pt_db.cpp).
void pt_dblog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
