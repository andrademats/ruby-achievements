#pragma once
// Configuração de compilação do SQLite para o Switch (incluída via
// -DSQLITE_CUSTOM_INCLUDE=pt_sqlite_config.h). Foco: pouca memória e nenhuma
// dependência de recursos POSIX ausentes no libnx (mmap, WAL/shm, locks).
// SQLITE_OS_OTHER: sem a VFS unix. A VFS "switch" (common/db/pt_vfs.cpp) usa a
// API de FS do libnx direto, sem a emulação POSIX da newlib/fsdev.
#define SQLITE_OS_OTHER 1
#define SQLITE_THREADSAFE 0
#define SQLITE_OMIT_WAL 1
#define SQLITE_OMIT_LOAD_EXTENSION 1
#define SQLITE_OMIT_SHARED_CACHE 1
#define SQLITE_OMIT_DEPRECATED 1
#define SQLITE_OMIT_PROGRESS_CALLBACK 1
#define SQLITE_MAX_MMAP_SIZE 0
#define SQLITE_DEFAULT_MEMSTATUS 0
#define SQLITE_DEFAULT_CACHE_SIZE -128
#define SQLITE_DEFAULT_LOOKASIDE 0,0
#define SQLITE_TEMP_STORE 3
#define SQLITE_DQS 0
#define SQLITE_LIKE_DOESNT_MATCH_BLOBS 1
#define SQLITE_DEFAULT_FOREIGN_KEYS 1
