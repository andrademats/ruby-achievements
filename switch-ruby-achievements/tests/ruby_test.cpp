// Raridade e Ruby (platina): migração da coluna, desbloqueio automático depois da
// última das outras, pop-up dela por último e formato "id|raridade|ícone|título".
// Rodar duas vezes no mesmo diretório: "prep" (banco sem a coluna, como os antigos)
// e depois sem argumento.
#include "achieve.cpp"
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-62s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static int64_t q(sqlite3* db, const char* sql) { return pt_db_scalar(db, sql, 0, 0, -1); }
static std::string qs(sqlite3* db, const char* sql) {
    sqlite3_stmt* st = NULL;
    std::string out = "(nada)";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        out = sqlite3_column_text(st, 0) ? (const char*)sqlite3_column_text(st, 0) : "(null)";
    sqlite3_finalize(st);
    return out;
}
// Arquivo no SD com a flag ligada (save_flag_search: valor 4 bytes depois do nome).
static void flag_file(const char* path) {
    FILE* f = fopen(path, "wb");
    fwrite("FLAGxxxx\x01", 1, 9, f);
    fclose(f);
}
static void write_pack(const char* tid, const char* body) {
    std::string p = std::string(PT_ACH_DIR) + "/" + tid + ".json";
    FILE* f = fopen(p.c_str(), "wb");
    fputs(body, f);
    fclose(f);
}
#define COND(file) "\"type\":\"save_flag_search\",\"save_file\":\"" file "\",\"flag_name\":\"FLAG\",\"size\":1,\"expected_value_int\":1"

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (const char* d : {"sdmc:", "sdmc:/switch", PT_DIR, PT_ACH_DIR, PT_DIR "/cache", "sdmc:/sv"}) mkdir(d, 0777);
    if (argc > 1 && !strcmp(argv[1], "prep")) {
        sqlite3* db = pt_db_open(300);
        sqlite3_exec(db, "ALTER TABLE achievements DROP COLUMN rarity", 0, 0, 0);
        check("preparo: banco antigo sem a coluna rarity",
              q(db, "SELECT COUNT(*) FROM pragma_table_info('achievements') WHERE name='rarity'") == 0);
        pt_db_close(db);
        return fails;
    }

    sqlite3* db = pt_db_open(300);
    check("migração: coluna rarity criada no banco antigo",
          q(db, "SELECT COUNT(*) FROM pragma_table_info('achievements') WHERE name='rarity'") == 1);
    sqlite3_exec(db,
                 "INSERT INTO users(id,nickname,created_at) VALUES(1,'P',0);"
                 "INSERT INTO games(id,display_name,source_type,created_at,updated_at) VALUES(1,'G','NATIVE',0,0);",
                 0, 0, 0);

    // Ruby em primeiro no arquivo, com uma condição (ignorada) e uma segunda Ruby (fora).
    write_pack("0100000000000001",
               "{\"achievements\":[\n"
               "{\"id\":\"plat\",\"title\":\"Platina\",\"rarity\":\"ruby\"," COND("/sv/nunca.sav") "},\n"
               "{\"id\":\"a\",\"title\":\"A\",\"icon\":\"icons/x/a.png\"," COND("/sv/a.sav") "},\n"
               "{\"id\":\"b\",\"title\":\"B\",\"rarity\":\"silver\"," COND("/sv/b.sav") "},\n"
               "{\"id\":\"c\",\"title\":\"C | ouro\",\"rarity\":\"gold\"," COND("/sv/c.sav") "},\n"
               "{\"id\":\"plat2\",\"title\":\"Outra\",\"rarity\":\"ruby\"}\n"
               "]}");
    AchieveTarget t{1, 1, 0x0100000000000001ULL, "", false};
    auto run = [&] {
        g_fake_now += 10;
        achieve_check(db, t, g_fake_now);
        achieve_commit();
    };

    flag_file("sdmc:/sv/a.sav");
    run();
    check("1 de 3: só A desbloqueada", q(db, "SELECT COUNT(*) FROM user_achievements") == 1);
    check("raridade padrão = bronze", qs(db, "SELECT rarity FROM achievements WHERE id LIKE '%:a'") == "bronze");
    check("raridade silver gravada", qs(db, "SELECT rarity FROM achievements WHERE id LIKE '%:b'") == "silver");
    check("Ruby gravada como ruby", qs(db, "SELECT rarity FROM achievements WHERE id LIKE '%:plat'") == "ruby");
    check("segunda Ruby fica de fora", q(db, "SELECT COUNT(*) FROM achievements WHERE id LIKE '%:plat2'") == 0);

    flag_file("sdmc:/sv/b.sav");
    run();
    check("2 de 3: Ruby ainda bloqueada",
          q(db, "SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE '%:plat'") == 0);

    flag_file("sdmc:/sv/c.sav");
    run();
    check("3 de 3: C e a Ruby na mesma avaliação", q(db, "SELECT COUNT(*) FROM user_achievements") == 4);
    check("Ruby com o mesmo horário da última",
          q(db, "SELECT COUNT(DISTINCT unlocked_at) FROM user_achievements WHERE achievement_id LIKE '%:c' "
                "OR achievement_id LIKE '%:plat'") == 1);
    check("Ruby é a última da fila",
          qs(db, "SELECT achievement_id FROM achievement_queue ORDER BY id DESC LIMIT 1").find(":plat") !=
              std::string::npos);

    achieve_export_popups(db);
    char buf[2048] = {0};
    pt_read_file(PT_POPUP_PATH, buf, sizeof(buf) - 1);
    std::string text = buf;
    std::string last = text.substr(text.rfind('\n', text.size() - 2) + 1);
    check("pop-up da Ruby: \"id|ruby||Platina\"", last.find("|ruby||Platina\n") != std::string::npos);
    check("pop-up com ícone: caminho no SD",
          text.find("|bronze|" PT_ACH_DIR "/icons/x/a.png|A\n") != std::string::npos);
    check("pop-up: '|' do título vira espaço", text.find("|gold||C   ouro\n") != std::string::npos);
    check("pop-up sem descrição (4 campos)", std::count(last.begin(), last.end(), '|') == 3);

    run();
    check("nova avaliação: nada repetido", q(db, "SELECT COUNT(*) FROM user_achievements") == 4);

    // Pacote só com a Ruby: nunca desbloqueia sozinha.
    write_pack("0100000000000002", "{\"achievements\":[{\"id\":\"p\",\"title\":\"P\",\"rarity\":\"ruby\"}]}");
    sqlite3_exec(db, "INSERT INTO games(id,display_name,source_type,created_at,updated_at) VALUES(2,'H','NATIVE',0,0);",
                 0, 0, 0);
    t = {2, 1, 0x0100000000000002ULL, "", false};
    run();
    check("pacote só com a Ruby: não desbloqueia", q(db, "SELECT COUNT(*) FROM user_achievements") == 4);

    // Pacote antigo, sem raridade e sem Ruby: segue igual.
    write_pack("0100000000000003", "{\"achievements\":[{\"id\":\"x\",\"title\":\"X\"," COND("/sv/a.sav") "}]}");
    sqlite3_exec(db, "INSERT INTO games(id,display_name,source_type,created_at,updated_at) VALUES(3,'I','NATIVE',0,0);",
                 0, 0, 0);
    t = {3, 1, 0x0100000000000003ULL, "", false};
    run();
    check("pacote antigo sem Ruby: desbloqueia normal", q(db, "SELECT COUNT(*) FROM user_achievements") == 5);

    // --- Troca de pacote feita no app, vista pelo sysmodule (2026-10-21).
    const char* kOne = "SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE 'L:0100000000000001:%'";
    achieve_packs_changed();  // estado inicial
    write_pack("0100000000000001.hard", "{\"achievements\":[{\"id\":\"h\",\"title\":\"H\"," COND("/sv/a.sav") "}]}");
    std::string err;
    check("troca para a variante (pt_pack_switch)",
          pt_pack_switch(db, "0100000000000001", "0100000000000001.hard.json", &err));
    check("progresso do principal apagado na troca", q(db, kOne) == 0);
    check("sysmodule percebe a troca (active_packs.txt)", achieve_packs_changed());
    check("e só uma vez", !achieve_packs_changed());
    t = {1, 1, 0x0100000000000001ULL, "", false};
    run();
    check("variante: só ela é lida (H com o save que já existia)",
          q(db, kOne) == 1 && q(db, "SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE '%:h'") == 1);
    check("lista do jogo só com a variante",
          q(db, "SELECT COUNT(*) FROM achievements WHERE id LIKE 'L:0100000000000001:%'") == 1);
    check("volta ao principal", pt_pack_switch(db, "0100000000000001", "0100000000000001.json", &err));
    run();
    check("principal do zero: saves iguais relidos, as 3 e a Ruby de novo", q(db, kOne) == 4);

    // --- Fila longa (proteção contra flood do overlay): o arquivo leva até PT_POPUP_QUEUE_MAX.
    sqlite3_exec(db, "DELETE FROM achievement_queue;", 0, 0, 0);
    for (int i = 0; i < 28; i++)
        sqlite3_exec(db, "INSERT INTO achievement_queue(achievement_id,user_id,created_at) "
                         "SELECT achievement_id, 1, 0 FROM user_achievements LIMIT 1;", 0, 0, 0);
    remove(PT_POPUP_SEEN_PATH);
    achieve_export_popups(db);
    char big[16384] = {0};
    pt_read_file(PT_POPUP_PATH, big, sizeof(big) - 1);
    check("fila de 28: arquivo com as 16 mais antigas (PT_POPUP_QUEUE_MAX)",
          std::count(big, big + strlen(big), '\n') == PT_POPUP_QUEUE_MAX && PT_POPUP_QUEUE_MAX >= 10);

    pt_db_close(db);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
