// Tracker completo (sessões, transações, conquistas e foco) com relógio e FS simulados.
#include <switch.h>
#include <utime.h>
#include "detect.h"
#include "tracker.h"
#include "pt_db.h"
#include "pt_log.h"
#include "pt_util.h"
#include "pt_paths.h"
#include "rubydb_writer.hpp"
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;
u64 g_tid = 0;
bool g_home_front = false;

// --- detect simulado ---
u64 detect_running_title(void) { return g_tid; }
bool detect_is_forwarder(u64 tid) { return (tid >> 56) == 0x05; }
bool detect_is_ghost(u64) { return false; }
bool detect_home_in_front(u64) { return g_home_front; }
u64 detect_focused_title(void) { return 0; }
void detect_init(void) {}
int g_build_reads = 0;
bool detect_build_id(char out[65]) {
    g_build_reads++;
    snprintf(out, 65, "2E819F30C4110A5B%048d", 0);
    return true;
}
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
static void put32(std::vector<char>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i));
}
uint32_t crc32_of(const char* s) {
    uint32_t c = 0xFFFFFFFFu;
    for (; *s; s++) {
        c ^= (unsigned char)*s;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    mkdir("sdmc:", 0777);
    mkdir("sdmc:/switch", 0777);
    pt_ensure_dirs();
    mkdir("sdmc:/0", 0777);
    for (const char* d : {"sdmc:/tico", "sdmc:/tico/saves", "sdmc:/tico/saves/gba"}) mkdir(d, 0777);
    FILE* gs = fopen("sdmc:/tico/saves/gba/Jogo.sav", "wb"); fputs("x", gs); fclose(gs);
    // Conquista por save do console no "BOTW".
    FILE* d = fopen(PT_ACH_DIR "/01007EF00011E000.json", "wb");
    // "points" de pacote antigo: ignorado (pontos removidos em 2026-10-20).
    fputs("{\"achievements\":[{\"id\":\"glider\",\"title\":\"Asa\",\"points\":10,\"type\":\"save_flag_hash_search\","
          "\"save_file\":\"game_data.sav\",\"flag_name\":\"IsGet_PlayerStole2\",\"hash_type\":\"crc32\","
          "\"expected_value_int\":1}]}",
          d);
    fclose(d);
    rubydb::from_json_file(PT_ACH_DIR "/01007EF00011E000.json");
    std::vector<char> save(4096, 0);
    put32(save, 0x0C, crc32_of("IsGet_PlayerStole2"));
    put32(save, 0x10, 1);
    FILE* sf = fopen("sdmc:/0/game_data.sav", "wb");
    fwrite(save.data(), 1, save.size(), sf);
    fclose(sf);

    tracker_init(g_fake_now);
    run(3);
    long scans0 = log_count("varredura de saves");

    // Jogo do sistema: nada no banco nem varredura de saves por 60 s.
    g_save_rc = 0xE02;  // save preso pelo jogo
    g_tid = 0x01007EF00011E000ULL;
    run(55);
    check("abertura (55 s): nenhuma sessão gravada ainda", q("SELECT COUNT(*) FROM sessions") == 0);
    check("abertura (55 s): nenhuma montagem de save", g_save_opens == 0);
    check("abertura (55 s): nenhuma varredura de saves de ROM", log_count("varredura de saves") == scans0);
    run(10);
    check("após 60 s: sessão gravada", q("SELECT COUNT(*) FROM sessions") == 1);
    check("após 60 s: build id do jogo em title_builds (uma leitura)",
          q("SELECT COUNT(*) FROM title_builds WHERE title_id='01007EF00011E000' AND "
            "build_id LIKE '2E819F30C4110A5B%' AND length(build_id)=64") == 1 && g_build_reads == 1);

    // Save recusado com o jogo aberto: tenta uma vez e espera.
    run(120);
    check("save em uso: uma única tentativa em 2 min", g_save_opens == 1);
    check("save em uso: log uma vez", log_count("em uso pelo jogo") == 1);
    check("jogo nativo: nenhuma varredura de saves de ROM (nem a de referência)",
          log_count("varredura de saves") == scans0);

    // Home (jogo suspenso continua com o save preso): nenhuma nova tentativa.
    g_home_front = true;
    run(30);
    g_home_front = false;
    run(60);
    check("Home e volta ao jogo: sem novas montagens", g_save_opens == 1);
    check("save preso: nada desbloqueado com o jogo aberto", q("SELECT COUNT(*) FROM user_achievements") == 0);

    // Fecha o jogo (save liberado): avalia o save e enfileira o pop-up.
    g_save_rc = 0;
    g_tid = 0;
    run(3);
    check("ao fechar: conquista do save desbloqueada", q("SELECT COUNT(*) FROM user_achievements") == 1);
    check("pop-up exportado", q("SELECT COUNT(*) FROM achievement_queue") == 1);
    {
        // Formato para o overlay: "id|raridade|ícone|título" (sem pontos, descrição nem som).
        char buf[512] = {0};
        pt_read_file(PT_POPUP_PATH, buf, sizeof(buf) - 1);
        std::string want = std::to_string(q("SELECT id FROM achievement_queue")) + "|bronze||Asa\n";
        check("pop-up: id|raridade|ícone|título (bronze padrão)", want == buf);
    }
    check("ao fechar: nenhuma varredura de saves de ROM", log_count("varredura de saves") == scans0);
    int64_t dur = q("SELECT duration FROM sessions");
    check("sessão encerrada com 275 s (abertura incluída)", dur == 275);
    check("sessão com fim gravado", q("SELECT end_time IS NOT NULL FROM sessions") == 1);

    // Homebrew: janela curta (10 s).
    g_tid = 0x0500000000001234ULL;
    run(15);
    check("forwarder/homebrew: gravado após ~10 s", q("SELECT COUNT(*) FROM sessions") == 2);
    check("forwarder/homebrew: varredura de saves de ROM continua", log_count("varredura de saves") == scans0 + 1);
    check("forwarder/homebrew: sem leitura de build id", g_build_reads == 1 && q("SELECT COUNT(*) FROM title_builds") == 1);
    g_tid = 0;
    run(3);
    check("journal PERSIST", q("PRAGMA journal_mode") != -1);

    // Log do sysmodule: lote numa escrita só (avaliação) e rotação para .old.
    pt_log_batch_begin();
    pt_log("lote %d", 1);
    pt_log("lote %d", 2);
    check("log em lote: nada no SD antes do fim", log_count("lote ") == 0);
    pt_log_batch_end();
    check("log em lote: as linhas gravadas no fim", log_count("lote 1") == 1 && log_count("lote 2") == 1);
    std::string filler(600, 'x');
    for (int i = 0; i < 60; i++) pt_log("enchendo %s", filler.c_str());
    pt_log("depois da rotacao");
    struct stat old_st;
    check("log acima de 32 KB vira .old (o histórico não some)",
          stat(PT_LOG_PATH ".old", &old_st) == 0 && old_st.st_size > 32 * 1024 && log_count("lote ") == 0 &&
              log_count("depois da rotacao") == 1);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
