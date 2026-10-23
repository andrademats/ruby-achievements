#pragma once
#include <string>
// Opções da aba Interface (config.ini, seção [app]): tema, fundo, idioma,
// formato de data e som do pop-up de conquista. load() roda uma vez no início do app, antes da 1ª view; os
// setters gravam no config.ini. Tema, fundo e idioma valem para as telas
// criadas depois: ui::reload_interface() recria as que estão abertas.
namespace prefs {

enum Theme { THEME_LIGHT = 0, THEME_DARK = 1 };
enum Background { BG_SOLID = 0, BG_CLASSIC = 1 };

void load();
int theme();
int background();
int language();     // i18n::Lang
int date_format();  // i18n::DateFormat
// Som do pop-up (arquivo em PT_SFX_DIR; "" = o padrão). Relido do config.ini.
std::string popup_sfx();

// Gravam no config.ini; retornam false se a gravação falhou (o valor em memória muda igual).
bool set_theme(int v);
bool set_background(int v);
bool set_language(int v);
bool set_date_format(int v);
bool set_popup_sfx(const std::string& name);  // "" = volta ao padrão

}  // namespace prefs
