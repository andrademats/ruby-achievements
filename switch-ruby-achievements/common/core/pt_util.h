#pragma once
#include <stddef.h>
#include <stdint.h>

// Relógio de parede (segundos Unix, relógio do usuário). Exige timeInitialize().
int64_t pt_now(void);

// Migra a pasta do nome antigo (pt_migrate_legacy) e cria PT_DIR e as subpastas
// se não existirem. Primeira chamada do sysmodule e do app, antes de log e banco.
void pt_ensure_dirs(void);

// Rebranding Switch Playtime -> Ruby Achievements: move cada item de PT_LEGACY_DIR
// para PT_DIR (pastas que existem nos dois são mescladas item por item; o que já
// existe no destino nunca é sobrescrito: fica na pasta antiga, ou é apagado se for uma
// cópia idêntica, como os arquivos que o dist já traz), renomeia o banco
// playtime.db (e o -journal) para PT_DB_PATH se o novo ainda não existir, apaga o
// playtime.nro velho e o .ovl velho (só se o novo estiver instalado) e remove a pasta
// antiga quando ela fica vazia. Sem a pasta antiga não faz nada. Resultado em
// PT_MIGRATION_LOG_PATH. Retorna quantos itens ficaram na pasta antiga.
int pt_migrate_legacy(void);

// "123h 45m" / "12m" / "0m".
void pt_format_duration(char* out, size_t cap, int64_t seconds);

// Lê até cap-1 bytes e termina com '\0'. Retorna bytes lidos ou -1.
long pt_read_file(const char* path, char* buf, size_t cap);

// Grava em arquivo temporário e troca pelo destino, para leitores nunca verem
// um arquivo pela metade. Retorna 0 em sucesso.
int pt_write_file_atomic(const char* path, const char* data, size_t len);

// Remove espaços nas pontas, in-place. Retorna o início.
char* pt_trim(char* s);
