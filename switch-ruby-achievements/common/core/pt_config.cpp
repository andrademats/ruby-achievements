#include "pt_config.h"
#include "pt_paths.h"
#include "pt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pt_config_defaults(PtConfig* c) {
    c->profile = 0;
    c->import_asked = 0;
    c->sgdb_key[0] = '\0';
    c->ra_user[0] = '\0';
    c->ra_key[0] = '\0';
    c->app_profile = 0;
    c->app_sort = 0;
    c->app_theme = 0;
    c->app_background = 1;  // Clássico desde a 1ª abertura
    c->app_language = 0;
    c->app_date_format = 0;
    c->popup_sfx[0] = '\0';
    c->popup_flood = 0;
}

// Chama fn(key, val) para cada linha "chave=valor" do texto (seções e comentários pulados).
template <typename Fn>
static void for_each_entry(char* text, Fn fn) {
    char* save = NULL;
    for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char* eq = strchr(line, '=');
        if (!eq || line[0] == '[' || line[0] == ';' || line[0] == '#') continue;
        *eq = '\0';
        fn(pt_trim(line), pt_trim(eq + 1));
    }
}

// As três chaves de API, em config.ini antigo ou secrets.ini. Retorna se `key` é uma delas.
static bool read_secret(PtConfig* c, const char* key, const char* val) {
    if (!strcmp(key, "steamgriddb_key")) snprintf(c->sgdb_key, sizeof(c->sgdb_key), "%s", val);
    else if (!strcmp(key, "ra_user")) snprintf(c->ra_user, sizeof(c->ra_user), "%s", val);
    else if (!strcmp(key, "ra_key")) snprintf(c->ra_key, sizeof(c->ra_key), "%s", val);
    else return false;
    return true;
}

static bool has_secrets(const PtConfig* c) { return c->sgdb_key[0] || c->ra_user[0] || c->ra_key[0]; }

static bool secrets_file_exists(void) {
    char probe[2];
    return pt_read_file(PT_SECRETS_PATH, probe, sizeof(probe)) >= 0;
}

void pt_config_load(PtConfig* c) {
    pt_config_defaults(c);
    char buf[1024];
    if (pt_read_file(PT_CONFIG_PATH, buf, sizeof(buf)) >= 0) {
        for_each_entry(buf, [c](const char* key, const char* val) {
            long long v = strtoll(val, NULL, 10);
            if (read_secret(c, key, val)) return;  // config.ini antigo (antes do secrets.ini)
            if (!strcmp(key, "profile")) c->profile = v;
            else if (!strcmp(key, "import_asked")) c->import_asked = v != 0;
            else if (!strcmp(key, "app_profile")) c->app_profile = v;
            else if (!strcmp(key, "app_sort")) c->app_sort = (int)v;
            else if (!strcmp(key, "app_theme")) c->app_theme = (int)v;
            else if (!strcmp(key, "app_background")) c->app_background = (int)v;
            else if (!strcmp(key, "app_language")) c->app_language = (int)v;
            else if (!strcmp(key, "app_date_format")) c->app_date_format = (int)v;
            else if (!strcmp(key, "popup_sfx")) snprintf(c->popup_sfx, sizeof(c->popup_sfx), "%s", val);
            else if (!strcmp(key, "popup_flood")) c->popup_flood = v != 0;
        });
    }
    char sec[512];
    if (pt_read_file(PT_SECRETS_PATH, sec, sizeof(sec)) >= 0) {
        // O secrets.ini manda: o que estiver nele (ou faltar) substitui o config.ini antigo.
        c->sgdb_key[0] = c->ra_user[0] = c->ra_key[0] = '\0';
        for_each_entry(sec, [c](const char* key, const char* val) { read_secret(c, key, val); });
    }
    if (c->profile < 0) c->profile = 0;
    if (c->app_profile < 0) c->app_profile = 0;
    if (c->app_sort < 0 || c->app_sort > 3) c->app_sort = 0;
    if (c->app_theme < 0 || c->app_theme > 1) c->app_theme = 0;
    if (c->app_background < 0 || c->app_background > 1) c->app_background = 1;
    if (c->app_language < 0 || c->app_language > 1) c->app_language = 0;
    if (c->app_date_format < 0 || c->app_date_format > 2) c->app_date_format = 0;
}

static int write_secrets(const PtConfig* c) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[secrets]\nsteamgriddb_key=%s\nra_user=%s\nra_key=%s\n", c->sgdb_key,
                     c->ra_user, c->ra_key);
    if (n < 0 || n >= (int)sizeof(buf)) return -1;
    return pt_write_file_atomic(PT_SECRETS_PATH, buf, (size_t)n);
}

int pt_config_save_app(const PtConfig* c) {
    // Migração: as chaves lidas do config.ini antigo vão para o secrets.ini antes de o
    // config.ini ser regravado sem elas.
    if (has_secrets(c) && !secrets_file_exists() && write_secrets(c) != 0) return -1;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
                     "[hud]\nprofile=%lld\n[app]\nimport_asked=%d\n"
                     "app_profile=%lld\napp_sort=%d\napp_theme=%d\napp_background=%d\napp_language=%d\n"
                     "app_date_format=%d\npopup_sfx=%s\n[overlay]\npopup_flood=%d\n",
                     (long long)c->profile, c->import_asked ? 1 : 0, (long long)c->app_profile, c->app_sort,
                     c->app_theme, c->app_background, c->app_language, c->app_date_format, c->popup_sfx,
                     c->popup_flood ? 1 : 0);
    if (n < 0 || n >= (int)sizeof(buf)) return -1;
    return pt_write_file_atomic(PT_CONFIG_PATH, buf, (size_t)n);
}

int pt_secrets_save(const PtConfig* c) {
    if (write_secrets(c) != 0) return -1;
    return pt_config_save_app(c);
}

int pt_config_migrate_secrets(void) {
    char buf[1024];
    if (pt_read_file(PT_CONFIG_PATH, buf, sizeof(buf)) < 0) return 0;
    bool legacy = false;
    PtConfig probe;
    pt_config_defaults(&probe);
    for_each_entry(buf, [&](const char* key, const char* val) { legacy = read_secret(&probe, key, val) || legacy; });
    if (!legacy) return 0;
    PtConfig c;
    pt_config_load(&c);
    return pt_config_save_app(&c) == 0 ? 1 : -1;
}
