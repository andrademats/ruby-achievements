// config.ini e secrets.ini: só o app grava as chaves; chaves antigas do HUD são ignoradas e
// somem; chaves de API saem do config.ini antigo para o secrets.ini sem se perder.
#include "pt_config.h"
#include "pt_paths.h"
#include "pt_util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
s64 g_fake_now = 1000;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static std::string slurp(const char* path) {
    char buf[1024];
    return pt_read_file(path, buf, sizeof(buf)) < 0 ? std::string("<ausente>") : std::string(buf);
}
static bool no_secrets_in_config() {
    std::string s = slurp(PT_CONFIG_PATH);
    return s.find("steamgriddb_key") == std::string::npos && s.find("ra_user") == std::string::npos &&
           s.find("ra_key") == std::string::npos;
}
// config.ini de uma versão com HUD e com as chaves de API dentro dele (até 2026-10-22).
static const char* kOldIni = "[hud]\nenabled=1\nx=100\ny=200\nsize=44\ncolor=3\nprofile=2\n[app]\nimport_asked=1\n"
                             "steamgriddb_key=SGDBKEY\nra_user=link\nra_key=ABC\n";

int main() {
    pt_write_file_atomic(PT_CONFIG_PATH, kOldIni, strlen(kOldIni));
    PtConfig c;
    pt_config_load(&c);
    check("perfil do total lido do [hud]", c.profile == 2);
    check("config.ini antigo: chaves lidas", c.import_asked == 1 && !strcmp(c.sgdb_key, "SGDBKEY") &&
                                                 !strcmp(c.ra_user, "link") && !strcmp(c.ra_key, "ABC"));
    check("app_profile ausente = todos", c.app_profile == 0);

    // Qualquer gravação do config.ini (app ou overlay) migra as chaves antes de tirá-las.
    c.app_profile = 2;
    check("gravou", pt_config_save_app(&c) == 0);
    std::string ini = slurp(PT_CONFIG_PATH);
    check("sem chaves do HUD", ini.find("enabled=") == std::string::npos && ini.find("color=") == std::string::npos);
    check("config.ini sem chaves de API", no_secrets_in_config());
    check("secrets.ini criado com as chaves", slurp(PT_SECRETS_PATH).find("ra_key=ABC") != std::string::npos);
    PtConfig d;
    pt_config_load(&d);
    check("perfil do total mantido", d.profile == 2);
    check("perfil do app gravado", d.app_profile == 2);
    check("chaves mantidas (vindas do secrets.ini)", !strcmp(d.sgdb_key, "SGDBKEY") && !strcmp(d.ra_key, "ABC"));

    // Overlay lê, muda só a opção dele e regrava: as chaves continuam.
    PtConfig o;
    pt_config_load(&o);
    o.popup_flood = 1;
    pt_config_save_app(&o);
    pt_config_load(&d);
    check("overlay regrava: chaves e opção", d.popup_flood == 1 && !strcmp(d.ra_user, "link"));

    // Remover o login do RA (app: pt_secrets_save).
    d.ra_user[0] = d.ra_key[0] = '\0';
    check("pt_secrets_save gravou", pt_secrets_save(&d) == 0);
    pt_config_load(&c);
    check("login removido, SteamGridDB mantida", !c.ra_user[0] && !c.ra_key[0] && !strcmp(c.sgdb_key, "SGDBKEY"));
    check("opções mantidas", c.app_profile == 2 && c.popup_flood == 1);

    // Cópia velha do config.ini com chaves, mas o secrets.ini já existe: ele manda.
    pt_write_file_atomic(PT_CONFIG_PATH, kOldIni, strlen(kOldIni));
    pt_config_load(&c);
    check("secrets.ini manda sobre config.ini antigo", !c.ra_user[0] && !c.ra_key[0]);

    // Migração na abertura do app: config.ini antigo e nenhum secrets.ini.
    unlink(PT_SECRETS_PATH);
    pt_write_file_atomic(PT_CONFIG_PATH, kOldIni, strlen(kOldIni));
    check("migração: migrou", pt_config_migrate_secrets() == 1);
    check("migração: config.ini limpo", no_secrets_in_config());
    pt_config_load(&c);
    check("migração: chaves e perfil preservados",
          !strcmp(c.sgdb_key, "SGDBKEY") && !strcmp(c.ra_user, "link") && !strcmp(c.ra_key, "ABC") && c.profile == 2);
    check("migração: 2ª abertura não faz nada", pt_config_migrate_secrets() == 0);

    // Sem nenhum arquivo: nada a migrar, padrões.
    unlink(PT_SECRETS_PATH);
    unlink(PT_CONFIG_PATH);
    check("sem arquivos: nada a migrar", pt_config_migrate_secrets() == 0);
    pt_config_load(&c);
    check("sem arquivos: chaves vazias", !c.sgdb_key[0] && !c.ra_user[0] && !c.ra_key[0]);
    // App sem chaves grava opções: não cria secrets.ini à toa.
    pt_config_save_app(&c);
    check("sem chaves: secrets.ini não criado", slurp(PT_SECRETS_PATH) == "<ausente>");

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
