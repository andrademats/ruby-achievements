#include "pt_db.h"
#include "pt_lock.h"
#include "pt_paths.h"
#include "pt_vfs.h"

#include <switch.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Gerado no build a partir de ../schema.sql (literal de string bruta).
static const char* const kSchemaSql =
#include "schema_sql.inc"
    ;

constexpr int kMaxRecoveries = 3;  // por processo: evita recriar o banco em laço

static bool g_schema_ready = false;  // schema aplicado e banco verificado neste processo
static bool g_ever_ok = false;
static int g_failures = 0;           // falhas seguidas de pt_db_open
static int g_recoveries = 0;
static bool g_verbose = false;
static void (*g_log_fn)(const char*) = NULL;
static char g_last_error[192] = "";

// ---- log -------------------------------------------------------------------

void pt_dblog(const char* fmt, ...) {
    if (!g_log_fn) return;
    char line[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_log_fn(line);
}

// Passo a passo: só na 1ª abertura do processo e a cada 60 falhas seguidas.
#define VLOG(...)                            \
    do {                                     \
        if (g_verbose) pt_dblog(__VA_ARGS__); \
    } while (0)

// Mensagens internas do SQLite (ex.: "database corruption at line N").
static void sqlite_log(void*, int code, const char* msg) {
    int primary = code & 0xff;
    bool serious = primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB || primary == SQLITE_IOERR ||
                   primary == SQLITE_CANTOPEN || primary == SQLITE_FULL;
    if (serious || g_verbose) pt_dblog("[sqlite] (%d) %s", code, msg);
}

void pt_db_set_log(void (*fn)(const char*)) {
    g_log_fn = fn;
    sqlite3_config(SQLITE_CONFIG_LOG, sqlite_log, NULL);  // precisa vir antes do sqlite3_initialize
}

const char* pt_db_last_error(void) { return g_last_error; }

static void set_error(const char* step, const char* msg, int code) {
    snprintf(g_last_error, sizeof(g_last_error), "%s: %s (codigo SQLite %d, libnx 0x%X)", step, msg, code,
             pt_vfs_last_result());
}

static void set_db_error(sqlite3* db, const char* step) {
    if (db)
        set_error(step, sqlite3_errmsg(db), sqlite3_extended_errcode(db));
    else
        set_error(step, "sem memoria", SQLITE_NOMEM);
}

static bool is_malformed(int rc) {
    return (rc & 0xff) == SQLITE_CORRUPT || (rc & 0xff) == SQLITE_NOTADB;
}

// ---- etapas ----------------------------------------------------------------

static int exec_logged(sqlite3* db, const char* sql) {
    char* err = NULL;
    VLOG("[db]   -> %s", sql);
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    VLOG("[db]   <- %s: rc=%d%s%s", sql, rc, err ? " " : "", err ? err : "");
    sqlite3_free(err);
    return rc;
}

// Primeira linha útil de um comando do schema (sem comentários), para o log.
static void describe(const char* sql, char* out, size_t cap) {
    for (;;) {
        while (*sql == ' ' || *sql == '\n' || *sql == '\r' || *sql == '\t') sql++;
        if (sql[0] != '-' || sql[1] != '-') break;
        while (*sql && *sql != '\n') sql++;
    }
    size_t n = 0;
    while (sql[n] && sql[n] != '\n' && sql[n] != '(' && n + 1 < cap) n++;
    while (n > 0 && sql[n - 1] == ' ') n--;
    memcpy(out, sql, n);
    out[n] = '\0';
}

// Colunas acrescentadas depois que bancos já existiam no SD: o CREATE TABLE IF NOT
// EXISTS não mexe em tabela pronta, então cada uma entra por ALTER TABLE ADD COLUMN
// (rápido e seguro no SQLite: as linhas antigas leem NULL), só se ainda faltar.
struct AddedColumn {
    const char* table;
    const char* column;
    const char* type;
};
static const AddedColumn kAddedColumns[] = {
    {"achievements", "rarity", "TEXT"},  // 2026-10-21: raridade das conquistas
};

static bool has_column(sqlite3* db, const char* table, const char* column) {
    char sql[96];
    snprintf(sql, sizeof(sql), "SELECT 1 FROM pragma_table_info('%s') WHERE name='%s'", table, column);
    return pt_db_scalar(db, sql, 0, 0, 0) == 1;
}

static int add_missing_columns(sqlite3* db) {
    for (const AddedColumn& c : kAddedColumns) {
        if (has_column(db, c.table, c.column)) continue;
        char sql[128];
        snprintf(sql, sizeof(sql), "ALTER TABLE %s ADD COLUMN %s %s", c.table, c.column, c.type);
        int rc = exec_logged(db, sql);
        VLOG("[db]   coluna nova %s.%s: rc=%d", c.table, c.column, rc);
        if (rc != SQLITE_OK) return rc;
    }
    return SQLITE_OK;
}

// Aplica o schema numa transação explícita, comando a comando.
static int apply_schema(sqlite3* db) {
    int rc = exec_logged(db, "BEGIN IMMEDIATE");
    if (rc != SQLITE_OK) return rc;

    const char* sql = kSchemaSql;
    int n = 0;
    while (rc == SQLITE_OK && *sql) {
        sqlite3_stmt* st = NULL;
        const char* tail = sql;
        rc = sqlite3_prepare_v2(db, sql, -1, &st, &tail);
        if (rc != SQLITE_OK) {
            VLOG("[db]   schema: erro ao preparar: rc=%d %s", rc, sqlite3_errmsg(db));
            break;
        }
        if (!st) break;  // só comentários/espaços até o fim
        char what[64];
        describe(sqlite3_sql(st), what, sizeof(what));
        sqlite3_step(st);
        rc = sqlite3_finalize(st);  // código do step, se ele falhou
        VLOG("[db]   schema #%d %s: rc=%d%s%s", ++n, what, rc, rc ? " " : "", rc ? sqlite3_errmsg(db) : "");
        if (tail == sql) break;
        sql = tail;
    }
    if (rc == SQLITE_OK) rc = add_missing_columns(db);

    if (rc == SQLITE_OK) {
        rc = exec_logged(db, "COMMIT");
    } else {
        int saved = rc;
        exec_logged(db, "ROLLBACK");
        rc = saved;
    }
    return rc;
}

// Confere o banco recém-aberto: quick_check percorre todas as páginas.
static int verify_db(sqlite3* db) {
    VLOG("[db]   paginas=%lld tamanho_pagina=%lld objetos_no_schema=%lld",
         (long long)pt_db_scalar(db, "PRAGMA page_count", 0, 0, -1),
         (long long)pt_db_scalar(db, "PRAGMA page_size", 0, 0, -1),
         (long long)pt_db_scalar(db, "SELECT COUNT(*) FROM sqlite_schema", 0, 0, -1));
    sqlite3_stmt* st = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &st, NULL);
    if (rc != SQLITE_OK) return rc;
    char first[128] = "";
    int step = sqlite3_step(st);
    if (step == SQLITE_ROW) {
        const char* t = (const char*)sqlite3_column_text(st, 0);
        snprintf(first, sizeof(first), "%s", t ? t : "");
    }
    rc = sqlite3_finalize(st);
    VLOG("[db]   quick_check: rc=%d resultado=\"%s\"", rc, first);
    if (rc != SQLITE_OK) return rc;
    if (strcmp(first, "ok") != 0) {
        pt_dblog("[db] quick_check reprovou o banco: %s", first);
        return SQLITE_CORRUPT;
    }
    return SQLITE_OK;
}

