// Tracker completo (sessões, transações, conquistas e foco) com relógio e FS simulados.
#include <switch.h>
#include <utime.h>
#include "detect.h"
#include "tracker.h"
#include "pt_db.h"
#include "pt_util.h"
#include "pt_paths.h"
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;
u64 g_tid = 0;
bool g_home_front = false;

// --- detect simulado ---
u64 detect_running_title(void) { return g_tid; }
bool detect_is_forwarder(u64 tid) { return (tid >> 56) == 0x05; }
bool detect_is_ghost(u64) { return false; }
bool detect_build_id(char out[65]) {
    out[0] = 0;
    return false;
}
bool detect_home_in_front(u64) { return g_home_front; }
u64 detect_focused_title(void) { return 0; }
void detect_init(void) {}
bool detect_title_info(u64 tid, char* n, size_t c, char* i, size_t) {
    snprintf(n, c, "Jogo %llX", (unsigned long long)(tid & 0xFFFF));
    i[0] = 0;
    return true;
}
bool detect_current_account(AccountUid* u) {
    u->uid[0] = 0xA;
    u->uid[1] = 1;
    return true;
}
bool pt_account_nickname(AccountUid, char* n, size_t c) {
    snprintf(n, c, "Link");
    return true;
}
void pt_uid_to_string(AccountUid u, char out[33]) {
    snprintf(out, 33, "%016llX%016llX", (unsigned long long)u.uid[1], (unsigned long long)u.uid[0]);
}

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static void run(int secs) {
    for (int i = 0; i < secs; i++) {
        g_fake_now++;
        tracker_tick(g_fake_now);
    }
}
static int64_t q(const char* sql) {
    sqlite3* db = pt_db_open(300);
    int64_t v = pt_db_scalar(db, sql, 0, 0, -1);
    pt_db_close(db);
    return v;
}
static long log_count(const char* needle) {
    FILE* f = fopen(PT_LOG_PATH, "rb");
    if (!f) return 0;
    char line[1024];
    long n = 0;
    while (fgets(line, sizeof line, f))
        if (strstr(line, needle)) n++;
    fclose(f);
    return n;
}
// Sessões fantasmas: o boot fecha as sessões que o processo anterior deixou abertas.
int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    mkdir("sdmc:", 0777);
    mkdir("sdmc:/switch", 0777);
    pt_ensure_dirs();

    // "Boot anterior": duas sessões abertas (desligou com jogo suspenso) e uma fechada.
    sqlite3* db = pt_db_open(300);
    sqlite3_exec(db,
                 "INSERT INTO users(id, nickname, created_at) VALUES(1, 'Link', 0);"
                 "INSERT INTO games(id, title_id, display_name, source_type, created_at, updated_at) "
                 " VALUES(1, '0100000000001111', 'A', 'NATIVE', 0, 0), (2, '0100000000002222', 'B', 'NATIVE', 0, 0);"
                 "INSERT INTO sessions(id, game_id, user_id, start_time, end_time, duration, created_at) VALUES"
                 " (1, 1, 1, 100, NULL, 500, 0), (2, 2, 1, 2000, NULL, 60, 0), (3, 1, 1, 50, 80, 30, 0);"
                 // Sphaira gravado por uma versão sem o filtro de utilitários.
                 "INSERT INTO games(id, title_id, display_name, source_type, created_at, updated_at) "
                 " VALUES(4, '0544653000000000', 'Sphaira', 'FORWARDER', 0, 0);"
                 "INSERT INTO sessions(id, game_id, user_id, start_time, end_time, duration, created_at) VALUES"
                 " (4, 4, 1, 300, 400, 100, 0);",
                 NULL, NULL, NULL);
    pt_db_recalc_game(db, 1, 0);
    pt_db_recalc_game(db, 2, 0);
    int64_t lp_before = pt_db_scalar(db, "SELECT last_played FROM games WHERE id = 1", 0, 0, -1);
    pt_db_close(db);

    // Boot com um jogo já aberto: a sessão viva nova não pode ser fechada.
    g_fake_now = 5000;
    tracker_init(g_fake_now);
    g_tid = 0x0500000000001234ULL;  // homebrew: grava após ~10 s
    run(20);

    check("fantasma 1: fim = início + duração", q("SELECT end_time FROM sessions WHERE id = 1") == 600);
    check("fantasma 2: fim = início + duração", q("SELECT end_time FROM sessions WHERE id = 2") == 2060);
    check("duração mantida", q("SELECT duration FROM sessions WHERE id = 1") == 500);
    check("sessão já fechada intacta", q("SELECT end_time FROM sessions WHERE id = 3") == 80);
    check("total do jogo inalterado", q("SELECT total_playtime FROM games WHERE id = 1") == 530);
    check("last_played inalterado", q("SELECT last_played FROM games WHERE id = 1") == lp_before);
    check("sessão viva do boot continua aberta",
          q("SELECT COUNT(*) FROM sessions WHERE end_time IS NULL") == 1);
    check("log do boot", log_count("sessoes abertas do boot anterior") == 1);
    check("Sphaira desativado no boot", q("SELECT disabled FROM tracking_disabled WHERE game_id = 4 AND reason = 'auto'") == 1);
    check("sessões do Sphaira apagadas", q("SELECT COUNT(*) FROM sessions WHERE game_id = 4") == 0);
    check("log dos utilitários", log_count("homebrews utilitarios") == 1);

    // Checkpoints seguintes não mexem mais (a viva continua aberta).
    run(130);
    check("viva continua aberta após checkpoints", q("SELECT COUNT(*) FROM sessions WHERE end_time IS NULL") == 1);
    check("log só uma vez", log_count("sessoes abertas do boot anterior") == 1);
    g_tid = 0;
    run(3);
    check("viva fechada ao sair do jogo", q("SELECT COUNT(*) FROM sessions WHERE end_time IS NULL") == 0);

    // users.txt: o app cria um perfil direto no banco (importação); o sysmodule
    // regrava o arquivo na gravação seguinte.
    char ubuf[512];
    int un = pt_read_file(PT_USERS_PATH, ubuf, sizeof(ubuf));
    check("users.txt com 1 perfil", un > 0 && !strstr(ubuf, "Zelda"));
    db = pt_db_open(300);
    sqlite3_exec(db, "INSERT INTO users(id, nickname, created_at) VALUES(9, 'Zelda', 0);", NULL, NULL, NULL);
    pt_db_close(db);
    g_tid = 0x0500000000001234ULL;
    run(20);
    un = pt_read_file(PT_USERS_PATH, ubuf, sizeof(ubuf));
    check("users.txt regravado com o perfil criado pelo app", un > 0 && strstr(ubuf, "9|Zelda") != NULL);

    // Tracking desativado pelo usuário (app) com o jogo aberto: a sessão viva
    // fecha com o tempo até ali e nada mais é gravado.
    int64_t game = q("SELECT id FROM games WHERE title_id = '0500000000001234'");
    int64_t live = q("SELECT id FROM sessions WHERE game_id = (SELECT id FROM games WHERE title_id = '0500000000001234') AND end_time IS NULL");
    check("sessão viva antes de desativar", live > 0);
    db = pt_db_open(300);
    pt_db_set_tracking(db, game, false, "user", 0);
    pt_db_close(db);
    run(70);
    check("sessão viva fechada ao desativar",
          q("SELECT COUNT(*) FROM sessions WHERE end_time IS NULL") == 0);
    int64_t dur = q("SELECT duration FROM sessions WHERE end_time IS NOT NULL ORDER BY id DESC LIMIT 1");
    int64_t count_before = q("SELECT COUNT(*) FROM sessions WHERE game_id = (SELECT id FROM games WHERE title_id = '0500000000001234')");
    run(70);
    check("nada gravado depois", q("SELECT COUNT(*) FROM sessions WHERE game_id = (SELECT id FROM games WHERE title_id = '0500000000001234')") == count_before);
    check("duração não cresce", q("SELECT duration FROM sessions WHERE end_time IS NOT NULL ORDER BY id DESC LIMIT 1") == dur);
    check("log do tracking desativado", log_count("tempo nao gravado") == 1);
    g_tid = 0;
    run(3);
    // Reabrir o jogo desativado: nenhuma sessão nova.
    g_tid = 0x0500000000001234ULL;
    run(30);
    check("reaberto desativado: sem sessão nova", q("SELECT COUNT(*) FROM sessions WHERE game_id = (SELECT id FROM games WHERE title_id = '0500000000001234')") == count_before);
    g_tid = 0;
    run(3);
    // Reativado: volta a contar.
    db = pt_db_open(300);
    pt_db_set_tracking(db, game, true, "user", 0);
    pt_db_close(db);
    g_tid = 0x0500000000001234ULL;
    run(30);
    check("reativado: sessão nova", q("SELECT COUNT(*) FROM sessions WHERE game_id = (SELECT id FROM games WHERE title_id = '0500000000001234')") == count_before + 1);
    g_tid = 0;
    run(3);
    // Utilitário novo (nome do NACP) já nasce desativado.
    db = pt_db_open(300);
    check("nome utilitário reconhecido", pt_db_is_utility_name("sphaira") && pt_db_is_utility_name("Ruby Achievements") &&
                                         !pt_db_is_utility_name("Zelda"));
    pt_db_close(db);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
