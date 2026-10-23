// Correções da auditoria no app: reimportação que preserva edições, conquistas
// na reassociação, paginação das sessões e sessões fantasmas.
#include <switch.h>
#include "pdm_shim2.h"
#include "data.hpp"
#include "pt_db.h"
#include "pt_status.h"
#include "pt_title.h"
#include <set>

s64 g_fake_now = 2000000;
std::vector<PdmPlayEvent> g_events;
std::vector<FakeStat> g_stats;
AccountUid g_user = {{0x1000000000000001ULL, 0x0123456789abcdefULL}};   // Link
AccountUid g_user2 = {{0x1000000000000002ULL, 0x0123456789abcdefULL}};  // Zelda
std::vector<u64> g_installed;
std::vector<u64> g_archived;
std::vector<std::pair<int, FakeStat>> g_user_stats;

Result pt_title_read(u64 tid, char* n, size_t c, char* i, size_t, u64* sz) {
    snprintf(n, c, "Jogo %llX", (unsigned long long)(tid >> 12 & 0xFFFF)); i[0] = 0; *sz = 0; return 0;
}
bool pt_account_nickname(AccountUid u, char* n, size_t c) { snprintf(n, c, u.uid[0] == g_user.uid[0] ? "Link" : "Zelda"); return true; }
void pt_uid_to_string(AccountUid u, char out[33]) {
    snprintf(out, 33, "%016llx%016llx", (unsigned long long)u.uid[1], (unsigned long long)u.uid[0]);
}
static void put(u32 w[2], u64 v) { w[0] = (u32)(v >> 32); w[1] = (u32)v; }
static void applet(u64 tid, int type, u64 ts) {
    PdmPlayEvent e; memset(&e, 0, sizeof(e));
    e.play_event_type = PdmPlayEventType_Applet;
    put(e.event_data.applet.program_id, tid);
    e.event_data.applet.applet_id = AppletId_application;
    e.event_data.applet.event_type = (u8)type;
    e.timestamp_user = ts; e.timestamp_steady = ts - 1000;
    g_events.push_back(e);
}
static void account(const AccountUid& u, int type, u64 ts) {
    PdmPlayEvent e; memset(&e, 0, sizeof(e));
    e.play_event_type = PdmPlayEventType_Account;
    put(&e.event_data.account.uid[0], u.uid[0]);
    put(&e.event_data.account.uid[2], u.uid[1]);
    e.event_data.account.type = (u8)type;
    e.timestamp_user = ts; e.timestamp_steady = ts - 1000;
    g_events.push_back(e);
}
enum { L = PdmAppletEventType_Launch, X = PdmAppletEventType_Exit, IN = PdmAppletEventType_InFocus, OUT = PdmAppletEventType_OutOfFocus };

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static int64_t q(const char* sql, int64_t a = 0, int64_t b = 0) {
    sqlite3* db = pt_db_open(300);
    int64_t v = pt_db_scalar(db, sql, a, b, -1);
    pt_db_close(db);
    return v;
}
static void exec(const char* sql) {
    sqlite3* db = pt_db_open(300);
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) printf("  SQL: %s\n", err);
    pt_db_close(db);
}
static std::string snapshot() {
    sqlite3* db = pt_db_open(300);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db, "SELECT COALESCE(sync_id,'-') || '|' || game_id || '|' || user_id || '|' || start_time || '|' || duration "
                           "FROM sessions ORDER BY sync_id", -1, &st, 0);
    std::string out;
    while (sqlite3_step(st) == SQLITE_ROW) out += std::string((const char*)sqlite3_column_text(st, 0)) + "\n";
    sqlite3_finalize(st);
    pt_db_close(db);
    return out;
}
static void set_stat(int user, u64 tid, u64 secs) {
    for (auto& p : g_user_stats)
        if (p.first == user && p.second.tid == tid) { p.second.playtime_s = secs; return; }
    g_user_stats.push_back({user, {tid, secs, 10000}});
}
static data::ImportReport import() {
    data::ImportReport r;
    if (!data::import_system_playtime(r)) printf("  importacao falhou: %s\n", data::last_error().c_str());
    return r;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    pt_ensure_dirs();
    const u64 GAL = 0x010099C022B96000ULL, TOMO = 0x010051F0207B2000ULL, AC = 0x01006F8002326000ULL;
    g_installed = {GAL, TOMO, AC};

    // Mesmo histórico do it2: Galaxy (Zelda, 170 s + sessão sem conta), Tomodachi (Link 1000 s),
    // Animal Crossing (Link 185 s).
    applet(GAL, L, 10000); account(g_user2, 0, 10010); applet(GAL, IN, 10020); applet(GAL, OUT, 10190);
    applet(GAL, IN, 10200);
    applet(TOMO, L, 60000); account(g_user, 0, 60005); applet(TOMO, IN, 60030); applet(TOMO, OUT, 61030);
    applet(TOMO, X, 61040); account(g_user, 1, 61041);
    account(g_user2, 0, 70000); applet(GAL, L, 70001); applet(GAL, IN, 70060); applet(GAL, OUT, 70360); applet(GAL, X, 70361);
    applet(AC, L, 90000); account(g_user, 0, 90020); applet(AC, IN, 90080); applet(AC, OUT, 90265); applet(AC, X, 90270);
    g_user_stats = {{1, {GAL, 470, 10000}}, {0, {TOMO, 1000, 60000}}, {0, {AC, 185, 90000}}};

    // --- 1) reimportação idempotente ------------------------------------------------
    data::ImportReport r1 = import();
    std::string s1 = snapshot();
    printf("%s", s1.c_str());
    check("importacao 1: 3 detalhadas + 1 residual", r1.sessions == 4);
    check("chave estavel sem indice", q("SELECT COUNT(*) FROM sessions WHERE sync_id LIKE 'nxal:%:%:%:%:%'") == 0);
    data::ImportReport r2 = import();
    check("reimportacao sem mudancas: nada novo", r2.sessions == 0);
    check("reimportacao sem mudancas: banco igual", snapshot() == s1);

    // --- 2) chaves antigas (com índice) são migradas, sem duplicar ------------------
    exec("UPDATE sessions SET sync_id = sync_id || ':0' WHERE sync_id LIKE 'nxal:%' AND sync_id NOT LIKE 'nxal:prev:%'");
    import();
    check("chaves antigas migradas, sem duplicatas", snapshot() == s1);

    int64_t link = q("SELECT id FROM users WHERE nickname = 'Link'");
    int64_t zelda = q("SELECT id FROM users WHERE nickname = 'Zelda'");
    int64_t tomo_s = q("SELECT id FROM sessions WHERE duration = 1000");
    int64_t tomo_g = q("SELECT game_id FROM sessions WHERE id = ?", tomo_s);
    int64_t gal_s = q("SELECT id FROM sessions WHERE duration = 170");
    int64_t gal_g = q("SELECT game_id FROM sessions WHERE id = ?", gal_s);
    int64_t ac_s = q("SELECT id FROM sessions WHERE duration = 185");

    // --- 3) conquistas acompanham a reassociação da sessão ---------------------------
    // a1: desbloqueada 5 s depois de fechar (save ao fechar) -> da sessão do Tomodachi.
    // a2: a Zelda já tinha (mais antiga) -> fica a data mais antiga, sem erro de chave.
    // a3: desbloqueada antes da sessão -> não é dela, fica com o Link.
    // ra: RetroAchievements -> não muda.
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO achievements(id, game_id, source, title) VALUES ('L:t:a1', %lld, 'LOCAL', 'a1'),"
             " ('L:t:a2', %lld, 'LOCAL', 'a2'), ('L:t:a3', %lld, 'LOCAL', 'a3'), ('RA:9', %lld, 'RA', 'ra');"
             "INSERT INTO user_achievements VALUES ('L:t:a1', %lld, 61045), ('L:t:a2', %lld, 61000),"
             " ('L:t:a2', %lld, 50000), ('L:t:a3', %lld, 59000), ('RA:9', %lld, 60500);",
             (long long)tomo_g, (long long)tomo_g, (long long)tomo_g, (long long)tomo_g, (long long)link,
             (long long)link, (long long)zelda, (long long)link, (long long)link);
    exec(sql);
    check("reassociar sessao", data::reassign_session_user(tomo_s, zelda));
    check("sessao com a Zelda", q("SELECT user_id FROM sessions WHERE id = ?", tomo_s) == zelda);
    check("a1 (save ao fechar) foi para a Zelda",
          q("SELECT user_id FROM user_achievements WHERE achievement_id = 'L:t:a1'") == zelda);
    check("a2: uma linha so, data mais antiga",
          q("SELECT COUNT(*) || unlocked_at FROM user_achievements WHERE achievement_id = 'L:t:a2'") == 150000);
    check("a2 com a Zelda", q("SELECT user_id FROM user_achievements WHERE achievement_id = 'L:t:a2'") == zelda);
    check("a3 (antes da sessao) ficou com o Link",
          q("SELECT user_id FROM user_achievements WHERE achievement_id = 'L:t:a3'") == link);
    check("RetroAchievements nao muda", q("SELECT user_id FROM user_achievements WHERE achievement_id = 'RA:9'") == link);

    // Jogo inteiro de volta para o Link: a1 volta; a2 (data de antes da sessão,
    // a da Zelda que ficou) não é de sessão nenhuma do jogo e fica com a Zelda.
    check("reassociar jogo", data::reassign_game_user(tomo_g, link));
    check("jogo inteiro: a1 de volta", q("SELECT user_id FROM user_achievements WHERE achievement_id = 'L:t:a1'") == link);
    check("jogo inteiro: a2 fica com a Zelda", q("SELECT user_id FROM user_achievements WHERE achievement_id = 'L:t:a2'") == zelda);
    data::reassign_session_user(tomo_s, zelda);

    // --- 4) edições sobrevivem à reimportação ----------------------------------------
    check("excluir detalhada do Galaxy", data::delete_session(gal_s));
    check("exclusao lembrada", q("SELECT COUNT(*) FROM import_deleted") == 1);
    check("mover Animal Crossing para o Galaxy", data::move_session(ac_s, gal_g));
    int64_t before = q("SELECT COUNT(*) FROM sessions");
    data::ImportReport r4 = import();
    check("reimportacao apos edicoes: nada novo", r4.sessions == 0);
    check("reimportacao: mesma quantidade de sessoes", q("SELECT COUNT(*) FROM sessions") == before);
    check("reassociada continua com a Zelda", q("SELECT user_id FROM sessions WHERE id = ?", tomo_s) == zelda);
    check("Tomodachi do Link nao ganhou residual",
          q("SELECT COUNT(*) FROM sessions WHERE game_id = ? AND user_id = ?", tomo_g, link) == 0);
    check("excluida nao voltou", q("SELECT COUNT(*) FROM sessions WHERE duration = 170") == 0);
    check("movida continua no Galaxy", q("SELECT game_id FROM sessions WHERE id = ?", ac_s) == gal_g);
    check("residual do Galaxy segue 300 s (exclusao conta como registrada)",
          q("SELECT duration FROM sessions WHERE sync_id LIKE 'nxal:prev:%' AND game_id = ?", gal_g) == 300);

    // --- 5) eventos novos entram uma vez ---------------------------------------------
    applet(TOMO, L, 100000); account(g_user, 0, 100005); applet(TOMO, IN, 100010); applet(TOMO, OUT, 100610);
    applet(TOMO, X, 100620);
    set_stat(0, TOMO, 1600);
    data::ImportReport r5 = import();
    check("evento novo importado", r5.sessions == 1);
    check("evento novo com 600 s", q("SELECT duration FROM sessions WHERE start_time = 100000") == 600);
    check("de novo: nada", import().sessions == 0);

    // --- 6) residual: acompanha o total enquanto intacta ------------------------------
    set_stat(1, GAL, 1000);
    import();
    int64_t res = q("SELECT id FROM sessions WHERE sync_id LIKE 'nxal:prev:%' AND game_id = ?", gal_g);
    check("residual intacta cresce: 1000 - 170 excluidos = 830", q("SELECT duration FROM sessions WHERE id = ?", res) == 830);
    data::reassign_session_user(res, link);
    set_stat(1, GAL, 1200);
    import();
    check("residual reassociada nao muda", q("SELECT duration FROM sessions WHERE id = ?", res) == 830);
    check("nenhuma residual nova para a Zelda",
          q("SELECT COUNT(*) FROM sessions WHERE sync_id LIKE 'nxal:prev:%' AND user_id = ?", zelda) == 0);
    data::delete_session(res);
    import();
    check("residual excluida nao volta", q("SELECT COUNT(*) FROM sessions WHERE sync_id LIKE 'nxal:prev:%' AND game_id = ?", gal_g) == 0);

    // --- 7) paginação ----------------------------------------------------------------
    exec("INSERT INTO games(id, title_id, display_name, source_type, created_at, updated_at) "
         "VALUES (50, '0100000000005000', 'Paginado', 'NATIVE', 0, 0)");
    {
        sqlite3* db = pt_db_open(300);
        sqlite3_exec(db, "BEGIN", 0, 0, 0);
        for (int i = 0; i < 250; i++) {
            // Pares com o mesmo início: o cursor precisa do id para desempatar.
            snprintf(sql, sizeof(sql),
                     "INSERT INTO sessions(game_id, user_id, start_time, end_time, duration, created_at) "
                     "VALUES (50, %lld, %d, %d, 10, 0)", (long long)(i % 3 ? link : zelda), 500000 + i / 2,
                     500000 + i / 2 + 10);
            sqlite3_exec(db, sql, 0, 0, 0);
        }
        sqlite3_exec(db, "COMMIT", 0, 0, 0);
        pt_db_close(db);
    }
    std::vector<SessionRow> page, all;
    const SessionRow* after = nullptr;
    SessionRow last;
    int pages = 0;
    do {
        data::sessions(50, 0, after, 100, page);
        if (!page.empty()) { last = page.back(); after = &last; }
        all.insert(all.end(), page.begin(), page.end());
        pages++;
    } while (page.size() == 100);
    std::set<int64_t> ids;
    bool ordered = true;
    for (size_t i = 0; i < all.size(); i++) {
        ids.insert(all[i].id);
        if (i && (all[i].start > all[i - 1].start || (all[i].start == all[i - 1].start && all[i].id > all[i - 1].id)))
            ordered = false;
    }
    check("paginas: 250 sessoes em 3 paginas", all.size() == 250 && pages == 3);
    check("paginas: sem repetir", ids.size() == 250);
    check("paginas: mais recente primeiro", ordered);
    data::SessionSummary sum;
    data::session_summary(50, 0, sum);
    check("resumo: 250 sessoes, 2500 s", sum.count == 250 && sum.total == 2500);
    data::session_summary(50, zelda, sum);
    check("resumo filtrado pelo perfil", sum.count == 84 && sum.total == 840);
    data::sessions(50, zelda, nullptr, 500, page);
    bool only_zelda = page.size() == 84;
    for (auto& p : page) only_zelda &= p.user_id == zelda;
    check("pagina filtrada pelo perfil", only_zelda);

    // --- 8) aba de fantasmas e Finalizar ---------------------------------------------
    exec("INSERT INTO sessions(id, game_id, user_id, start_time, end_time, duration, created_at) VALUES "
         "(900, 50, 1, 700000, NULL, 120, 0), (901, 50, 1, 710000, NULL, 30, 0)");
    PtStatus st; memset(&st, 0, sizeof(st)); st.session_id = 901;  // 901 é a sessão viva
    pt_status_write(&st);
    data::sessions(0, 0, nullptr, 100, page);
    check("fantasmas: so a 900 (a viva fica de fora)", page.size() == 1 && page[0].id == 900 && page[0].ghost);
    data::session_summary(0, 0, sum);
    check("resumo das fantasmas", sum.count == 1 && sum.total == 120);
    check("finalizar a viva e recusado", !data::finalize_ghost(901));
    check("finalizar fantasma", data::finalize_ghost(900));
    check("fim = inicio + duracao", q("SELECT end_time FROM sessions WHERE id = 900") == 700120);
    data::sessions(0, 0, nullptr, 100, page);
    check("fantasmas: lista vazia", page.empty());

    // --- 9) tracking desativado ------------------------------------------------------
    auto in_home = [](int64_t id) {
        std::vector<GameRow> gs;
        data::games(0, gs);
        for (auto& g : gs)
            if (g.id == id) return true;
        return false;
    };
    check("jogo na tela inicial", in_home(50));
    check("desativar", data::set_tracking(50, false));
    check("desativado sai da tela inicial", !in_home(50));
    std::vector<GameRow> dis;
    data::disabled_games(dis);
    check("aparece em Desativados (usuário)", dis.size() == 1 && dis[0].id == 50 && !dis[0].auto_disabled);
    check("sessões guardadas", q("SELECT COUNT(*) FROM sessions WHERE game_id = 50") == 252);
    check("reativar", data::set_tracking(50, true));
    check("reativado volta à tela inicial", in_home(50));
    data::disabled_games(dis);
    check("Desativados vazio", dis.empty());
    // Utilitário na biblioteca: a importação aplica o filtro (desativa e apaga as sessões).
    exec("INSERT INTO games(id, title_id, display_name, source_type, created_at, updated_at) "
         "VALUES (60, '0544653000000000', 'Sphaira', 'FORWARDER', 0, 0)");
    exec("INSERT INTO sessions(game_id, user_id, start_time, end_time, duration, created_at) VALUES (60, 1, 1, 50, 49, 0)");
    import();
    check("importação desativa o Sphaira", q("SELECT disabled FROM tracking_disabled WHERE game_id = 60 AND reason = 'auto'") == 1);
    check("sessões do Sphaira apagadas", q("SELECT COUNT(*) FROM sessions WHERE game_id = 60") == 0);
    data::disabled_games(dis);
    check("Sphaira em Desativados (automático)", dis.size() == 1 && dis[0].auto_disabled);
    check("reativar o Sphaira", data::set_tracking(60, true));
    import();
    check("reativado pelo usuário: o filtro não desativa de novo",
          q("SELECT disabled FROM tracking_disabled WHERE game_id = 60") == 0);

    // Tempo total do perfil (barra lateral das Configurações): todas as sessões do perfil,
    // de todos os jogos e apps (desativados e unificados inclusive); 0 = todos os perfis.
    exec("INSERT INTO sessions(game_id, user_id, start_time, end_time, duration, created_at) VALUES (60, 2, 5, 9, 4, 0)");
    int64_t t1 = -1, t2 = -1, t_all = -1, none = -1;
    data::profile_total(1, t1);
    data::profile_total(2, t2);
    data::profile_total(0, t_all);
    data::profile_total(999, none);
    check("tempo total do perfil 1 = soma das sessões dele",
          t1 > 0 && t1 == q("SELECT SUM(duration) FROM sessions WHERE user_id = 1"));
    check("tempo total do perfil 2 = soma das sessões dele (app reativado incluso)",
          t2 >= 4 && t2 == q("SELECT SUM(duration) FROM sessions WHERE user_id = 2"));
    check("todos os perfis = soma de todas as sessões", t_all == q("SELECT SUM(duration) FROM sessions") && t_all >= t1 + t2);
    check("perfil sem sessões = 0", none == 0);

    // Filtro de utilitários: o app com o nome novo e os nomes antigos.
    check("utilitário: Ruby Achievements e Switch Playtime",
          pt_db_is_utility_name("Ruby Achievements") && pt_db_is_utility_name("ruby achievements") &&
              pt_db_is_utility_name("Switch Playtime") && pt_db_is_utility_name("Switch Playtime Tracker") &&
              !pt_db_is_utility_name("Ruby"));

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}

#include "forwarder/owo.hpp"
namespace pt_fwd {
u64 forwarder_title_id(const std::string&, const std::string&) { return 0x0500000000001000ULL; }
Result install_forwarder(const Config&, NcmStorageId, const Progress&, u64*, std::string*) { return 1; }
}  // namespace pt_fwd