// Etapas 2 a 6. Em erro fecha a conexão, preenche g_last_error e retorna o código.
static int open_and_prepare(sqlite3** out) {
    *out = NULL;

    int64_t size = -1, jsize = -1;
    uint8_t head[16];
    size_t got = 0, jgot = 0;
    bool exists = pt_vfs_probe(PT_DB_PATH, &size, head, sizeof(head), &got);
    bool jexists = pt_vfs_probe(PT_DB_PATH "-journal", &jsize, head + 0, 0, &jgot);
    VLOG("[db] etapa 2/6 arquivo: %s %s (tamanho %lld); journal %s (tamanho %lld)", PT_DB_PATH,
         exists ? "existe" : "nao existe, sera criado vazio", (long long)size, jexists ? "existe" : "nao existe",
         (long long)jsize);
    if (exists && size > 0 && (got < 16 || memcmp(head, "SQLite format 3", 16) != 0)) {
        // Arquivo com conteúdo que não começa pelo cabeçalho do SQLite.
        pt_dblog("[db] cabecalho invalido em %s (tamanho %lld, %u bytes lidos)", PT_DB_PATH, (long long)size,
                 (unsigned)got);
        set_error("arquivo", "cabecalho do SQLite ausente", SQLITE_NOTADB);
        return SQLITE_NOTADB;
    }
    if (exists && size == 0) VLOG("[db]   arquivo vazio: o SQLite vai inicializar o banco");

    VLOG("[db] etapa 3/6 sqlite3_open_v2(%s, vfs=%s)", PT_DB_PATH, PT_VFS_NAME);
    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(PT_DB_PATH, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, PT_VFS_NAME);
    VLOG("[db]   <- open: rc=%d", rc);
    if (rc != SQLITE_OK) {
        set_db_error(db, "abrir");
        sqlite3_close(db);
        return rc;
    }
    sqlite3_busy_timeout(db, 0);

    VLOG("[db] etapa 4/6 pragmas");
    // journal PERSIST: o arquivo -journal é reaproveitado (cabeçalho zerado) em
    // vez de criado e apagado a cada transação, as operações mais lentas no
    // FS do cartão SD. synchronous segue FULL: com uma transação por gravação
    // são poucos syncs, e um banco corrompido perderia todo o histórico.
    const char* pragmas[] = {"PRAGMA foreign_keys=ON", "PRAGMA cache_size=-128", "PRAGMA temp_store=MEMORY",
                             "PRAGMA journal_mode=PERSIST", "PRAGMA journal_size_limit=1048576"};
    for (const char* p : pragmas) {
        rc = exec_logged(db, p);
        if (rc != SQLITE_OK) {
            set_db_error(db, p);
            sqlite3_close(db);
            return rc;
        }
    }

    if (!g_schema_ready) {
        VLOG("[db] etapa 5/6 schema (BEGIN IMMEDIATE ... COMMIT)");
        rc = apply_schema(db);
        if (rc != SQLITE_OK) {
            set_db_error(db, "schema");
            sqlite3_close(db);
            return rc;
        }
        VLOG("[db] etapa 6/6 verificacao (quick_check)");
        rc = verify_db(db);
        if (rc != SQLITE_OK) {
            set_error("verificacao", sqlite3_errstr(rc), rc);
            sqlite3_close(db);
            return rc;
        }
        g_schema_ready = true;
    }
    *out = db;
    return SQLITE_OK;
}

