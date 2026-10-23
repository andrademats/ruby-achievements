// ram_flag_search sobre memória simulada (assinatura, cadeia de ponteiros, hash).
#include "achieve.cpp"
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;

struct FakeRegion {
    u64 base;
    std::vector<uint8_t> bytes;
};
extern std::vector<FakeRegion> g_fake_regions;
extern RamProcess g_fake_proc;
extern bool g_fake_dmnt, g_fake_session;
extern int g_fake_opens, g_fake_closes;
extern long g_fake_read_bytes;
uint8_t* fake_at(u64 addr);

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static void exec(sqlite3* db, const char* sql) {
    char* err = 0;
    sqlite3_exec(db, sql, 0, 0, &err);
    if (err) printf("SQL: %s\n", err);
}
static bool has(sqlite3* db, const char* aid) {
    std::string id = std::string("L:0100000000ABC000:") + aid;
    return pt_db_scalar(db, "SELECT COUNT(*) FROM user_achievements WHERE achievement_id=?1", 0, 0, 0) >= 0 &&
           [&] {
               sqlite3_stmt* st;
               sqlite3_prepare_v2(db, "SELECT 1 FROM user_achievements WHERE achievement_id=?", -1, &st, 0);
               sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
               bool y = sqlite3_step(st) == SQLITE_ROW;
               sqlite3_finalize(st);
               return y;
           }();
}
static void put(u64 addr, const void* p, size_t n) { memcpy(fake_at(addr), p, n); }
static void put32(u64 addr, uint32_t v) { put(addr, &v, 4); }
static void put64(u64 addr, uint64_t v) { put(addr, &v, 8); }
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

