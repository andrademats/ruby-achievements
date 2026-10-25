#pragma once
#include <switch.h>
#include <stddef.h>
#include <stdint.h>

// Program ID do aplicativo em execução (em foco ou suspenso na Home). 0 = nenhum.
u64 detect_running_title(void);

// Title ID da QLaunch (menu Home).
#define DETECT_QLAUNCH_TID 0x0100000000001000ULL

// true quando a Home está em primeiro plano: nenhum aplicativo aberto, ou o
// aplicativo `app_tid` saiu de foco (último evento dele no pdm é OutOfFocus) ou
// a QLaunch recebeu o foco depois dele.
bool detect_home_in_front(u64 app_tid);

// Jogo sob o cursor na Home, publicado pelo patch da QLaunch em PT_FOCUS_PATH.
// 0 = nenhum.
u64 detect_focused_title(void);

// Heurística de forwarder: Title IDs gerados por ferramentas de forwarder
// (NSP -> NRO) usam o prefixo 0x05.
bool detect_is_forwarder(u64 tid);

// Forwarder fantasma instalado pelo app (PT_GHOST_TIDS_PATH): não conta tempo.
bool detect_is_ghost(u64 tid);

// Carrega a lista de títulos com NACP bloqueado e trata a marca de uma leitura
// interrompida por travamento (ver PT_PROBE_PATH). Chamar uma vez no início.
void detect_init(void);

// Lê o NACP do título: nome no idioma do sistema e ícone, salvo em
// PT_ICONS_DIR/<TID>.jpg (icon_path recebe o caminho, ou "" se falhar).
bool detect_title_info(u64 tid, char* name, size_t name_cap, char* icon_path, size_t icon_cap);

// Build id do executável principal do aplicativo em execução (64 dígitos hex maiúsculos),
// pelo ldr:dmnt, sem ligar o dmnt ao jogo. false = nenhum aplicativo ou o loader não respondeu.
bool detect_build_id(char out[65]);

// Usuário atual: accountGetLastOpenedUser, depois accountGetPreselectedUser.
bool detect_current_account(AccountUid* uid);