// ---- API -------------------------------------------------------------------

sqlite3* pt_db_open(int timeout_ms) {
    g_verbose = g_ever_ok ? (g_failures % 60 == 1) : (g_failures % 60 == 0);
    pt_vfs_set_trace(g_verbose);
    VLOG("[db] ---- abrindo banco (SQLite %s, VFS %s, tentativa %d) ----", sqlite3_libversion(), PT_VFS_NAME,
         g_failures + 1);

    VLOG("[db] etapa 1/6 lock: mkdir %s", PT_LOCK_DIR);
    if (!pt_lock_acquire(timeout_ms)) {
        snprintf(g_last_error, sizeof(g_last_error), "lock: %s indisponivel (mkdir errno %d)", PT_LOCK_DIR,
                 pt_lock_last_errno());
        VLOG("[db] FALHOU %s", g_last_error);
        pt_vfs_set_trace(false);
        g_failures++;
        return NULL;
    }
    VLOG("[db]   lock obtido");

    sqlite3* db = NULL;
    int rc = open_and_prepare(&db);
    if (rc != SQLITE_OK) VLOG("[db] FALHOU %s", g_last_error);

    if (is_malformed(rc)) {
        if (g_recoveries < kMaxRecoveries) {
            g_recoveries++;
            pt_dblog("[db] RECUPERACAO %d/%d: banco malformado (%s). Movendo para %s.corrupt e recriando do zero.",
                     g_recoveries, kMaxRecoveries, g_last_error, PT_DB_PATH);
            if (pt_vfs_discard(PT_DB_PATH) == 0) {
                g_schema_ready = false;
                bool was = g_verbose;
                g_verbose = true;  // a recriação sempre sai completa no log
                pt_vfs_set_trace(true);
                rc = open_and_prepare(&db);
                g_verbose = was;
                pt_dblog("[db] RECUPERACAO %s", rc == SQLITE_OK ? "concluida: banco novo valido" : g_last_error);
            } else {
                pt_dblog("[db] RECUPERACAO falhou: nao foi possivel liberar %s", PT_DB_PATH);
            }
        } else {
            VLOG("[db] limite de %d recuperacoes neste boot atingido; nada foi apagado", kMaxRecoveries);
        }
    }
    pt_vfs_set_trace(false);

    if (rc != SQLITE_OK) {
        pt_lock_release();
        g_failures++;
        return NULL;
    }
    VLOG("[db] banco pronto");
    g_failures = 0;
    g_ever_ok = true;
    g_last_error[0] = '\0';
    return db;
}

