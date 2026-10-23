// Regressão da importação: casos tirados do import.log real (console com 2 contas).
#include <switch.h>
#include "pdm_shim2.h"
#include "data.hpp"
#include "pt_db.h"
#include "pt_title.h"

s64 g_fake_now = 2000000;
std::vector<PdmPlayEvent> g_events;
std::vector<FakeStat> g_stats;
AccountUid g_user = {{0x1000000000000001ULL, 0x0123456789abcdefULL}};   // Link
AccountUid g_user2 = {{0x1000000000000002ULL, 0x0123456789abcdefULL}};  // Zelda
std::vector<u64> g_installed;
std::vector<u64> g_archived;
std::vector<std::pair<int, FakeStat>> g_user_stats;  // (conta, total)

Result pt_title_read(u64 tid, char* n, size_t c, char* i, size_t, u64* sz) {
    snprintf(n, c, "Jogo %llX", (unsigned long long)(tid >> 12 & 0xFFFF)); i[0] = 0; *sz = 0; return 0;
}
bool pt_account_nickname(AccountUid u, char* n, size_t c) { snprintf(n, c, u.uid[0] == g_user.uid[0] ? "Link" : "Zelda"); return true; }
void pt_uid_to_string(AccountUid u, char out[33]) {
    snprintf(out, 33, "%016llx%016llx", (unsigned long long)u.uid[1], (unsigned long long)u.uid[0]);
}

static void put(u32 w[2], u64 v) { w[0] = (u32)(v >> 32); w[1] = (u32)v; }
static u64 g_steady_off = 1000;
static void applet(u64 tid, int type, u64 ts) {
    PdmPlayEvent e; memset(&e, 0, sizeof(e));
    e.play_event_type = PdmPlayEventType_Applet;
    put(e.event_data.applet.program_id, tid);
    e.event_data.applet.applet_id = AppletId_application;
    e.event_data.applet.event_type = (u8)type;
    e.timestamp_user = ts; e.timestamp_steady = ts - g_steady_off;
    g_events.push_back(e);
}
static void account(const AccountUid& u, int type, u64 ts) {
    PdmPlayEvent e; memset(&e, 0, sizeof(e));
    e.play_event_type = PdmPlayEventType_Account;
    put(&e.event_data.account.uid[0], u.uid[0]);
    put(&e.event_data.account.uid[2], u.uid[1]);
    e.event_data.account.type = (u8)type;
    e.timestamp_user = ts; e.timestamp_steady = ts - g_steady_off;
    g_events.push_back(e);
}
enum { L = PdmAppletEventType_Launch, X = PdmAppletEventType_Exit, IN = PdmAppletEventType_InFocus, OUT = PdmAppletEventType_OutOfFocus };

int main() {
    pt_ensure_dirs();
    const u64 GAL = 0x010099C022B96000ULL, TOMO = 0x010051F0207B2000ULL, AC = 0x01006F8002326000ULL, UNP = 0x0100701015846000ULL;
    g_installed = {GAL, TOMO, AC, UNP};
    g_archived = {AC};  // arquivado: registro na Home, sem o jogo

    // 1) Galaxy (Zelda) trava sem OutFocus/Exit; horas depois Tomodachi (Link).
    //    Antes: a sessão do Galaxy ficava aberta até o próximo Launch do Galaxy.
    applet(GAL, L, 10000); account(g_user2, 0, 10010); applet(GAL, IN, 10020); applet(GAL, OUT, 10190);  // 170 s
    applet(GAL, IN, 10200);                                   // volta ao jogo e trava
    applet(TOMO, L, 60000); account(g_user, 0, 60005); applet(TOMO, IN, 60030); applet(TOMO, OUT, 61030);
    applet(TOMO, OUT, 61031); applet(TOMO, X, 61040); account(g_user, 1, 61041);                           // 1000 s
    // 2) Animal Crossing aberto sem evento de conta, logo depois de uma sessão da Zelda:
    //    antes herdava a Zelda (conta "atual" global); agora fica sem perfil.
    account(g_user2, 0, 70000); applet(GAL, L, 70001); applet(GAL, IN, 70060); applet(GAL, OUT, 70360); applet(GAL, X, 70361);  // 300 s (Zelda)
    applet(AC, L, 80000); applet(AC, IN, 80100); applet(AC, OUT, 82564); applet(AC, X, 82565);          // sem conta
    applet(AC, L, 90000); account(g_user, 0, 90020); applet(AC, IN, 90080); applet(AC, OUT, 90265); applet(AC, X, 90270);   // 185 s (Link)
    // 3) Unpacking: total oficial 0 para todos (NXAL mostra 0): nada é importado.
    applet(UNP, L, 95000); account(g_user, 0, 95001); applet(UNP, IN, 95010); applet(UNP, OUT, 95067); applet(UNP, X, 95068);

    g_user_stats = {{1, {GAL, 470, 10000}}, {0, {TOMO, 1000, 60000}}, {0, {AC, 185, 90000}}};

    data::ImportReport r;
    bool ok = data::import_system_playtime(r);
    printf("importacao ok=%d sessoes=%d segundos=%lld nao_instalados=%d\n", ok, r.sessions, (long long)r.seconds, r.uninstalled);
    std::vector<GameRow> un;
    data::uninstalled_games(un);
    for (auto& g : un) printf("  desinstalado: %s %s %lld s\n", g.title_id.c_str(), g.name.c_str(), (long long)g.total);
    std::vector<UserRow> us;
    data::users(us);
    for (auto& u : us) {
        for (int64_t gid = 1; gid <= 4; gid++) {
            std::vector<SessionRow> rows;
            GameRow gr;
            data::game(gid, u.id, gr);
            data::sessions(gid, u.id, nullptr, 1000, rows);
            printf("  jogo %lld perfil %s: %zu sessoes, total %lld, count %lld\n", (long long)gid, u.nickname.c_str(),
                   rows.size(), (long long)gr.total, (long long)gr.session_count);
            for (auto& x : rows)
                if (x.user_id != u.id) printf("  ERRO: sessao de outro perfil\n");
        }
    }
    for (int64_t gid = 1; gid <= 4; gid++) {
        std::vector<SessionRow> all;
        data::sessions(gid, 0, nullptr, 1000, all);
        printf("  jogo %lld todos: %zu sessoes\n", (long long)gid, all.size());
    }
    sqlite3* db = pt_db_open(300);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db, "SELECT g.title_id, u.nickname, s.start_time, s.end_time, s.duration FROM sessions s "
                           "JOIN games g ON g.id=s.game_id JOIN users u ON u.id=s.user_id ORDER BY s.start_time", -1, &st, 0);
    while (sqlite3_step(st) == SQLITE_ROW)
        printf("  %s %-8s %lld -> %lld  %lld s\n", sqlite3_column_text(st, 0), sqlite3_column_text(st, 1),
               sqlite3_column_int64(st, 2), sqlite3_column_int64(st, 3), sqlite3_column_int64(st, 4));
    sqlite3_finalize(st);
    pt_db_close(db);
}

// O forwarder não faz parte deste teste.
#include "forwarder/owo.hpp"
namespace pt_fwd {
u64 forwarder_title_id(const std::string&, const std::string&) { return 0x0500000000001000ULL; }
Result install_forwarder(const Config&, NcmStorageId, const Progress&, u64*, std::string*) { return 1; }
}  // namespace pt_fwd