int main() {
    for (const char* d : {"sdmc:", "sdmc:/switch", "sdmc:/switch/ruby_achievements", "sdmc:/switch/ruby_achievements/achievements",
                          "sdmc:/switch/ruby_achievements/cache"})
        mkdir(d, 0777);
    const u64 kMain = 0x8000000, kMainSize = 2 << 20, kHeap = 0x20000000;
    g_fake_regions.push_back({kMain, std::vector<uint8_t>(kMainSize)});
    g_fake_regions.push_back({kHeap, std::vector<uint8_t>(1 << 20, 0)});
    uint32_t x = 12345;
    for (auto& b : g_fake_regions[0].bytes) b = (uint8_t)((x = x * 1103515245u + 12345u) >> 24);
    g_fake_proc = {77, 0x0100000000ABC000ULL, kMain, kMainSize};

    // Assinatura única em main+0x123456, cadeia [sig+0x10] -> P1, [P1+0x20] -> P2.
    const uint8_t sig[] = {0xFD, 0x7B, 0xC1, 0xA9, 0xF4, 0x4F, 0x01, 0xA9};
    const u64 at = kMain + 0x123456, p1 = kHeap + 0x1000, p2 = kHeap + 0x40000;
    put(at, sig, sizeof sig);
    put64(at + 0x10, p1);
    put64(p1 + 0x20, p2);
    put64(kMain + 0x200, p2);  // [main+0x200] -> P2 (sem assinatura)
    // Janela em P2: flags como hash+valor.
    put32(p2 + 0x800, crc32_ieee("IsGet_PlayerStole2"));
    put32(p2 + 0x804, 1);
    put32(p2 + 0x900, murmur3_32("Flag_X"));
    put32(p2 + 0x904, 0);
    put32(p2 + 0xA00, 0xCAFEBABE);
    put32(p2 + 0xA08, 7);
    // Assinatura ambígua: aparece duas vezes.
    const uint8_t amb[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    put(kMain + 0x1000, amb, sizeof amb);
    put(kMain + 0x1F0000, amb, sizeof amb);

    FILE* d = fopen("sdmc:/switch/ruby_achievements/achievements/0100000000ABC000.json", "wb");
    fputs("{\"achievements\":["
          "{\"id\":\"glider\",\"title\":\"Asa\",\"type\":\"ram_flag_search\",\"signature\":\"FD 7B ?? A9 F4 4F 01 A9\","
          "\"pointer_offsets\":[\"0x10\",32],\"flag_name\":\"IsGet_PlayerStole2\",\"hash_type\":\"crc32\"},"
          "{\"id\":\"xflag\",\"title\":\"X\",\"type\":\"ram_flag_search\",\"signature\":\"FD7BC1A9F44F01A9\","
          "\"pointer_offsets\":[\"0x10\",\"0x20\"],\"flag_name\":\"Flag_X\",\"expected_value_int\":1},"
          "{\"id\":\"byhash\",\"title\":\"H\",\"type\":\"ram_flag_search\",\"signature\":\"FD 7B C1 A9 F4 4F 01 A9\","
          "\"pointer_offsets\":[\"0x10\",\"0x20\"],\"flag_hash\":\"0xCAFEBABE\",\"offset_after_flag\":8,\"size\":1,"
          "\"operator\":\">=\",\"expected_value_int\":5},"
          "{\"id\":\"mainrel\",\"title\":\"M\",\"type\":\"ram_flag_search\",\"pointer_offsets\":[\"0x200\"],"
          "\"flag_name\":\"IsGet_PlayerStole2\",\"hash_type\":\"crc32\"},"
          "{\"id\":\"amb\",\"title\":\"A\",\"type\":\"ram_flag_search\",\"signature\":\"11 22 33 44 55 66\","
          "\"flag_name\":\"IsGet_PlayerStole2\",\"hash_type\":\"crc32\"},"
          "{\"id\":\"missing\",\"title\":\"N\",\"type\":\"ram_flag_search\",\"signature\":\"DE AD BE EF 00 11 22 33\","
          "\"flag_name\":\"IsGet_PlayerStole2\",\"hash_type\":\"crc32\"},"
          "{\"id\":\"nullptr\",\"title\":\"P\",\"type\":\"ram_flag_search\",\"signature\":\"FD 7B C1 A9 F4 4F 01 A9\","
          "\"pointer_offsets\":[\"0x10\",\"0x20\",\"0x30\"],\"flag_name\":\"IsGet_PlayerStole2\",\"hash_type\":\"crc32\"},"
          "{\"id\":\"bad\",\"title\":\"B\",\"type\":\"ram_flag_search\",\"signature\":\"FD 7\",\"flag_name\":\"x\"},"
          "{\"id\":\"mix\",\"title\":\"C\",\"type\":\"count_of\",\"conditions\":["
          "{\"type\":\"playtime_stat\",\"target_seconds\":10},{\"type\":\"ram_flag_search\",\"pointer_offsets\":[\"0x200\"],"
          "\"flag_name\":\"Flag_X\"}]}"
          "]}",
          d);
    fclose(d);

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
         "VALUES(1,'0100000000ABC000','Jogo','NATIVE',0,0);"
         "INSERT INTO sessions(game_id,user_id,start_time,duration,created_at) VALUES(1,1,0,100,0);");

    AchieveTarget live{1, 1, 0x0100000000ABC000ULL, "", true};
    achieve_check(db, live, 100);
    achieve_commit();
    check("assinatura (com ??) + cadeia [0x10, 32]: desbloqueia", has(db, "glider"));
    check("flag_hash em hex, offset 8, size 1, >=", has(db, "byhash"));
    check("sem assinatura: [main+0x200]", has(db, "mainrel"));
    check("murmur3 com valor 0: bloqueada", !has(db, "xflag"));
    check("assinatura ambígua: bloqueada", !has(db, "amb"));
    check("assinatura ausente: bloqueada", !has(db, "missing"));
    check("ponteiro nulo na cadeia: bloqueada, sem travar", !has(db, "nullptr"));
    check("assinatura inválida: bloqueada", !has(db, "bad"));
    check("count_of tempo + RAM (flag 0): bloqueada", !has(db, "mix"));
    check("log: ambígua", log_count("ambigua") == 1);
    check("log: não encontrada", log_count("nao encontrada") == 1);
    check("uma conexão ao dmnt:cht, fechada no fim", g_fake_opens == 1 && g_fake_closes == 1 && !g_fake_session);
    check("pop-ups na fila", pt_db_scalar(db, "SELECT COUNT(*) FROM achievement_queue", 0, 0, 0) == 3);

    // Flag muda na memória com o jogo aberto: desbloqueia na próxima avaliação,
    // sem refazer o pattern scan.
    put32(p2 + 0x904, 1);
    long before = g_fake_read_bytes;
    achieve_check(db, live, 130);
    achieve_commit();
    long reread = g_fake_read_bytes - before;
    check("flag mudou na RAM: desbloqueia com o jogo aberto", has(db, "xflag"));
    check("count_of tempo + RAM: desbloqueia", has(db, "mix"));
    check("2ª avaliação sem novo pattern scan (< 1 módulo lido)", reread < (long)kMainSize);
    check("ambígua/ausente sem novo log", log_count("ambigua") == 1 && log_count("nao encontrada") == 1);

    // Jogo fechado: nada de memória.
    int opens = g_fake_opens;
    AchieveTarget closed = live;
    closed.running = false;
    achieve_check(db, closed, 160);
    achieve_commit();
    check("jogo fechado: não conecta ao dmnt:cht", g_fake_opens == opens);

    // Processo novo (mesmo jogo reaberto): assinaturas procuradas de novo.
    exec(db, "DELETE FROM user_achievements WHERE achievement_id LIKE '%glider'");
    g_fake_proc.pid = 78;
    before = g_fake_read_bytes;
    achieve_check(db, live, 190);
    achieve_commit();
    check("processo novo: novo pattern scan", g_fake_read_bytes - before >= (long)kMainSize);
    check("processo novo: desbloqueia de novo", has(db, "glider"));

    // Outro jogo no dmnt (Title ID diferente): não lê.
    exec(db, "DELETE FROM user_achievements WHERE achievement_id LIKE '%glider'");
    g_fake_proc.tid = 0x0100000000DEF000ULL;
    before = g_fake_read_bytes;
    achieve_check(db, live, 220);
    achieve_commit();
    check("dmnt ligado a outro jogo: não lê a memória", g_fake_read_bytes == before && !has(db, "glider"));
    check("log: indisponível uma vez", log_count("indisponivel (dmnt") == 1);
    g_fake_dmnt = false;
    achieve_check(db, live, 250);
    achieve_commit();
    check("sem dmnt:cht: sem novo log", log_count("indisponivel (dmnt") == 1);
    g_fake_dmnt = true;
    g_fake_proc.tid = 0x0100000000ABC000ULL;
    achieve_check(db, live, 280);
    achieve_commit();
    check("dmnt de volta: desbloqueia", has(db, "glider"));

    // --- Layout do BOTW na RAM (decomp zeldaret/botw, gdtFlag.h) ---
    // FlagBool 0x18: vptr, type, props, config, valor (0x0B, bit 0), hash (0x0C).
    // FlagS32 0x28: ..., valor (0x18), hash (0x1C). [main+0x300] -> array de
    // ponteiros -> [0] = primeiro objeto.
    const u64 objs = kHeap + 0x80000, ptrs = kHeap + 0x90000;
    auto flag_bool = [&](u64 o, const char* name, uint8_t value) {
        put64(o, 0x7100123450ULL);
        *fake_at(o + 0x0B) = value;
        put32(o + 0x0C, crc32_ieee(name));
    };
    flag_bool(objs, "IsGet_PlayerStole2", 0x03);  // bit 0 = 1 (outros bits são metadados)
    flag_bool(objs + 0x18, "MapTower_01", 0x02);  // bit 0 = 0
    put32(objs + 0x100 + 0x18, 42);
    put32(objs + 0x100 + 0x1C, crc32_ieee("DungeonClearCounter"));
    put64(ptrs, objs);
    put64(kMain + 0x300, ptrs);
    put64(kMain + 0x308, 0);
    d = fopen("sdmc:/switch/ruby_achievements/achievements/0100000000BBB000.json", "wb");
    const char* chain = "\"pointer_offsets\":[\"0x300\",\"0x0\"],\"search_size\":\"0x400000\",\"hash_type\":\"crc32\"";
    std::string defs = std::string("{\"achievements\":[") +
        "{\"id\":\"b_glider\",\"title\":\"G\",\"icon\":\"icons/0100000000BBB000/b_glider.png\",\"type\":\"ram_flag_search\"," + chain +
        ",\"flag_name\":\"IsGet_PlayerStole2\",\"offset_after_flag\":-1,\"size\":1,\"operator\":\"bit_check\"},"
        "{\"id\":\"b_tower\",\"title\":\"T\",\"type\":\"ram_flag_search\"," + chain +
        ",\"flag_name\":\"MapTower_01\",\"offset_after_flag\":-1,\"size\":1,\"operator\":\"bit_check\"},"
        "{\"id\":\"b_shrine\",\"title\":\"S\",\"type\":\"ram_flag_search\"," + chain +
        ",\"flag_name\":\"DungeonClearCounter\",\"offset_after_flag\":-4,\"operator\":\">=\",\"expected_value_int\":30},"
        "{\"id\":\"b_fill\",\"title\":\"F\",\"type\":\"ram_flag_search\",\"pointer_offsets\":[\"PREENCHER_sInstance\"],"
        "\"flag_name\":\"x\"},"
        "{\"id\":\"b_chain\",\"title\":\"C\",\"type\":\"ram_flag_search\",\"pointer_offsets\":[\"0x308\",\"0x0\"],"
        "\"flag_name\":\"x\"},"
        "{\"id\":\"b_bad\",\"title\":\"B\",\"type\":\"ram_flag_search\",\"pointer_offsets\":[\"0x300000\",\"0x0\"],"
        "\"flag_name\":\"x\"}"
        "]}";
    fputs(defs.c_str(), d);
    fclose(d);
    exec(db, "INSERT INTO games(id,title_id,display_name,source_type,created_at,updated_at) "
             "VALUES(2,'0100000000BBB000','BOTW-like','NATIVE',0,0);");
    g_fake_proc = {90, 0x0100000000BBB000ULL, kMain, kMainSize};
    AchieveTarget botw{2, 1, 0x0100000000BBB000ULL, "", true};
    auto hasb = [&](const char* aid) {
        return pt_db_scalar(db, ("SELECT COUNT(*) FROM user_achievements WHERE achievement_id='L:0100000000BBB000:" +
                                 std::string(aid) + "'").c_str(), 0, 0, 0) == 1;
    };
    long ok0 = log_count("flag lida com sucesso");
    achieve_check(db, botw, 300);
    achieve_commit();
    check("BOTW: bool (byte antes do hash, bit 0) cumprida", hasb("b_glider"));
    check("BOTW: bool com bit 0 = 0 bloqueada", !hasb("b_tower"));
    check("BOTW: s32 4 bytes antes do hash (42 >= 30)", hasb("b_shrine"));
    check("placeholder PREENCHER: bloqueada", !hasb("b_fill"));
    check("log: 3 'flag lida com sucesso'", log_count("flag lida com sucesso") - ok0 == 3);
    check("log: 'ainda nao cumprida' para a torre", log_count("b_tower: flag lida com sucesso: valor 2 (0x2) no hash 0x7BF47AB8 em 0x20080024 -> ainda nao cumprida") == 1);
    check("log: placeholder = parametros invalidos", log_count("b_fill: parametros invalidos") == 1);
    check("log: cadeia parou no offset 1 (main+0x308)",
          log_count("b_chain: cadeia parou no offset 1 (0x308): leitura de main+0x308 deu ponteiro nulo") == 1);
    check("log: leitura recusada mostra o rc",
          log_count("b_bad: cadeia parou no offset 1 (0x300000): leitura de 0x8300000 falhou (rc=0x6A0B)") == 1);
    long lines = log_count("ram: ");
    achieve_check(db, botw, 330);
    achieve_commit();
    check("mesmo resultado: nenhuma linha nova no log", log_count("ram: ") == lines);
    *fake_at(objs + 0x18 + 0x0B) = 0x03;
    achieve_check(db, botw, 360);
    achieve_commit();
    check("torre ativada na RAM: desbloqueia", hasb("b_tower"));
    check("log: só a linha nova da torre", log_count("ram: ") == lines + 1);

    // --- JSON real do BOTW (build CD57B23F, v1114112) (tools/botw_ram_teste) com a cadeia achada no main ---
    // [main+0x1E3B188] -> Manager; [+0xC00] mFlagBuffer -> TriggerParam;
    // [+0x10] mBoolFlags.buffer / [+0x20] mS32Flags.buffer; [+0] primeiro objeto.
    {
        FILE* in = fopen(PT_REPO "/tools/botw_ram_teste/01007EF00011E000.aventureiro.json", "rb");
        FILE* out = fopen("sdmc:/switch/ruby_achievements/achievements/01007EF00011E000.json", "wb");
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(in);
        fclose(out);
    }
    const u64 bMain = 0x80000000, bMainSize = 0x1E98000, bHeap = 0x90000000;
    g_fake_regions.push_back({bMain, std::vector<uint8_t>(bMainSize, 0)});
    g_fake_regions.push_back({bHeap, std::vector<uint8_t>(1 << 20, 0)});
    const u64 mgr = bHeap + 0x1000, tp = bHeap + 0x3000, bptr = bHeap + 0x4000, sptr = bHeap + 0x5000,
              bobj = bHeap + 0x10000, sobj = bHeap + 0x40000;
    put64(bMain + 0x1E3B188, mgr);
    put64(mgr + 0xC00, tp);
    put64(mgr + 0xBF8, bHeap + 0x8000);  // mFlagBuffer1 (não usado)
    put32(tp + 0x08, 16);
    put64(tp + 0x10, bptr);
    put32(tp + 0x18, 2);
    put64(tp + 0x20, sptr);
    const char* bools[] = {"IsGet_PlayerStole2", "MapTower_01", "MapTower_02", "MapTower_03", "MapTower_04",
                           "MapTower_05", "MapTower_06", "MapTower_07", "MapTower_08", "MapTower_09",
                           "MapTower_10", "MapTower_11", "MapTower_12", "MapTower_13", "MapTower_14", "MapTower_15"};
    for (int i = 0; i < 16; i++) {
        u64 o = bobj + 0x18 * (u64)i;
        put64(bptr + 8 * (u64)i, o);
        flag_bool(o, bools[i], 0x02);  // bit 0 = 0
    }
    put64(sptr, sobj);
    put32(sobj + 0x18, 0);
    put32(sobj + 0x1C, crc32_ieee("DungeonClearCounter_Dummy"));
    put64(sptr + 8, sobj + 0x28);
    put32(sobj + 0x28 + 0x18, 0);
    put32(sobj + 0x28 + 0x1C, crc32_ieee("DungeonClearCounter"));
    exec(db, "INSERT INTO games(id,title_id,display_name,source_type,created_at,updated_at) "
             "VALUES(3,'01007EF00011E000','BOTW','NATIVE',0,0);");
    g_fake_proc = {91, 0x01007EF00011E000ULL, bMain, bMainSize, {}};
    strcpy(g_fake_proc.build_id, "CD57B23FA4BBAD65803D9788C01821EE00000000000000000000000000000000");
    AchieveTarget real{3, 1, 0x01007EF00011E000ULL, "", true};
    auto hasr = [&](const char* aid) {
        return pt_db_scalar(db, ("SELECT COUNT(*) FROM user_achievements WHERE achievement_id='L:01007EF00011E000:" +
                                 std::string(aid) + "'").c_str(), 0, 0, 0) == 1;
    };
    achieve_check(db, real, 400);
    achieve_commit();
    check("BOTW RAM: nada cumprido ainda", !hasr("rt_paraglider") && !hasr("rt_tower_first") && !hasr("rt_shrine_1"));
    check("BOTW RAM: sem 'parametros invalidos'", log_count("rt_paraglider: parametros invalidos") == 0 &&
                                                     log_count("rt_shrine_1: parametros invalidos") == 0);
    check("BOTW RAM: asa-delta lida (ainda nao cumprida)",
          log_count("rt_paraglider: flag lida com sucesso: valor 2 (0x2) no hash 0xFE4D1501 em 0x9001000C -> ainda nao cumprida") == 1);
    *fake_at(bobj + 0x0B) = 0x03;               // asa-delta
    *fake_at(bobj + 0x18 * 7 + 0x0B) = 0x03;    // MapTower_07
    put32(sobj + 0x28 + 0x18, 1);               // 1 santuário
    achieve_check(db, real, 430);
    achieve_commit();
    check("BOTW RAM: asa-delta desbloqueada", hasr("rt_paraglider"));
    check("BOTW RAM: primeira torre (MapTower_07)", hasr("rt_tower_first"));
    check("BOTW RAM: primeiro santuário (s32 = 1)", hasr("rt_shrine_1"));
    check("BOTW RAM: log do santuário",
          log_count("rt_shrine_1: flag lida com sucesso: valor 1 (0x1) no hash 0xE605CE62 em 0x90040044 -> cumprida") == 1);
    // f32: mF32Flags em TriggerParam+0x28 (buffer +0x30), FlagF32 com valor 4 bytes antes do hash.
    {
        const u64 fptr = bHeap + 0x6000, fobj = bHeap + 0x60000;
        put32(tp + 0x28, 1);
        put64(tp + 0x30, fptr);
        put64(fptr, fobj);
        float stamina = 2000.0f;
        put(fobj + 0x18, &stamina, 4);
        put32(fobj + 0x1C, crc32_ieee("StaminaMax"));
        achieve_check(db, real, 460);
        achieve_commit();
        check("f32: 2 rodas de stamina (2000.0) desbloqueia", hasr("rt_stamina_2"));
        check("f32: 3 rodas (precisa 3000.0) bloqueada", !hasr("rt_stamina_3"));
    }
    check("pacote de RAM: nenhuma das 58 rejeitada", log_count("parametros invalidos") == 3);  // só bad (x2, parte 1) e b_fill
    check("pacote de RAM: só conquistas de RAM (e a Ruby)", pt_db_scalar(db, "SELECT COUNT(*) FROM achievements WHERE id LIKE 'L:01007EF00011E000:%' AND id NOT LIKE '%:rt_%' AND rarity <> 'ruby'", 0, 0, 0) == 0);
    check("icone: caminho no SD gravado na galeria",
          pt_db_scalar(db, "SELECT COUNT(*) FROM achievements WHERE id='L:0100000000BBB000:b_glider' AND "
                           "icon_local_path='sdmc:/switch/ruby_achievements/achievements/icons/0100000000BBB000/b_glider.png'", 0, 0, 0) == 1);
    // Trava de versão: o pacote tem build_id; outro executável não lê a RAM.
    for (int i = 1; i <= 5; i++) *fake_at(bobj + 0x18 * (u64)i + 0x0B) = 0x03;  // 5 torres
    g_fake_proc.pid = 92;
    strcpy(g_fake_proc.build_id, "2094F152836683A3632C6153FD84AB8000000000000000000000000000000000");
    achieve_check(db, real, 490);
    achieve_commit();
    check("build id diferente: 5 torres nao desbloqueiam", !hasr("rt_tower_5"));
    check("build id diferente: log uma vez",
          log_count("ram: build id do jogo 2094F152836683A3632C6153FD84AB80 difere do pacote (CD57B23FA4BBAD65803D9788C01821EE)") == 1);
    achieve_check(db, real, 520);
    achieve_commit();
    check("build id diferente: sem log repetido", log_count("difere do pacote") == 1);
    g_fake_proc.pid = 93;
    strcpy(g_fake_proc.build_id, "CD57B23FA4BBAD65803D9788C01821EE00000000000000000000000000000000");
    achieve_check(db, real, 550);
    achieve_commit();
    check("build id certo: 5 torres desbloqueiam", hasr("rt_tower_5"));
    // Pacote de teste atual: a variante "aventureiro" (54 rt_* + a Ruby).
    check("pacote de RAM: 54 na galeria", pt_db_scalar(db, "SELECT COUNT(*) FROM achievements WHERE id LIKE 'L:01007EF00011E000:rt_%'", 0, 0, 0) == 54);

    // --- ram_pointer_chain: cadeia genérica [[main+0x1000]+0x28]+off (convenção EdiZon/Cheat Engine) ---
    {
        const u64 g1 = kHeap + 0xA0000, g2 = kHeap + 0xB0000;
        put64(kMain + 0x1000, g1);
        put64(g1 + 0x28, g2);
        put32(g2 + 0x10, 150);                         // s32
        *fake_at(g2 + 0x20) = 200;                     // u8 (como s8 seria -56)
        *fake_at(g2 + 0x21) = 0xFD;                    // s8 = -3
        put32(g2 + 0x22, 0xBEEF);                      // u16 (os 2 bytes de cima ficam zerados)
        uint16_t s16 = (uint16_t)(int16_t)-1000;
        put(g2 + 0x26, &s16, 2);                       // s16
        float f = 2.5f;
        put(g2 + 0x28, &f, 4);                         // f32
        put64(g2 + 0x30, 0x100000001ULL);              // u64
        put32(g2 + 0x38, 0xB);                         // u32 0b1011
        put32(kMain + 0x2000, 7);                      // endereço fixo
        put64(kMain + 0x3000, 0);                      // ponteiro nulo
        auto pc = [](const char* id, const char* offs, const char* type, const char* op, const char* exp) {
            return std::string("{\"id\":\"") + id + "\",\"title\":\"" + id + "\",\"type\":\"ram_pointer_chain\","
                   "\"pointer_offsets\":[" + offs + "],\"data_type\":\"" + type + "\",\"operator\":\"" + op + "\"," +
                   exp + "}";
        };
        const char* chain = "\"0x1000\",\"0x28\",";
        auto ch = [&](const char* last) { return std::string(chain) + "\"" + last + "\""; };
        std::string defs = "{\"achievements\":[" +
            pc("g_s32_ge", ch("0x10").c_str(), "s32", ">=", "\"expected_value_int\":100") + "," +
            pc("g_s32_gt", ch("0x10").c_str(), "s32", ">", "\"expected_value_int\":150") + "," +
            pc("g_s32_lt", ch("0x10").c_str(), "s32", "<", "\"expected_value_int\":151") + "," +
            pc("g_s32_ne", ch("0x10").c_str(), "s32", "!=", "\"expected_value_int\":150") + "," +
            pc("g_u8", ch("0x20").c_str(), "u8", "==", "\"expected_value_int\":200") + "," +
            pc("g_s8", ch("0x21").c_str(), "s8", "<", "\"expected_value_int\":0") + "," +
            pc("g_u16", ch("0x22").c_str(), "u16", "==", "\"expected_value_hex\":\"0xBEEF\"") + "," +
            pc("g_s16", ch("0x26").c_str(), "s16", "<=", "\"expected_value_int\":-1000") + "," +
            pc("g_f32", ch("0x28").c_str(), "f32", ">=", "\"expected_value_float\":2.5") + "," +
            pc("g_f32_gt", ch("0x28").c_str(), "f32", ">", "\"expected_value_float\":2.5") + "," +
            pc("g_u64", ch("0x30").c_str(), "u64", "==", "\"expected_value_hex\":\"0x100000001\"") + "," +
            pc("g_bits", ch("0x38").c_str(), "u32", "bit_set", "\"expected_value_int\":10") + "," +
            pc("g_bits_no", ch("0x38").c_str(), "u32", "bit_set", "\"expected_value_int\":4") + "," +
            pc("g_static", "\"0x2000\"", "u32", "==", "\"expected_value_int\":7") + "," +
            pc("g_null", "\"0x3000\",\"0x8\"", "s32", ">=", "\"expected_value_int\":1") + "," +
            pc("g_badtype", ch("0x10").c_str(), "x32", ">=", "\"expected_value_int\":1") + "," +
            pc("g_fbit", ch("0x28").c_str(), "f32", "bit_set", "\"expected_value_float\":1") + "," +
            "{\"id\":\"g_both\",\"title\":\"B\",\"type\":\"count_of\",\"conditions\":[" +
            pc("x", ch("0x10").c_str(), "s32", ">=", "\"expected_value_int\":100") + "," +
            pc("y", ch("0x20").c_str(), "u8", ">", "\"expected_value_int\":250") + "]}" +
            "]}";
        FILE* g = fopen("sdmc:/switch/ruby_achievements/achievements/0100000000CCC000.json", "wb");
        fputs(defs.c_str(), g);
        fclose(g);
        exec(db, "INSERT INTO games(id,title_id,display_name,source_type,created_at,updated_at) "
                 "VALUES(4,'0100000000CCC000','Generico','NATIVE',0,0);");
        g_fake_proc = {95, 0x0100000000CCC000ULL, kMain, kMainSize, {}};
        AchieveTarget gen{4, 1, 0x0100000000CCC000ULL, "", true};
        auto hasg = [&](const char* aid) {
            return pt_db_scalar(db, ("SELECT COUNT(*) FROM user_achievements WHERE achievement_id='L:0100000000CCC000:" +
                                     std::string(aid) + "'").c_str(), 0, 0, 0) == 1;
        };
        // Log zerado: o pt_log trunca o arquivo ao passar de 32 KB, e as checagens abaixo
        // procuram linhas desta avaliação.
        unlink(PT_LOG_PATH);
        long l0 = log_count("ponteiro lido com sucesso");
        achieve_check(db, gen, 600);
        achieve_commit();
        check("ptr: s32 150 >= 100", hasg("g_s32_ge"));
        check("ptr: s32 150 > 150 nao", !hasg("g_s32_gt"));
        check("ptr: s32 150 < 151", hasg("g_s32_lt"));
        check("ptr: s32 150 != 150 nao", !hasg("g_s32_ne"));
        check("ptr: u8 200 (sem sinal)", hasg("g_u8"));
        check("ptr: s8 0xFD = -3 < 0", hasg("g_s8"));
        check("ptr: u16 le so 2 bytes (0xBEEF)", hasg("g_u16"));
        check("ptr: s16 -1000 <= -1000", hasg("g_s16"));
        check("ptr: f32 2.5 >= 2.5", hasg("g_f32"));
        check("ptr: f32 2.5 > 2.5 nao", !hasg("g_f32_gt"));
        check("ptr: u64 0x100000001", hasg("g_u64"));
        check("ptr: bit_set 0b1011 & 0b1010", hasg("g_bits"));
        check("ptr: bit_set 0b1011 & 0b0100 nao", !hasg("g_bits_no"));
        check("ptr: offset unico = endereco fixo main+0x2000", hasg("g_static"));
        check("ptr: count_of com duas cadeias (1 de 2 nao basta)", !hasg("g_both"));
        check("log: u8 lido com endereco",
              log_count("g_u8: ponteiro lido com sucesso: u8 200 (0xC8) em 0x200B0020 -> cumprida") == 1);
        check("log: f32 lido", log_count("g_f32: ponteiro lido com sucesso: f32 2.5 em 0x200B0028 -> cumprida") == 1);
        check("log: endereco fixo", log_count("g_static: ponteiro lido com sucesso: u32 7 (0x7) em main+0x2000") == 1);
        check("log: ponteiro nulo", log_count("g_null: cadeia parou no offset 1 (0x3000): leitura de main+0x3000 deu ponteiro nulo") == 1);
        check("log: tipo invalido", log_count("g_badtype: parametros invalidos") == 1);
        check("log: bit_set em f32 invalido", log_count("g_fbit: parametros invalidos") == 1);
        check("log: as duas cadeias do count_of", log_count("g_both: ponteiro lido com sucesso") == 2);
        long l1 = log_count("ponteiro lido com sucesso");
        achieve_check(db, gen, 630);
        achieve_commit();
        check("ptr: mesmo resultado, sem log novo", log_count("ponteiro lido com sucesso") == l1 && l1 > l0);
        *fake_at(g2 + 0x20) = 255;  // u8 passa de 250: o count_of completa
        achieve_check(db, gen, 660);
        achieve_commit();
        check("ptr: valor mudou na RAM, count_of completa", hasg("g_both"));
    }

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