void pt_db_close(sqlite3* db) {
    if (db) sqlite3_close(db);
    pt_lock_release();
}

void pt_make_sync_id(char out[33]) {
    u8 raw[16];
    randomGet(raw, sizeof(raw));
    for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", raw[i]);
    out[32] = '\0';
}

int64_t pt_db_scalar(sqlite3* db, const char* sql, int64_t a, int64_t b, int64_t def) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return def;
    int params = sqlite3_bind_parameter_count(st);
    if (params >= 1) sqlite3_bind_int64(st, 1, a);
    if (params >= 2) sqlite3_bind_int64(st, 2, b);
    int64_t v = def;
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL)
        v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

int64_t pt_db_shared_user(sqlite3* db, int64_t now) {
    int64_t id = pt_db_scalar(db, "SELECT id FROM users WHERE is_shared=1 ORDER BY id LIMIT 1", 0, 0, 0);
    if (id > 0) return id;

    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO users(account_uid,nickname,is_shared,created_at,sync_id) "
                           "VALUES(NULL,'Compartilhado',1,?,?)",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    char sync[33];
    pt_make_sync_id(sync);
    sqlite3_bind_int64(st, 1, now);
    sqlite3_bind_text(st, 2, sync, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? sqlite3_last_insert_rowid(db) : 0;
}

int64_t pt_db_resolve_game(sqlite3* db, int64_t game_id) {
    // Limite evita laço infinito caso um ciclo seja criado por engano.
    for (int hop = 0; hop < 16 && game_id > 0; hop++) {
        sqlite3_stmt* st = NULL;
        if (sqlite3_prepare_v2(db, "SELECT status FROM games WHERE id=?", -1, &st, NULL) != SQLITE_OK)
            return game_id;
        sqlite3_bind_int64(st, 1, game_id);
        int64_t next = 0;
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char* status = (const char*)sqlite3_column_text(st, 0);
            if (status && strncmp(status, "MERGED:", 7) == 0) next = strtoll(status + 7, NULL, 10);
        }
        sqlite3_finalize(st);
        if (next <= 0 || next == game_id) return game_id;
        game_id = next;
    }
    return game_id;
}

