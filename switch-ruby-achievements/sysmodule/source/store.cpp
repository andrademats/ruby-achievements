#include "store.h"
#include "detect.h"
#include "pt_paths.h"
#include "pt_title.h"
#include "pt_util.h"

#include "pt_log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static sqlite3_stmt* prep(sqlite3* db, const char* sql) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return NULL;
    return st;
}

static int64_t step_insert(sqlite3* db, sqlite3_stmt* st) {
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? sqlite3_last_insert_rowid(db) : 0;
}

int64_t store_resolve_user(sqlite3* db, const AccountUid* uid, int64_t now, bool* created) {
    *created = false;
    if (!uid) {
        // Sem conta detectada: último usuário usado, senão "Compartilhado".
        int64_t last = pt_db_scalar(db, "SELECT user_id FROM sessions ORDER BY start_time DESC, id DESC LIMIT 1", 0, 0, 0);
        if (last > 0) return last;
        int64_t before = pt_db_scalar(db, "SELECT COUNT(*) FROM users", 0, 0, 0);
        int64_t shared = pt_db_shared_user(db, now);
        *created = pt_db_scalar(db, "SELECT COUNT(*) FROM users", 0, 0, 0) != before;
        return shared;
    }

    char uid_str[33];
    pt_uid_to_string(*uid, uid_str);
    char nick[64];
    if (!pt_account_nickname(*uid, nick, sizeof(nick))) snprintf(nick, sizeof(nick), "Usuário %.8s", uid_str);
    return pt_db_account_user(db, uid_str, nick, now, created);
}

int64_t store_game_for_title(sqlite3* db, u64 tid, int64_t now) {
    // NACP lido com a proteção contra travamento (detect_title_info).
    return pt_db_title_game(db, tid, now, detect_title_info);
}

// Nome da ROM sem pasta nem extensão, em minúsculas: "/roms/n64/Mario Kart 64 (USA).z64"
// e "nome:Mario Kart 64 (USA)" dão "mario kart 64 (usa)".
static void rom_stem(const char* path, char* out, size_t cap) {
    bool by_name = strncmp(path, "nome:", 5) == 0;
    const char* b = path + 5;
    if (!by_name) {
        const char* slash = strrchr(path, '/');
        b = slash ? slash + 1 : path;
    }
    size_t n = strlen(b);
    const char* dot = by_name ? NULL : strrchr(b, '.');
    if (dot && dot > b) n = (size_t)(dot - b);
    if (n >= cap) n = cap - 1;
    for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)b[i]);
    out[n] = '\0';
}

// Jogo de uma ROM. Inclui registros unificados (MERGED): o rom_path deles
// continua apontando para o jogo de destino, então a unificação é permanente.
// Quando um dos lados só tem o nome ("nome:..."), compara pelo nome da ROM.
// Retorna o registro achado (sem seguir MERGED); 0 = nenhum.
static int64_t game_by_rom(sqlite3* db, const char* rom_path, int64_t exclude) {
    sqlite3_stmt* st = prep(db,
                            "SELECT id FROM games WHERE rom_path=? AND id<>? "
                            "ORDER BY (status LIKE 'MERGED:%'), (source_type='FORWARDER') DESC, id LIMIT 1");
    if (!st) return 0;
    sqlite3_bind_text(st, 1, rom_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, exclude);
    int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (id > 0) return id;

    char want[256], have[256];
    rom_stem(rom_path, want, sizeof(want));
    if (!want[0]) return 0;
    bool want_by_name = strncmp(rom_path, "nome:", 5) == 0;
    st = prep(db,
              "SELECT id, rom_path FROM games WHERE rom_path IS NOT NULL AND id<>? "
              "ORDER BY (status LIKE 'MERGED:%'), (source_type='FORWARDER') DESC, id");
    if (!st) return 0;
    sqlite3_bind_int64(st, 1, exclude);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* p = (const char*)sqlite3_column_text(st, 1);
        if (!p || (!want_by_name && strncmp(p, "nome:", 5) != 0)) continue;  // dois arquivos: só caminho igual
        rom_stem(p, have, sizeof(have));
        if (strcmp(want, have) == 0) {
            id = sqlite3_column_int64(st, 0);
            break;
        }
    }
    sqlite3_finalize(st);
    return id;
}

