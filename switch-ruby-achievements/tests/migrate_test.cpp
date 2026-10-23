// Migração da pasta do nome antigo (Switch Playtime -> Ruby Achievements): pt_migrate_legacy.
#include "pt_paths.h"
#include "pt_util.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
s64 g_fake_now = 1000;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static void put(const std::string& p, const std::string& text) {
    FILE* f = fopen(p.c_str(), "wb");
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}
static std::string get(const std::string& p) {
    char buf[8192];
    long n = pt_read_file(p.c_str(), buf, sizeof(buf));
    return n < 0 ? std::string("<sem arquivo>") : std::string(buf, (size_t)n);
}
static bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}
static void reset() {
    system("rm -rf sdmc:");
    mkdir("sdmc:", 0777);
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/.overlays", 0777);
}

int main() {
    const std::string L = PT_LEGACY_DIR, N = PT_DIR;

    // 1) SD com o dist novo copiado por cima de uma instalação antiga.
    reset();
    mkdir(L.c_str(), 0777);
    for (const char* d : {"/sfx", "/achievements", "/covers", "/db.lock"}) mkdir((L + d).c_str(), 0777);
    put(L + "/playtime.db", "BANCO");
    put(L + "/playtime.db-journal", "JOURNAL");
    put(L + "/playtime.nro", "APP VELHO");
    put(L + "/config.ini", "[app]\napp_language=1\n");
    put(L + "/sfx/conquista.wav", "WAV PADRAO");
    put(L + "/sfx/meu.wav", "WAV DO USUARIO");
    put(L + "/achievements/01007EF00011E000.json", "PACOTE EDITADO");
    put(L + "/covers/capa.jpg", "CAPA");
    put(L + "/db.lock/owner", "1");
    put(PT_LEGACY_OVERLAY_PATH, "OVL VELHO");
    mkdir(N.c_str(), 0777);
    mkdir((N + "/sfx").c_str(), 0777);
    mkdir((N + "/achievements").c_str(), 0777);
    put(N + "/ruby_achievements.nro", "APP NOVO");
    put(N + "/sfx/conquista.wav", "WAV PADRAO");
    put(N + "/achievements/01007EF00011E000.json", "PACOTE DO DIST");
    put(PT_OVERLAY_PATH, "OVL NOVO");

    int left = pt_migrate_legacy();
    check("banco renomeado para ruby_achievements.db", get(PT_DB_PATH) == "BANCO");
    check("journal renomeado junto", get(PT_DB_PATH "-journal") == "JOURNAL");
    check("sem playtime.db na pasta nova", !exists(N + "/playtime.db"));
    check("config.ini movido", get(PT_CONFIG_PATH) == "[app]\napp_language=1\n");
    check("som do usuário movido para sfx/", get(N + "/sfx/meu.wav") == "WAV DO USUARIO");
    check("capa movida (pasta inteira)", get(N + "/covers/capa.jpg") == "CAPA");
    check("lock movido", exists(N + "/db.lock/owner"));
    check("app velho apagado", !exists(L + "/playtime.nro"));
    check("app novo intacto", get(PT_APP_NRO) == "APP NOVO");
    check("conflito: o do destino não é sobrescrito", get(N + "/achievements/01007EF00011E000.json") == "PACOTE DO DIST");
    check("conflito: o antigo fica na pasta antiga", get(L + "/achievements/01007EF00011E000.json") == "PACOTE EDITADO");
    check("cópia idêntica apagada da pasta antiga", !exists(L + "/sfx/conquista.wav"));
    check("subpasta antiga vazia removida", !exists(L + "/sfx") && !exists(L + "/covers"));
    check("um item ficou para trás", left == 1);
    check("pasta antiga fica (tem o conflito)", exists(L));
    check("overlay velho apagado (o novo existe)", !exists(PT_LEGACY_OVERLAY_PATH) && exists(PT_OVERLAY_PATH));
    check("log da migração gravado", get(PT_MIGRATION_LOG_PATH).find("1 ficaram") != std::string::npos);

    // 2) Segunda execução (próximo boot): nada muda.
    left = pt_migrate_legacy();
    check("2ª execução: mesmo resultado", left == 1 && get(PT_DB_PATH) == "BANCO" &&
                                              get(L + "/achievements/01007EF00011E000.json") == "PACOTE EDITADO");

    // 3) Instalação antiga sem a pasta nova (só o sysmodule novo no SD): tudo vai.
    reset();
    mkdir(L.c_str(), 0777);
    mkdir((L + "/cache").c_str(), 0777);
    mkdir((L + "/cache/01007EF00011E000").c_str(), 0777);
    put(L + "/playtime.db", "BANCO");
    put(L + "/cache/01007EF00011E000/game_data.sav", "SAVE");
    put(PT_LEGACY_OVERLAY_PATH, "OVL VELHO");
    left = pt_migrate_legacy();
    check("sem pasta nova: tudo movido e a antiga removida", left == 0 && !exists(L));
    check("banco e cache no lugar novo", get(PT_DB_PATH) == "BANCO" &&
                                             get(N + "/cache/01007EF00011E000/game_data.sav") == "SAVE");
    check("overlay velho fica sem o novo instalado", exists(PT_LEGACY_OVERLAY_PATH));

    // 4) Os dois bancos existem: nada é sobrescrito.
    reset();
    mkdir(L.c_str(), 0777);
    mkdir(N.c_str(), 0777);
    put(L + "/playtime.db", "ANTIGO");
    put(PT_DB_PATH, "NOVO");
    pt_migrate_legacy();
    check("dois bancos: o novo fica", get(PT_DB_PATH) == "NOVO");
    check("dois bancos: o antigo fica guardado", get(N + "/playtime.db") == "ANTIGO");

    // 5) Sem pasta antiga: não faz nada, e pt_ensure_dirs cria a nova.
    reset();
    check("sem pasta antiga: 0", pt_migrate_legacy() == 0 && !exists(N));
    pt_ensure_dirs();
    check("pt_ensure_dirs cria a pasta nova", exists(N) && exists(PT_ACH_DIR) && exists(PT_COVERS_DIR));
    check("e não cria a antiga", !exists(L));

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails ? 1 : 0;
}