int pt_db_recalc_game(sqlite3* db, int64_t game_id, int64_t now) {
    sqlite3_stmt* st = NULL;
    const char* sql =
        "UPDATE games SET "
        " total_playtime=(SELECT COALESCE(SUM(duration),0) FROM sessions WHERE game_id=?1),"
        " first_played=(SELECT MIN(start_time) FROM sessions WHERE game_id=?1),"
        " last_played=(SELECT MAX(COALESCE(end_time,start_time+duration)) FROM sessions WHERE game_id=?1),"
        " updated_at=?2 "
        "WHERE id=?1";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, game_id);
    sqlite3_bind_int64(st, 2, now);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int exec2(sqlite3* db, const char* sql, int64_t a, int64_t b) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, a);
    sqlite3_bind_int64(st, 2, b);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int pt_db_close_open_sessions(sqlite3* db, int64_t session_id, int64_t keep) {
    int rc = exec2(db,
                   "UPDATE sessions SET end_time = start_time + duration "
                   "WHERE end_time IS NULL AND (?1 = 0 OR id = ?1) AND id <> ?2",
                   session_id, keep);
    return rc == 0 ? sqlite3_changes(db) : -1;
}

int pt_db_merge_games(sqlite3* db, int64_t from, int64_t into, int64_t now) {
    into = pt_db_resolve_game(db, into);
    if (from <= 0 || into <= 0 || from == into) return -1;

    char status[32];
    snprintf(status, sizeof(status), "MERGED:%lld", (long long)into);

    // SAVEPOINT: funciona sozinho e dentro da transação do flush do sysmodule.
    if (sqlite3_exec(db, "SAVEPOINT merge_games", NULL, NULL, NULL) != SQLITE_OK) return -1;
    int rc = exec2(db, "UPDATE sessions SET game_id=?2 WHERE game_id=?1", from, into);
    if (rc == 0) rc = exec2(db, "UPDATE achievements SET game_id=?2 WHERE game_id=?1", from, into);

    sqlite3_stmt* st = NULL;
    if (rc == 0 &&
        sqlite3_prepare_v2(db, "UPDATE games SET status=?2, updated_at=?3 WHERE id=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, from);
        sqlite3_bind_text(st, 2, status, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, now);
        rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
        sqlite3_finalize(st);
    } else {
        rc = -1;
    }
    // Herda a capa e o rom_path quando o destino não tem.
    if (rc == 0)
        rc = exec2(db,
                   "UPDATE games SET "
                   " cover_path=COALESCE(cover_path,(SELECT cover_path FROM games WHERE id=?1)),"
                   " rom_path=COALESCE(rom_path,(SELECT rom_path FROM games WHERE id=?1)) "
                   "WHERE id=?2",
                   from, into);
    if (rc == 0) rc = pt_db_recalc_game(db, from, now);
    if (rc == 0) rc = pt_db_recalc_game(db, into, now);

    if (rc != 0) sqlite3_exec(db, "ROLLBACK TO merge_games", NULL, NULL, NULL);
    sqlite3_exec(db, "RELEASE merge_games", NULL, NULL, NULL);
    return rc;
}

int64_t pt_db_account_user(sqlite3* db, const char* uid_str, const char* nickname, int64_t now, bool* changed) {
    *changed = false;
    int64_t id = 0;
    char old_nick[64] = "";
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT id, nickname FROM users WHERE account_uid=?", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, uid_str, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        id = sqlite3_column_int64(st, 0);
        const char* n = (const char*)sqlite3_column_text(st, 1);
        if (n) snprintf(old_nick, sizeof(old_nick), "%s", n);
    }
    sqlite3_finalize(st);

    if (id > 0) {
        if (strcmp(old_nick, nickname) != 0 &&
            sqlite3_prepare_v2(db, "UPDATE users SET nickname=? WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, nickname, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, id);
            *changed = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
        }
        return id;
    }

    if (sqlite3_prepare_v2(db, "INSERT INTO users(account_uid,nickname,is_shared,created_at,sync_id) VALUES(?,?,0,?,?)",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    char sync[33];
    pt_make_sync_id(sync);
    sqlite3_bind_text(st, 1, uid_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, nickname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_text(st, 4, sync, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return 0;
    *changed = true;
    return sqlite3_last_insert_rowid(db);
}

bool pt_db_name_suspect(const char* name, const char* tid_str) {
    if (!name || !name[0]) return true;
    if (tid_str && strcmp(name, tid_str) == 0) return true;  // NACP não lido quando o jogo foi criado
    bool hangul = false, han_kana = false;
    const unsigned char* p = (const unsigned char*)name;
    while (*p) {
        uint32_t cp;
        int extra;
        if (*p < 0x80) cp = *p, extra = 0;
        else if ((*p & 0xE0) == 0xC0) cp = *p & 0x1F, extra = 1;
        else if ((*p & 0xF0) == 0xE0) cp = *p & 0x0F, extra = 2;
        else if ((*p & 0xF8) == 0xF0) cp = *p & 0x07, extra = 3;
        else return true;  // UTF-8 inválido
        p++;
        for (int i = 0; i < extra; i++, p++) {
            if ((*p & 0xC0) != 0x80) return true;
            cp = (cp << 6) | (*p & 0x3F);
        }
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return true;   // controle
        if (cp == 0xFFFD || (cp >= 0xE000 && cp <= 0xF8FF)) return true;  // substituição / uso privado
        if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F))
            hangul = true;
        if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF))
            han_kana = true;
    }
    // Hangul junto com kanji/kana não aparece em nomes reais: são bytes soltos
    // lidos como texto (ex.: "E 혁J 1 斧").
    return hangul && han_kana;
}

// Nome gravado ilegível ou só o Title ID: lê o NACP de novo e corrige.
static void repair_title_name(sqlite3* db, int64_t id, uint64_t tid, const char* tid_str, int64_t now,
                              PtTitleReader read) {
    if (!read) return;
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT display_name FROM games WHERE id=?", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int64(st, 1, id);
    char old_name[256] = "";
    bool found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        const char* n = (const char*)sqlite3_column_text(st, 0);
        snprintf(old_name, sizeof(old_name), "%s", n ? n : "");
    }
    sqlite3_finalize(st);
    if (!found || !pt_db_name_suspect(old_name, tid_str)) return;

    char name[256] = "", icon[128] = "";
    if (!read(tid, name, sizeof(name), icon, sizeof(icon)) || pt_db_name_suspect(name, tid_str)) return;
    if (sqlite3_prepare_v2(db,
                           "UPDATE games SET display_name=?, cover_path=COALESCE(cover_path,?), updated_at=? WHERE id=?",
                           -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    if (icon[0]) sqlite3_bind_text(st, 2, icon, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, 2);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_int64(st, 4, id);
    if (sqlite3_step(st) == SQLITE_DONE) pt_dblog("[db] nome de %s corrigido: \"%s\" -> \"%s\"", tid_str, old_name, name);
    sqlite3_finalize(st);
}

int64_t pt_db_title_game(sqlite3* db, uint64_t tid, int64_t now, PtTitleReader read) {
    char tid_str[17];
    snprintf(tid_str, sizeof(tid_str), "%016lX", tid);

    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT id FROM games WHERE title_id=? AND source_type IN ('NATIVE','FORWARDER') "
                           "ORDER BY id LIMIT 1",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, tid_str, -1, SQLITE_TRANSIENT);
    int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (id > 0) {
        id = pt_db_resolve_game(db, id);
        // Só o registro do próprio título (o destino de uma unificação é outro jogo).
        sqlite3_stmt* t = NULL;
        bool own = false;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM games WHERE id=? AND title_id=?", -1, &t, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(t, 1, id);
            sqlite3_bind_text(t, 2, tid_str, -1, SQLITE_TRANSIENT);
            own = sqlite3_step(t) == SQLITE_ROW;
            sqlite3_finalize(t);
        }
        if (own) repair_title_name(db, id, tid, tid_str, now, read);
        return id;
    }

    char name[256] = "", icon[128] = "";
    if (!read || !read(tid, name, sizeof(name), icon, sizeof(icon)) || !name[0])
        snprintf(name, sizeof(name), "%s", tid_str);

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO games(title_id,display_name,rom_path,source_type,status,cover_path,"
                           "total_playtime,created_at,updated_at,sync_id) VALUES(?,?,NULL,?,'INSTALLED',?,0,?,?,?)",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    char sync[33];
    pt_make_sync_id(sync);
    sqlite3_bind_text(st, 1, tid_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
    // Forwarders (NSP -> NRO) usam Title IDs com prefixo 0x05.
    sqlite3_bind_text(st, 3, (tid >> 56) == 0x05 ? "FORWARDER" : "NATIVE", -1, SQLITE_STATIC);
    if (icon[0]) sqlite3_bind_text(st, 4, icon, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, 4);
    sqlite3_bind_int64(st, 5, now);
    sqlite3_bind_int64(st, 6, now);
    sqlite3_bind_text(st, 7, sync, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return 0;
    int64_t game = sqlite3_last_insert_rowid(db);
    if (pt_db_is_utility_name(name)) pt_db_set_tracking(db, game, false, "auto", now);
    return game;
}

// --- Tracking por jogo ---------------------------------------------------------
static bool name_has(const char* name, const char* word) {
    size_t n = strlen(word);
    for (const char* p = name; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == word[i]) i++;
        if (i == n) return true;
    }
    return false;
}

static bool name_is(const char* name, const char* word) {
    size_t i = 0;
    for (; name[i] && word[i]; i++)
        if (tolower((unsigned char)name[i]) != word[i]) return false;
    return !name[i] && !word[i];
}

bool pt_db_is_utility_name(const char* name) {
    if (!name) return false;
    // "Ruby Achievements" e os nomes antigos (Switch Playtime, até 2026-10-22).
    return name_has(name, "sphaira") || name_is(name, "ruby achievements") || name_is(name, "switch playtime") ||
           name_is(name, "playtime") || name_is(name, "switch playtime tracker");
}

bool pt_db_tracking_disabled(sqlite3* db, int64_t game_id) {
    return pt_db_scalar(db, "SELECT disabled FROM tracking_disabled WHERE game_id=?", game_id, 0, 0) == 1;
}

int pt_db_set_tracking(sqlite3* db, int64_t game_id, bool enabled, const char* reason, int64_t now) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO tracking_disabled(game_id,disabled,reason,updated_at) VALUES(?,?,?,?) "
                           "ON CONFLICT(game_id) DO UPDATE SET disabled=excluded.disabled, reason=excluded.reason, "
                           "updated_at=excluded.updated_at",
                           -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(st, 1, game_id);
    sqlite3_bind_int(st, 2, enabled ? 0 : 1);
    sqlite3_bind_text(st, 3, reason ? reason : "user", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, now);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int pt_db_apply_utility_filter(sqlite3* db, int64_t now) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT id, display_name FROM games WHERE status NOT LIKE 'MERGED:%' "
                           "AND id NOT IN (SELECT game_id FROM tracking_disabled)",
                           -1, &st, NULL) != SQLITE_OK)
        return -1;
    int64_t ids[64];
    int n = 0;
    while (n < 64 && sqlite3_step(st) == SQLITE_ROW)
        if (pt_db_is_utility_name((const char*)sqlite3_column_text(st, 1))) ids[n++] = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    for (int i = 0; i < n; i++) {
        if (pt_db_set_tracking(db, ids[i], false, "auto", now) != 0) return -1;
        if (exec2(db, "DELETE FROM sessions WHERE game_id=?1 AND ?2 = ?2", ids[i], 0) != 0) return -1;
        pt_db_recalc_game(db, ids[i], now);
    }
    return n;
}
