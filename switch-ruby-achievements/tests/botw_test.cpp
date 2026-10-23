#include "achieve.cpp"
#include <utime.h>
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-58s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static void exec(sqlite3* db, const char* sql) {
    char* err = 0;
    sqlite3_exec(db, sql, 0, 0, &err);
    if (err) printf("SQL: %s\n", err);
}
static std::string unlocked_list(sqlite3* db) {
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db, "SELECT achievement_id FROM user_achievements ORDER BY achievement_id", -1, &st, 0);
    std::string out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::string id = (const char*)sqlite3_column_text(st, 0);
        out += id.substr(id.rfind(':') + 1) + " ";
    }
    sqlite3_finalize(st);
    return out;
}

// Save no formato do BOTW: cabeçalho de 0x0C bytes e entradas [crc32][valor].
struct Save {
    std::vector<char> b = std::vector<char>(0x0C, 0);
    void set(const char* name, uint32_t v) {
        uint32_t h = crc32_ieee(name);
        for (size_t p = 0x0C; p + 8 <= b.size(); p += 8)
            if (*(uint32_t*)&b[p] == h) {
                *(uint32_t*)&b[p + 4] = v;
                return;
            }
        size_t p = b.size();
        b.resize(p + 8);
        *(uint32_t*)&b[p] = h;
        *(uint32_t*)&b[p + 4] = v;
    }
    void filler(int n) {  // flags que não importam, como no save real
        char name[32];
        for (int i = 0; i < n; i++) {
            snprintf(name, sizeof name, "Filler_%d", i);
            set(name, i);
        }
    }
    void write(const char* path, time_t mtime) {
        FILE* f = fopen(path, "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        struct utimbuf u = {mtime, mtime};
        utime(path, &u);
    }
};

int main() {
    for (const char* d : {"sdmc:", "sdmc:/0", "sdmc:/switch", "sdmc:/switch/ruby_achievements", "sdmc:/switch/ruby_achievements/achievements",
                          "sdmc:/switch/ruby_achievements/cache"})
        mkdir(d, 0777);
    // Pacote real, copiado do sd_card_structure.
    {
        FILE* in = fopen(PT_REPO "/sd_card_structure/switch/ruby_achievements/"
                         "achievements/01007EF00011E000.json", "rb");
        FILE* out = fopen("sdmc:/switch/ruby_achievements/achievements/01007EF00011E000.json", "wb");
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(in);
        fclose(out);
    }
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
         "INSERT INTO users(id,account_uid,nickname,created_at) VALUES(1,'0000000000000001000000000000000A','Link',0);"
         "INSERT INTO games(id,title_id,display_name,source_type,created_at,updated_at) "
         "VALUES(1,'01007EF00011E000','Zelda BOTW','NATIVE',0,0);");

    Save s;
    s.filler(60000);  // ~480 KB, como um save real
    s.set("IsGet_PlayerStole2", 1);
    for (int i = 1; i <= 15; i++) {
        char t[16];
        snprintf(t, sizeof t, "MapTower_%02d", i);
        s.set(t, 1);
    }
    s.set("DungeonClearCounter", 42);
    s.set("Clear_RemainsWater", 1);
    s.set("Clear_RemainsFire", 1);
    s.set("Clear_RemainsWind", 0);
    s.set("IsGet_MemoryPhoto_003", 1);
    s.set("DefeatedForestGiantNum", 2);
    s.set("GameClear", 0);
    s.write("sdmc:/0/game_data.sav", 5000);

    AchieveTarget live{1, 1, 0x01007EF00011E000ULL, "", true};
    achieve_check(db, live, 100); achieve_commit();
    std::string got = unlocked_list(db);
    printf("desbloqueadas: %s\n", got.c_str());
    check("9 conquistas do save simulado",
          got == "beast_rudania beast_ruta hinox memory_first paraglider shrine_1 shrine_30 tower_all tower_first ");
    check("22 conquistas registradas na galeria (21 + Ruby)",
          pt_db_scalar(db, "SELECT COUNT(*) FROM achievements WHERE game_id=1", 0, 0, 0) == 22);
    check("pacote BOTW: uma Ruby, ainda bloqueada (faltam conquistas)",
          pt_db_scalar(db, "SELECT COUNT(*) FROM achievements WHERE rarity='ruby'", 0, 0, 0) == 1 &&
              pt_db_scalar(db, "SELECT COUNT(*) FROM user_achievements WHERE achievement_id LIKE '%:platina'", 0, 0, 0) == 0);
    check("Ganon é secreta", pt_db_scalar(db, "SELECT is_secret FROM achievements WHERE id LIKE '%:ganon'", 0, 0, 0) == 1);

    // Autosave: mais dois Animais Divinos e Ganon -> beast_all e ganon.
    s.set("Clear_RemainsWind", 1);
    s.set("Clear_RemainsElectric", 1);
    s.set("GameClear", 1);
    s.set("DungeonClearCounter", 120);
    s.write("sdmc:/0/game_data.sav", 6000);
    achieve_check(db, live, 130); achieve_commit();
    got = unlocked_list(db);
    check("autosave: beast_all, ganon, shrine_60 e shrine_120",
          got.find("beast_all") != std::string::npos && got.find("ganon") != std::string::npos &&
              got.find("shrine_60") != std::string::npos && got.find("shrine_120") != std::string::npos &&
              got.find("beast_medoh") != std::string::npos && got.find("beast_naboris") != std::string::npos);
    check("talus e molduga continuam bloqueados (flag ausente)",
          got.find("talus") == std::string::npos && got.find("molduga") == std::string::npos);
    check("memory_all bloqueada (1 de 14)", got.find("memory_all") == std::string::npos);
    check("fila de pop-ups = desbloqueios",
          pt_db_scalar(db, "SELECT COUNT(*) FROM achievement_queue", 0, 0, 0) ==
              pt_db_scalar(db, "SELECT COUNT(*) FROM user_achievements", 0, 0, 0));

    // count_of misto (tempo + save) com o save igual: o save é lido mesmo assim.
    EvalCtx c{db, live, false, {}, {}};
    const char* mixed =
        "{\"type\":\"count_of\",\"conditions\":[{\"type\":\"playtime_stat\",\"target_seconds\":3600},"
        "{\"type\":\"save_flag_hash_search\",\"save_file\":\"game_data.sav\",\"flag_name\":\"IsGet_PlayerStole2\","
        "\"hash_type\":\"crc32\",\"expected_value_int\":1}]}";
    JVal m;
    json_parse(mixed, strlen(mixed), m);
    want_hashes(c, m);
    check("count_of misto: falta o tempo", !satisfied(c, m));
    exec(db, "INSERT INTO sessions(game_id,user_id,start_time,duration,created_at) VALUES(1,1,0,4000,0)");
    EvalCtx c2{db, live, false, {}, {}};
    want_hashes(c2, m);
    check("count_of misto: tempo cumprido com o save igual", satisfied(c2, m));
    const char* bad = "{\"type\":\"count_of\",\"min\":5,\"conditions\":[{\"type\":\"playtime_stat\",\"target_seconds\":1}]}";
    JVal b;
    json_parse(bad, strlen(bad), b);
    check("count_of com min maior que a lista: nunca", !satisfied(c2, b));
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
