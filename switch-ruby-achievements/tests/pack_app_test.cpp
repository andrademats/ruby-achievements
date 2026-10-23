// Pacotes de conquistas no app (data.cpp + common/ach): reconhecimento pelo Title ID
// sem abrir o jogo, lista de pacotes, troca do ativo com reset e volta do zero.
#include <switch.h>
#include "pdm_shim2.h"
#include "data.hpp"
#include "i18n.hpp"
#include "pt_db.h"
#include "pt_pack.h"
#include "pt_paths.h"
#include "pt_status.h"
#include "pt_title.h"
#include "pt_util.h"

s64 g_fake_now = 2000000;
std::vector<PdmPlayEvent> g_events;
std::vector<FakeStat> g_stats;
AccountUid g_user = {{0x1000000000000001ULL, 0x0123456789abcdefULL}};
AccountUid g_user2 = {{0x1000000000000002ULL, 0x0123456789abcdefULL}};
std::vector<u64> g_installed;
std::vector<u64> g_archived;
std::vector<std::pair<int, FakeStat>> g_user_stats;

Result pt_title_read(u64 tid, char* n, size_t c, char* i, size_t, u64* sz) {
    snprintf(n, c, "Jogo %04llX", (unsigned long long)(tid >> 32 & 0xFFFF));
    i[0] = 0;
    *sz = 0;
    return 0;
}
bool pt_account_nickname(AccountUid, char* n, size_t c) { snprintf(n, c, "P"); return true; }
void pt_uid_to_string(AccountUid u, char out[33]) {
    snprintf(out, 33, "%016llx%016llx", (unsigned long long)u.uid[1], (unsigned long long)u.uid[0]);
}

