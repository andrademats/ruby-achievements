#include "prefs.hpp"

#include <stdio.h>

#include "i18n.hpp"
#include "pt_config.h"
#include "pt_wav.hpp"

namespace prefs {

namespace {
int g_theme = THEME_LIGHT;
int g_background = BG_CLASSIC;  // padrão (pt_config)

// Relê o config.ini, muda um campo e grava (os outros campos ficam como estão).
template <typename Fn>
bool save(Fn change) {
    PtConfig c;
    pt_config_load(&c);
    change(c);
    return pt_config_save_app(&c) == 0;
}
}  // namespace

void load() {
    PtConfig c;
    pt_config_load(&c);
    g_theme = c.app_theme;
    g_background = c.app_background;
    i18n::set_language(c.app_language);
    i18n::set_date_format(c.app_date_format);
}

int theme() { return g_theme; }
int background() { return g_background; }
int language() { return i18n::language(); }
int date_format() { return i18n::date_format(); }

bool set_theme(int v) {
    g_theme = v == THEME_DARK ? THEME_DARK : THEME_LIGHT;
    return save([](PtConfig& c) { c.app_theme = g_theme; });
}

bool set_background(int v) {
    g_background = v == BG_CLASSIC ? BG_CLASSIC : BG_SOLID;
    return save([](PtConfig& c) { c.app_background = g_background; });
}

bool set_language(int v) {
    i18n::set_language(v);
    return save([](PtConfig& c) { c.app_language = i18n::language(); });
}

bool set_date_format(int v) {
    i18n::set_date_format(v);
    return save([](PtConfig& c) { c.app_date_format = i18n::date_format(); });
}

std::string popup_sfx() {
    PtConfig c;
    pt_config_load(&c);
    return pt::sfx_name_ok(c.popup_sfx) ? c.popup_sfx : "";
}

bool set_popup_sfx(const std::string& name) {
    if (!name.empty() && (!pt::sfx_name_ok(name.c_str()) || name.size() >= sizeof(PtConfig::popup_sfx))) return false;
    return save([&](PtConfig& c) { snprintf(c.popup_sfx, sizeof(c.popup_sfx), "%s", name.c_str()); });
}

}  // namespace prefs
