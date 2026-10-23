// Aba Interface: idioma (tr/trf), formato de data e gravação no config.ini (prefs).
#include "i18n.hpp"
#include "prefs.hpp"
#include "pt_config.h"
#include "pt_paths.h"
#include "pt_util.h"
#include <stdio.h>
#include <string.h>
#include <string>
s64 g_fake_now = 1000;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

int main() {
    // --- Idioma ---
    i18n::set_language(i18n::LANG_EN);
    check("inglês: Voltar -> Back", std::string(tr("Voltar")) == "Back");
    check("inglês: Chaves -> Keys", std::string(tr("Chaves")) == "Keys");
    check("inglês: chave sem tradução volta igual", std::string(tr("xyz sem tradução")) == "xyz sem tradução");
    check("inglês: trf traduz e preenche", trf("%d/%d conquistas", 3, 10) == "3/10 achievements");
    check("inglês: trf com texto", trf("Total de %s: %s", "Ana", "2h 03min") == "Total for Ana: 2h 03min");
    check("inglês: texto longo com aspas e \\n",
          trf("Reativar o tracking de tempo de %s?\n\nO jogo volta para a tela inicial e as próximas sessões "
              "passam a contar.",
              "Zelda")
              .rfind("Turn time tracking back on for Zelda?\n\n", 0) == 0);
    i18n::set_language(i18n::LANG_PT);
    check("português: texto original", std::string(tr("Voltar")) == "Voltar");
    check("português: trf", trf("%d/%d conquistas", 3, 10) == "3/10 conquistas");
    i18n::set_language(7);
    check("idioma inválido = inglês", i18n::language() == i18n::LANG_EN);

    // --- Datas ---
    check("MM/DD/YYYY", i18n::format_datetime(2026, 10, 3, 9, 5, i18n::DATE_MDY) == "10/03/2026 09:05");
    check("DD/MM/YYYY", i18n::format_datetime(2026, 10, 3, 9, 5, i18n::DATE_DMY) == "03/10/2026 09:05");
    check("YYYY/MM/DD", i18n::format_datetime(2026, 10, 3, 9, 5, i18n::DATE_YMD) == "2026/10/03 09:05");
    check("só a data", i18n::format_day(2026, 1, 31, i18n::DATE_DMY) == "31/01/2026");
    i18n::set_date_format(9);
    check("formato inválido = MM/DD/YYYY", i18n::date_format() == i18n::DATE_MDY);

    // --- config.ini ---
    const char* old_ini = "[hud]\nprofile=2\n[app]\nimport_asked=1\nsteamgriddb_key=K\nra_user=u\nra_key=R\n"
                          "app_profile=2\napp_sort=1\n";
    pt_write_file_atomic(PT_CONFIG_PATH, old_ini, strlen(old_ini));
    prefs::load();
    check("config antigo: padrões (claro, clássico, inglês, MM/DD)",
          prefs::theme() == prefs::THEME_LIGHT && prefs::background() == prefs::BG_CLASSIC &&
              prefs::language() == i18n::LANG_EN && prefs::date_format() == i18n::DATE_MDY);
    check("grava tema escuro", prefs::set_theme(prefs::THEME_DARK));
    check("grava fundo sólido", prefs::set_background(prefs::BG_SOLID));
    check("grava português", prefs::set_language(i18n::LANG_PT));
    check("grava DD/MM/YYYY", prefs::set_date_format(i18n::DATE_DMY));
    PtConfig c;
    pt_config_load(&c);
    check("config.ini com as 4 opções", c.app_theme == 1 && c.app_background == 0 && c.app_language == 1 &&
                                             c.app_date_format == 1);
    check("outras chaves mantidas", c.profile == 2 && c.import_asked == 1 && !strcmp(c.sgdb_key, "K") &&
                                        !strcmp(c.ra_user, "u") && !strcmp(c.ra_key, "R") && c.app_profile == 2 &&
                                        c.app_sort == 1);
    i18n::set_language(i18n::LANG_EN);
    i18n::set_date_format(i18n::DATE_MDY);
    prefs::load();
    check("relido do config.ini", prefs::theme() == prefs::THEME_DARK && prefs::background() == prefs::BG_SOLID &&
                                      prefs::language() == i18n::LANG_PT && prefs::date_format() == i18n::DATE_DMY);
    const char* bad = "[app]\napp_theme=5\napp_background=-1\napp_language=9\napp_date_format=3\n";
    pt_write_file_atomic(PT_CONFIG_PATH, bad, strlen(bad));
    prefs::load();
    check("valores inválidos voltam ao padrão", prefs::theme() == 0 && prefs::background() == prefs::BG_CLASSIC &&
                                                    prefs::language() == 0 && prefs::date_format() == 0);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
