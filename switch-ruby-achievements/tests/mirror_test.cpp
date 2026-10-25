#include "achieve.cpp"
#include "rubydb_writer.hpp"
#include <utime.h>
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-54s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static void put32(std::vector<char>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i));
}
static void set_mtime(const char* path, time_t t) {
    struct utimbuf u = {t, t};
    utime(path, &u);
}
static void write_save(const char* path, std::vector<char>& b, time_t mtime) {
    FILE* f = fopen(path, "wb");
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
    set_mtime(path, mtime);
}
static int count(sqlite3* db, const char* sql) {
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db, sql, -1, &st, 0);
    sqlite3_step(st);
    int n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}
static bool has(sqlite3* db, const char* id) {
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db, "SELECT 1 FROM user_achievements WHERE achievement_id=?", -1, &st, 0);
    sqlite3_bind_text(st, 1, id, -1, 0);
    bool y = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return y;
}
static void exec(sqlite3* db, const char* sql) {
    char* err = 0;
    sqlite3_exec(db, sql, 0, 0, &err);
    if (err) printf("SQL: %s\n", err);
}

int main() {
    for (const char* d : {"sdmc:", "sdmc:/0", "sdmc:/1", "sdmc:/manual", "sdmc:/manual/0", "sdmc:/switch",
                          "sdmc:/switch/ruby_achievements", "sdmc:/switch/ruby_achievements/achievements", "sdmc:/switch/ruby_achievements/cache"})
        mkdir(d, 0777);

    // --- resolve_active_save_path ---
    std::vector<char> b(150000, 0);
    write_save("sdmc:/0/game_data.sav", b, 1000);
    write_save("sdmc:/1/game_data.sav", b, 3000);
    write_save("sdmc:/manual/0/game_data.sav", b, 2000);
    FsFileSystem fs{7};
    check("resolver: mais recente (1/)", resolve_active_save_path(&fs, "/", "game_data.sav") == "/1/game_data.sav");
    set_mtime("sdmc:/manual/0/game_data.sav", 5000);
    check("resolver: subpasta funda mais recente (manual/0/)",
          resolve_active_save_path(&fs, "/", "game_data.sav") == "/manual/0/game_data.sav");
    check("resolver: caminho explícito", resolve_active_save_path(&fs, "/", "0/game_data.sav") == "/0/game_data.sav");
    check("resolver: inexistente -> padrão", resolve_active_save_path(&fs, "/", "nada.sav") == "/nada.sav");

    // --- fluxo completo: espelho, pop-up e detecção de mudança ---
    sqlite3* db;
    sqlite3_open(":memory:", &db);
    FILE* sf = fopen(PT_REPO "/schema.sql", "rb");
    std::string schema;
    char cb[4096];
    size_t n;
    while ((n = fread(cb, 1, sizeof cb, sf)) > 0) schema.append(cb, n);
    fclose(sf);
    exec(db, schema.c_str());
    exec(db,
         "INSERT INTO users(id,account_uid,nickname,created_at) VALUES(1,'0000000000000001000000000000000A','Ana',0);"
         "INSERT INTO games(id,title_id,display_name,source_type,created_at,updated_at) "
         "VALUES(1,'01007EF00011E000','Zelda','NATIVE',0,0);");
    const char* defs_path = "sdmc:/switch/ruby_achievements/achievements/01007EF00011E000.json";
    FILE* d = fopen(defs_path, "wb");
    fputs("{\"achievements\":["
          "{\"id\":\"glider\",\"title\":\"Asa\",\"type\":\"save_flag_hash_search\",\"save_file\":\"game_data.sav\","
          "\"flag_name\":\"IsGet_Paraglider\",\"hash_type\":\"crc32\",\"expected_value_int\":1},"
          "{\"id\":\"sword\",\"title\":\"Espada\",\"type\":\"save_flag_hash_search\",\"save_file\":\"game_data.sav\","
          "\"flag_name\":\"Flag_Sword\",\"hash_type\":\"crc32\",\"expected_value_int\":1},"
          "{\"id\":\"txt\",\"title\":\"Texto\",\"type\":\"save_flag_search\",\"save_file\":\"game_data.sav\","
          "\"flag_name\":\"KOROK\",\"offset_after_flag\":0,\"expected_value_int\":3}"
          "]}",
          d);
    fclose(d);
    rubydb::from_json_file(defs_path);
    const char* pack_path = "sdmc:/switch/ruby_achievements/achievements/01007EF00011E000.rubydb";

    // Save ativo = manual/0 (mais recente): Asa=1, Espada=0, KOROK=3.
    put32(b, 0x0C + 8 * 5000, crc32_ieee("IsGet_Paraglider"));
    put32(b, 0x0C + 8 * 5000 + 4, 1);
    put32(b, 0x0C + 8 * 9000, crc32_ieee("Flag_Sword"));
    put32(b, 0x0C + 8 * 9000 + 4, 0);
    memcpy(&b[120000], "KOROK", 5);
    b[120005] = 3;
    write_save("sdmc:/manual/0/game_data.sav", b, 6000);
    // O slot 0 (mais antigo) tem a espada: não deve contar.
    std::vector<char> old = b;
    put32(old, 0x0C + 8 * 9000 + 4, 1);
    write_save("sdmc:/0/game_data.sav", old, 1500);

    AchieveTarget live{1, 1, 0x01007EF00011E000ULL, "", true};
    achieve_check(db, live, 100); achieve_commit();
    check("jogo aberto: asa e texto desbloqueados",
          has(db, "L:01007EF00011E000:glider") && has(db, "L:01007EF00011E000:txt"));
    check("jogo aberto: espada do slot antigo ignorada", !has(db, "L:01007EF00011E000:sword"));
    check("fila de pop-up com 2", count(db, "SELECT COUNT(*) FROM achievement_queue") == 2);
    struct stat st;
    check("cópia espelho no cache",
          stat("sdmc:/switch/ruby_achievements/cache/01007EF00011E000_1_manual_0_game_data.sav", &st) == 0 &&
              st.st_size == 150000);
    achieve_export_popups(db);
    char pop[1024] = {0};
    FILE* pf = fopen("sdmc:/switch/ruby_achievements/achievement_popup.txt", "rb");
    if (pf) {
        fread(pop, 1, sizeof pop - 1, pf);
        fclose(pf);
    }
    check("achievement_popup.txt tem Asa e Texto", strstr(pop, "|Asa\n") && strstr(pop, "|Texto\n"));

    // Conteúdo muda com data e tamanho iguais: não relê (economia esperada).
    put32(b, 0x0C + 8 * 9000 + 4, 1);
    write_save("sdmc:/manual/0/game_data.sav", b, 6000);
    int opens = g_save_opens;
    achieve_check(db, live, 130); achieve_commit();
    check("data igual: não relê (espada ainda bloqueada)", !has(db, "L:01007EF00011E000:sword"));
    check("monta o save uma vez por avaliação", g_save_opens == opens + 1);
    // Autosave: data nova -> copia de novo e desbloqueia com o jogo aberto.
    set_mtime("sdmc:/manual/0/game_data.sav", 7000);
    achieve_check(db, live, 160); achieve_commit();
    check("autosave: espada desbloqueada com o jogo aberto", has(db, "L:01007EF00011E000:sword"));
    check("3 desbloqueios, sem duplicar", count(db, "SELECT COUNT(*) FROM user_achievements") == 3);

    // FS recusa com o jogo aberto: nada quebra; ao fechar, avalia.
    exec(db, "DELETE FROM user_achievements WHERE achievement_id LIKE '%sword'");
    g_evaluated.clear();
    g_save_rc = 0x7D402;
    achieve_check(db, live, 190); achieve_commit();
    check("FS recusa: não desbloqueia nem trava", !has(db, "L:01007EF00011E000:sword"));
    g_save_rc = 0;
    AchieveTarget closed = live;
    closed.running = false;
    achieve_check(db, closed, 220); achieve_commit();
    check("ao fechar: desbloqueia", has(db, "L:01007EF00011E000:sword"));

    // Save e definições iguais: não reavalia; definições mudam: reavalia.
    exec(db, "DELETE FROM user_achievements WHERE achievement_id LIKE '%glider'");
    achieve_check(db, closed, 250); achieve_commit();
    check("save e definições iguais: não reavalia", !has(db, "L:01007EF00011E000:glider"));
    d = fopen(pack_path, "ab");  // muda o tamanho do pacote (o SQLite ignora o byte a mais)
    fputs(" ", d);
    fclose(d);
    achieve_check(db, closed, 280); achieve_commit();
    check("definições mudaram: reavalia", has(db, "L:01007EF00011E000:glider"));

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
