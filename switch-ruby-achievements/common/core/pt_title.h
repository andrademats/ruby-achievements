#pragma once
#include <switch.h>
#include <stddef.h>

// Metadados de títulos e contas do Switch, usados pelo sysmodule e pelo app.
// Exigem os serviços ns, set e acc inicializados por quem chama.

// Lê o NACP: nome no idioma do sistema e ícone, salvo em PT_ICONS_DIR/<TID>.jpg
// (icon_path = "" se não salvou). `nacp_size` recebe o tamanho lido.
Result pt_title_read(u64 tid, char* name, size_t name_cap, char* icon_path, size_t icon_cap, u64* nacp_size);

bool pt_account_nickname(AccountUid uid, char* nick, size_t cap);
// account_uid como gravado em users (32 dígitos hex).
void pt_uid_to_string(AccountUid uid, char out[33]);