int64_t store_game_for_rom(sqlite3* db, const char* rom_path, const char* label, int64_t now) {
    int64_t id = game_by_rom(db, rom_path, 0);
    if (id > 0) {
        int64_t game = pt_db_resolve_game(db, id);
        if (game != id) pt_log("rom: \"%s\" ja unificada ao jogo %lld", rom_path, (long long)game);
        return game;
    }

    // Capas de ROM são sempre manuais: cover_path fica NULL.
    sqlite3_stmt* st = prep(db,
                            "INSERT INTO games(title_id,display_name,rom_path,source_type,status,cover_path,"
                            "total_playtime,created_at,updated_at,sync_id) "
                            "VALUES(NULL,?,?,'EMULATOR_ROM','INSTALLED',NULL,0,?,?,?)");
    if (!st) return 0;
    char sync[33];
    pt_make_sync_id(sync);
    sqlite3_bind_text(st, 1, label[0] ? label : rom_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, rom_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_int64(st, 4, now);
    sqlite3_bind_text(st, 5, sync, -1, SQLITE_TRANSIENT);
    return step_insert(db, st);
}

bool store_forwarder_learn_rom(sqlite3* db, int64_t game_id, const char* rom_path, int64_t now) {
    sqlite3_stmt* st = prep(db, "SELECT rom_path FROM games WHERE id=?");
    if (!st) return true;
    sqlite3_bind_int64(st, 1, game_id);
    char current[512] = "";
    bool has = false;
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
        snprintf(current, sizeof(current), "%s", (const char*)sqlite3_column_text(st, 0));
        has = current[0] != '\0';
    }
    sqlite3_finalize(st);

    if (has) return strcmp(current, rom_path) == 0;

    st = prep(db, "UPDATE games SET rom_path=?, updated_at=? WHERE id=?");
    if (!st) return true;
    sqlite3_bind_text(st, 1, rom_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, now);
    sqlite3_bind_int64(st, 3, game_id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    // Unificação automática: mesmo rom_path => mesmo jogo.
    int64_t other = pt_db_resolve_game(db, game_by_rom(db, rom_path, game_id));
    if (other > 0 && other != game_id) pt_db_merge_games(db, other, game_id, now);
    return true;
}

int64_t store_insert_session(sqlite3* db, int64_t game_id, int64_t user_id, int64_t start, int64_t now) {
    sqlite3_stmt* st = prep(db,
                            "INSERT INTO sessions(game_id,user_id,start_time,end_time,duration,created_at,sync_id,synced) "
                            "VALUES(?,?,?,NULL,0,?,?,0)");
    if (!st) return 0;
    char sync[33];
    pt_make_sync_id(sync);
    sqlite3_bind_int64(st, 1, game_id);
    sqlite3_bind_int64(st, 2, user_id);
    sqlite3_bind_int64(st, 3, start);
    sqlite3_bind_int64(st, 4, now);
    sqlite3_bind_text(st, 5, sync, -1, SQLITE_TRANSIENT);
    return step_insert(db, st);
}

int store_update_session(sqlite3* db, int64_t session_id, int64_t user_id, int64_t duration, int64_t end_time) {
    sqlite3_stmt* st = prep(db, "UPDATE sessions SET user_id=?, duration=?, end_time=? WHERE id=?");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_int64(st, 2, duration);
    if (end_time > 0) sqlite3_bind_int64(st, 3, end_time);
    else sqlite3_bind_null(st, 3);
    sqlite3_bind_int64(st, 4, session_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int store_delete_session(sqlite3* db, int64_t session_id) {
    sqlite3_stmt* st = prep(db, "DELETE FROM sessions WHERE id=?");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, session_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int store_move_session(sqlite3* db, int64_t session_id, int64_t game_id) {
    sqlite3_stmt* st = prep(db, "UPDATE sessions SET game_id=? WHERE id=?");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, game_id);
    sqlite3_bind_int64(st, 2, session_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int64_t store_session_game(sqlite3* db, int64_t session_id) {
    return pt_db_scalar(db, "SELECT game_id FROM sessions WHERE id=?", session_id, 0, 0);
}

int64_t store_profile_total(sqlite3* db, int64_t game_id, int64_t profile, int64_t exclude_session) {
    if (profile > 0) {
        sqlite3_stmt* st = prep(db,
                                "SELECT COALESCE(SUM(duration),0) FROM sessions "
                                "WHERE game_id=? AND user_id=? AND id<>?");
        if (!st) return 0;
        sqlite3_bind_int64(st, 1, game_id);
        sqlite3_bind_int64(st, 2, profile);
        sqlite3_bind_int64(st, 3, exclude_session);
        int64_t v = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
        sqlite3_finalize(st);
        return v;
    }
    return pt_db_scalar(db, "SELECT COALESCE(SUM(duration),0) FROM sessions WHERE game_id=? AND id<>?",
                        game_id, exclude_session, 0);
}

int64_t store_last_played_game(sqlite3* db) {
    return pt_db_scalar(db,
                        "SELECT id FROM games WHERE status NOT LIKE 'MERGED:%' AND last_played IS NOT NULL "
                        "ORDER BY last_played DESC LIMIT 1",
                        0, 0, 0);
}

int64_t store_find_title(sqlite3* db, u64 tid) {
    char hex[17];
    snprintf(hex, sizeof(hex), "%016lX", tid);
    sqlite3_stmt* st = prep(db,
                            "SELECT id FROM games WHERE title_id=? AND source_type IN ('NATIVE','FORWARDER') "
                            "ORDER BY id LIMIT 1");
    if (!st) return 0;
    sqlite3_bind_text(st, 1, hex, -1, SQLITE_TRANSIENT);
    int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return id > 0 ? pt_db_resolve_game(db, id) : 0;
}

void store_game_name(sqlite3* db, int64_t game_id, char* out, size_t cap) {
    out[0] = '\0';
    sqlite3_stmt* st = prep(db, "SELECT display_name FROM games WHERE id=?");
    if (!st) return;
    sqlite3_bind_int64(st, 1, game_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char* n = (const char*)sqlite3_column_text(st, 0);
        if (n) snprintf(out, cap, "%s", n);
    }
    sqlite3_finalize(st);
    // O arquivo de status é linha-a-linha.
    for (char* p = out; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';
}

int store_export_users(sqlite3* db) {
    sqlite3_stmt* st = prep(db, "SELECT id, nickname FROM users ORDER BY is_shared, id");
    if (!st) return -1;
    char buf[4096];
    size_t len = 0;
    while (sqlite3_step(st) == SQLITE_ROW && len < sizeof(buf) - 80) {
        const char* n = (const char*)sqlite3_column_text(st, 1);
        int w = snprintf(buf + len, sizeof(buf) - len, "%lld|%.60s\n",
                         (long long)sqlite3_column_int64(st, 0), n ? n : "");
        if (w > 0) len += (size_t)w;
    }
    sqlite3_finalize(st);
    return pt_write_file_atomic(PT_USERS_PATH, buf, len);
}
