#pragma once
#include <stdint.h>

// Estado publicado pelo sysmodule para o overlay e o app (PT_STATUS_PATH).
// O sysmodule só regrava o arquivo em mudanças de estado e a cada checkpoint.
// O HUD do overlay mostra focused_playtime (só na Home, onde o jogo suspenso
// não conta tempo); o app usa session_id para reconhecer a sessão viva.
typedef struct {
    char title_id[17];     // título em execução ("" = nenhum, Home)
    int64_t game_id;       // jogo em foco (em execução ou último jogado)
    char game_name[128];
    int64_t session_id;    // sessão viva (0 = nenhuma)
    int64_t user_id;       // usuário da sessão viva
    int64_t profile_id;    // perfil principal usado no cálculo de `base`
    int64_t base;          // soma das sessões fechadas do jogo p/ o perfil (s)
    int64_t live;          // segundos contados na sessão viva no momento da escrita
    int counting;          // 1 = sessão viva conta para o perfil e o relógio corre
    int64_t written_at;    // pt_now() no momento da escrita
    // Home (QLaunch) em primeiro plano: o HUD só aparece aí, com o jogo sob o
    // cursor (focused_tid, lido de PT_FOCUS_PATH). Em jogo, is_qlaunch = 0.
    int is_qlaunch;
    uint64_t focused_tid;      // 0 = nenhum (sem o patch da QLaunch ou fora da grade)
    int64_t focused_game_id;   // 0 = título sem tempo no Ruby Achievements
    char focused_name[128];
    int64_t focused_playtime;  // total do jogo em destaque para o perfil (s)
} PtStatus;

int pt_status_write(const PtStatus* s);
int pt_status_read(PtStatus* s);  // 0 = ok

typedef struct {
    int64_t id;
    char nickname[64];
} PtUserEntry;

// Lista de perfis (PT_USERS_PATH, "id|apelido" por linha). Retorna a quantidade.
int pt_users_read(PtUserEntry* out, int max);
