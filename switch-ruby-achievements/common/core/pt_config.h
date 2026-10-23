#pragma once
#include <stdint.h>

// Configuração persistida em PT_CONFIG_PATH (formato INI). O app grava; o overlay grava
// só a sua opção ([overlay]), sempre relendo o arquivo antes (pt_config_save_app
// regrava tudo a partir de `c`).
//   [hud] profile: perfil cujo total o sysmodule publica no status.txt (o
//         tempo mostrado no menu do overlay); 0 = todos os perfis. O nome da
//         seção ficou da época do HUD flutuante (removido em 2026-10-20).
//   [app] opções do app.
//   [overlay] opções do menu do overlay.
// Chaves antigas do HUD (enabled, x, y, size, color) são ignoradas na leitura
// e somem na próxima gravação.
//
// Chaves de API ficam em PT_SECRETS_PATH ([secrets]), separadas do config.ini, para o
// usuário poder mandar config e logs sem expor nada. Até 2026-10-22 elas ficavam no
// config.ini ([app] steamgriddb_key, ra_user, ra_key):
//   - leitura: se o secrets.ini existe, ele manda (chave ausente = vazia); senão valem as do
//     config.ini antigo;
//   - pt_config_save_app nunca grava chaves no config.ini; se o secrets.ini ainda não existe
//     e há chaves, ele é criado antes (assim o overlay, que também regrava o config.ini,
//     não as perde).
typedef struct {
    int64_t profile;     // [hud] users.id do perfil do total publicado; 0 = todos
    int import_asked;    // [app] 1 = o app já ofereceu importar o NX Activity Log
    char sgdb_key[64];   // secrets.ini: chave da API do SteamGridDB ("" = sem chave)
    char ra_user[64];    // secrets.ini: usuário do RetroAchievements
    char ra_key[64];     // secrets.ini: chave da Web API do RetroAchievements
    int64_t app_profile; // [app] users.id do último perfil escolhido no app; 0 = todos
    int app_sort;        // [app] ordem da grade da tela inicial (ui::SortMode); 0 = recentes
    int app_theme;       // [app] tema do app: 0 = claro (padrão), 1 = escuro
    int app_background;  // [app] fundo: 0 = sólido, 1 = clássico (imagem; padrão)
    int app_language;    // [app] idioma do app: 0 = English (padrão), 1 = português (Brasil)
    int app_date_format; // [app] datas: 0 = MM/DD/YYYY (padrão), 1 = DD/MM/YYYY, 2 = YYYY/MM/DD
    char popup_sfx[64];  // [app] som do pop-up em PT_SFX_DIR; "" = conquista.wav (pt_wav.hpp)
    int popup_flood;     // [overlay] 1 = proteção contra flood de pop-ups (popup_queue.hpp); padrão 0
} PtConfig;

void pt_config_defaults(PtConfig* c);
// Carrega (valores ausentes/inválidos ficam no padrão). Sempre preenche `c`.
void pt_config_load(PtConfig* c);
// Grava as opções de `c` no config.ini (sem as chaves; ver acima).
int pt_config_save_app(const PtConfig* c);
// App: grava as chaves de `c` no secrets.ini (vazio apaga) e regrava o config.ini sem elas.
int pt_secrets_save(const PtConfig* c);
// App, na abertura: se o config.ini ainda tem chaves (versão antiga), passa-as para o
// secrets.ini e as tira do config.ini. Devolve 1 se migrou, 0 se não havia nada, -1 em erro.
int pt_config_migrate_secrets(void);
