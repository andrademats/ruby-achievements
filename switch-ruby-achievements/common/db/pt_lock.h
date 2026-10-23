#pragma once
#include <stdbool.h>

// Lock entre processos (sysmodule x app NRO) para acesso ao banco.
// O SQLite roda com a VFS "unix-none" (o FS do Switch não tem fcntl locks),
// então todo acesso ao banco precisa estar dentro deste lock.
// Implementado com mkdir(), que é atômico no FS do Switch.
// Locks abandonados (processo morto) expiram após PT_LOCK_STALE_SECONDS.
#define PT_LOCK_STALE_SECONDS 20

bool pt_lock_acquire(int timeout_ms);
void pt_lock_release(void);
// errno do último mkdir() que falhou (EEXIST = lock ocupado).
int pt_lock_last_errno(void);