#include "forwarder/owo.hpp"
namespace pt_fwd {  // o forwarder fantasma não entra neste teste
u64 forwarder_title_id(const std::string&, const std::string&) { return 0x0500000000001000ULL; }
Result install_forwarder(const Config&, NcmStorageId, const Progress&, u64*, std::string*) { return 1; }
}  // namespace pt_fwd

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static int64_t q(const char* sql, int64_t a = 0) {
    sqlite3* db = pt_db_open(300);
    int64_t v = pt_db_scalar(db, sql, a, 0, -1);
    pt_db_close(db);
    return v;
}
static void exec(const char* sql) {
    sqlite3* db = pt_db_open(300);
    char* err = nullptr;
    if (sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK) printf("SQL: %s\n", err);
    pt_db_close(db);
}
static void put(const char* name, const char* body) {
    std::string p = std::string(PT_ACH_DIR) + "/" + name;
    FILE* f = fopen(p.c_str(), "wb");
    fputs(body, f);
    fclose(f);
}
static int64_t game_of(const char* tid) {
    char sql[128];
    snprintf(sql, sizeof sql, "SELECT id FROM games WHERE title_id='%s'", tid);
    return q(sql);
}
static bool has_game(const std::vector<AchievementGameRow>& v, int64_t id, int total) {
    for (auto& g : v)
        if (g.game_id == id) return g.total == total;
    return false;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (const char* d : {"sdmc:", "sdmc:/switch", PT_DIR, PT_ACH_DIR}) mkdir(d, 0777);
    const u64 A = 0x0100AAAA00000000ULL, B = 0x0100BBBB00000000ULL, C = 0x0100CCCC00000000ULL;
    const u64 E = 0x0100EEEE00000000ULL;
    g_installed = {A, B, E};  // C não está instalado; D já tem histórico (desinstalado); E com JSON inválido
    put("0100AAAA00000000.json",
        "{\"game_name\":\"A\",\"achievements\":[{\"id\":\"a\",\"title\":\"A1\",\"rarity\":\"gold\",\"type\":\"playtime_stat\","
        "\"target_seconds\":60},{\"id\":\"b\",\"title\":\"A2\",\"type\":\"playtime_stat\",\"target_seconds\":90},"
        "{\"id\":\"platina\",\"title\":\"Plat\",\"rarity\":\"ruby\"}]}");
    put("0100AAAA00000000.speedrun.json",
        "{\"pack_name\":\"Speedrun\",\"achievements\":[{\"id\":\"x\",\"title\":\"Rápido\",\"type\":\"playtime_stat\","
        "\"target_seconds\":30},{\"id\":\"platina\",\"title\":\"Plat S\",\"rarity\":\"ruby\"}]}");
    put("0100bbbb00000000.json", "{\"achievements\":[{\"id\":\"k\",\"title\":\"K\",\"type\":\"playtime_stat\",\"target_seconds\":1}]}");
    put("0100CCCC00000000.json", "{\"achievements\":[{\"id\":\"c\",\"title\":\"C\",\"type\":\"playtime_stat\",\"target_seconds\":1}]}");
    put("0100DDDD00000000.json", "{\"achievements\":[{\"id\":\"d\",\"title\":\"D\",\"type\":\"playtime_stat\",\"target_seconds\":1}]}");
    put("0100EEEE00000000.json", "{isto não é json");
    put("JogoROM.json", "{\"achievements\":[{\"id\":\"r\",\"title\":\"R\",\"type\":\"playtime_stat\",\"target_seconds\":1}]}");

    // Banco com dois perfis e o D (jogado antes, hoje desinstalado).
    { sqlite3* db = pt_db_open(300); pt_db_close(db); }
    exec("INSERT INTO users(id,nickname,created_at) VALUES(1,'P1',0),(2,'P2',0);"
         "INSERT INTO games(title_id,display_name,source_type,created_at,updated_at) VALUES('0100DDDD00000000','D','NATIVE',0,0);");

    // --- descoberta (pt_pack)
    auto keys = pt_pack_title_keys();
    check("Title IDs com pacote: A, B (minúsculas), C, D, E — sem a ROM",
          keys.size() == 5 && keys[0] == "0100AAAA00000000" && keys[1] == "0100BBBB00000000");
    auto files = pt_pack_files("0100AAAA00000000");
    check("A: principal primeiro, depois a variante",
          files.size() == 2 && files[0] == "0100AAAA00000000.json" && files[1] == "0100AAAA00000000.speedrun.json");
    check("A: ativo padrão = principal", pt_pack_active("0100AAAA00000000") == "0100AAAA00000000.json");
    check("B: arquivo com o Title ID em minúsculas", pt_pack_active("0100BBBB00000000") == "0100bbbb00000000.json");
    check("ROM: sem variantes", pt_pack_files("JogoROM") == std::vector<std::string>{"JogoROM.json"});

    // --- reconhecimento sem abrir o jogo
    check("sincronização dos pacotes", data::sync_achievement_packs());
    std::vector<AchievementGameRow> ag;
    data::achievement_games(0, ag);
    int64_t ga = game_of("0100AAAA00000000"), gb = game_of("0100BBBB00000000"), gd = game_of("0100DDDD00000000");
    check("A instalado e nunca aberto: jogo criado pelo NACP", ga > 0 && q("SELECT COUNT(*) FROM games WHERE display_name='Jogo AAAA'") == 1);
    check("A na galeria com as 3 conquistas do pacote principal", has_game(ag, ga, 3));
    check("B na galeria (arquivo em minúsculas)", gb > 0 && has_game(ag, gb, 1));
    check("D desinstalado mas já no banco: ligado ao pacote", has_game(ag, gd, 1));
    check("C nem instalado nem no banco: fica de fora", game_of("0100CCCC00000000") == -1);
    check("E instalado com pacote inválido: não cria o jogo", game_of("0100EEEE00000000") == -1);
    check("jogo novo aparece na tela inicial (todos os perfis)", [&] {
        std::vector<GameRow> g;
        data::games(0, g);
        for (auto& x : g)
            if (x.id == ga) return true;
        return false;
    }());
    std::vector<AchievementRow> rows;
    data::achievements(ga, 0, rows);
    bool gold = false, ruby = false;
    for (auto& r : rows) gold |= r.title == "A1" && r.rarity == "gold", ruby |= r.title == "Plat" && r.rarity == "ruby";
    check("galeria com a raridade de cada conquista", gold && ruby);

    // --- lista de pacotes
    std::vector<AchievementPack> packs;
    data::achievement_packs(ga, packs);
    check("A: 2 pacotes, principal ativo, \"Speedrun\" pelo pack_name",
          packs.size() == 2 && packs[0].active && packs[0].label == tr("principal") && packs[0].count == 3 &&
              packs[1].label == "Speedrun" && !packs[1].active);
    data::achievement_packs(gb, packs);
    check("B: 1 pacote", packs.size() == 1);

    // --- progresso nos dois perfis + outro jogo, e a troca
    exec("INSERT INTO user_achievements VALUES('L:0100AAAA00000000:a',1,100),('L:0100AAAA00000000:a',2,110),"
         "('L:0100AAAA00000000:b',1,120),('L:0100DDDD00000000:d',1,130);"
         "INSERT INTO achievement_queue(achievement_id,user_id,created_at) VALUES('L:0100AAAA00000000:b',1,120);");
    check("troca para o Speedrun", data::set_active_pack(ga, "0100AAAA00000000.speedrun.json"));
    check("desbloqueios do A apagados (todos os perfis)",
          q("SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE 'L:0100AAAA00000000:%'") == 0);
    check("fila de pop-ups do A apagada", q("SELECT COUNT(*) FROM achievement_queue") == 0);
    check("outro jogo intacto", q("SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE 'L:0100DDDD%'") == 1);
    check("galeria já mostra o Speedrun (2 conquistas, 0%)",
          q("SELECT COUNT(*) FROM achievements WHERE game_id=?", ga) == 2 &&
              q("SELECT COUNT(*) FROM achievements WHERE title='Rápido'") == 1);
    check("escolha gravada para o sysmodule", pt_pack_active("0100AAAA00000000") == "0100AAAA00000000.speedrun.json");
    check("pacote antigo continua no SD", pt_pack_files("0100AAAA00000000").size() == 2);
    data::achievement_packs(ga, packs);
    check("lista marca o Speedrun como ativo", packs.size() == 2 && packs[1].active && !packs[0].active);

    exec("INSERT INTO user_achievements VALUES('L:0100AAAA00000000:x',1,200);");
    check("volta ao principal", data::set_active_pack(ga, "0100AAAA00000000.json"));
    data::achievements(ga, 1, rows);
    int unlocked = 0;
    for (auto& r : rows) unlocked += r.unlocked_at > 0;
    check("principal recomeça do zero (3 conquistas, nenhuma desbloqueada)", rows.size() == 3 && unlocked == 0);
    check("progresso do Speedrun também apagado",
          q("SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE 'L:0100AAAA00000000:%'") == 0);
    check("arquivo inexistente recusado", !data::set_active_pack(ga, "0100AAAA00000000.nada.json"));
    check("nova sincronização não duplica", data::sync_achievement_packs() &&
                                                 q("SELECT COUNT(*) FROM achievements WHERE game_id=?", ga) == 3 &&
                                                 q("SELECT COUNT(*) FROM games WHERE title_id='0100AAAA00000000'") == 1);

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
